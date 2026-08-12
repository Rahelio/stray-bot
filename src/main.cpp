#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <cstdlib>
#include <HTTPClient.h>
#include <WiFi.h>
#include <Wire.h>
#include "audio.h"
#include "dialogue.h"
#include "expressions.h"
#include "input.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// ====================================================================
// 1. DISPLAY CONFIG (two Elegoo 0.96" SSD1306s, ESP32-S3-Zero)
// ====================================================================
// TEMP: proving out whether one 0.96" panel can carry both the face and
// the response text — yellow top 16px for text, blue bottom 48px for
// the face — before committing to two physical screens. Flip this back
// to 0 to restore the normal dual-screen wiring/rendering.
#define SINGLE_SCREEN_MODE 1

// Face display, on the primary I2C bus.
#define SDA_PIN 8
#define SCL_PIN 9

#define OLED_ADDR 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
FaceGeometry face = defaultFaceGeometry();

#if !SINGLE_SCREEN_MODE
// Dialogue display, on a second I2C bus — a second SSD1306 hardwired to
// the same default address (0x3C) as the first can't share a bus, so
// this one gets its own pins via the ESP32-S3's second I2C peripheral.
#define SDA2_PIN 6
#define SCL2_PIN 5
#define OLED2_ADDR 0x3C

TwoWire I2CDialogue(1);
Adafruit_SSD1306 dialogueDisplay(SCREEN_WIDTH, SCREEN_HEIGHT, &I2CDialogue, -1);
#endif

#if SINGLE_SCREEN_MODE
// Word-wrapped, confined to y=0..15 (the panel's yellow band) instead
// of dialogue.h's drawDialogue(), which claims the whole screen below
// the yellow strip. Only 2 lines fit at text size 1 (8px/line), so the
// tail-scroll behavior is the same idea as drawDialogue() just with a
// much smaller window.
void drawTopText(Adafruit_SSD1306 &d, const String &revealed) {
  const int maxCharsPerLine = 21; // 128px / 6px-per-char at text size 1
  const int linesOnScreen = 2;    // 16px yellow band / 8px per line
  const int maxWrappedLines = 40; // wrap the whole thing, then show the tail

  String lines[maxWrappedLines];
  int lineCount = wrapText(revealed, maxCharsPerLine, lines, maxWrappedLines);
  int firstVisible = lineCount > linesOnScreen ? lineCount - linesOnScreen : 0;

  d.setTextSize(1);
  d.setTextColor(SSD1306_WHITE);
  for (int i = firstVisible; i < lineCount; i++) {
    d.setCursor(0, (i - firstVisible) * 8);
    d.print(lines[i]);
  }
}
#endif

// ====================================================================
// 2. NETWORK CONFIGURATION (fill these in)
// ====================================================================

// WIFI_SSID/WIFI_PASSWORD/OLLAMA_HOST come from src/.env, injected as
// ENV_* preprocessor defines at compile time by scripts/load_dotenv.py
// (see extra_scripts in platformio.ini) — there's no filesystem on the
// device to read a .env file from at runtime.
const char* WIFI_SSID     = ENV_WIFI_SSID;
const char* WIFI_PASSWORD = ENV_WIFI_PASSWORD;


// Your Ollama host's LAN IP (Mac dev box or Proxmox LXC in production)
const char* OLLAMA_HOST   = ENV_OLLAMA_HOST;
const int   OLLAMA_PORT   = 11434;

// Target the custom model you created with the Modelfile
const char* MODEL_NAME    = "stray-bot";

// ====================================================================
// 3. FACE / MOOD STATE MANAGEMENT
// ====================================================================
// currentFace/moodSetAt/moodActive are written by the LLM task (running
// on its own FreeRTOS task/core) and read by loop() on the main task,
// so all access goes through faceMutex — see setFace()/faceForRender().
enum EyeState { HAPPY, SAD, ANGRY, SURPRISED, SLEEPY, CONFUSED, BLINK, UNKNOWN };
EyeState currentFace = HAPPY;
unsigned long moodSetAt = 0;
const unsigned long moodHoldDuration = 6000;
// True while a mood set by the LLM is still being shown; false once it
// has expired and the face has gone back to the idle resting look —
// lets renderFace() tell "a real [HAPPY] response" apart from "idle".
bool moodActive = false;
SemaphoreHandle_t faceMutex = NULL;

// Return type of faceForRender() — declared up here, above every
// function, so the Arduino IDE's auto-generated prototypes (which get
// inserted right before the first function definition in the file)
// don't end up referencing it before it exists.
struct RenderState {
  EyeState face;
  bool moodActive;
};

// Blink timing (idle animation, only ever touched by the render loop)
unsigned long lastBlink = 0;
unsigned long blinkInterval = 3500;
bool blinking = false;
unsigned long blinkStart = 0;
const int blinkDuration = 180;

// Sets the mood from the LLM task. Safe to call from any task.
void setFace(EyeState f) {
  xSemaphoreTake(faceMutex, portMAX_DELAY);
  currentFace = f;
  moodSetAt = millis();
  moodActive = true;
  xSemaphoreGive(faceMutex);
}

// Reads the mood for rendering, expiring it back to idle once it's had
// its moment on screen. Only called from the render loop.
RenderState faceForRender(unsigned long now) {
  xSemaphoreTake(faceMutex, portMAX_DELAY);
  if (moodActive && now - moodSetAt > moodHoldDuration) {
    moodActive = false;
  }
  RenderState r = { currentFace, moodActive };
  xSemaphoreGive(faceMutex);
  return r;
}

// Maps an EyeState to one of expressions.h's drawing functions.
ExpressionFunc expressionForState(EyeState s) {
  switch (s) {
    case HAPPY:     return expressionHappy;
    case SAD:       return expressionSad;
    case ANGRY:     return expressionAngry;
    case SURPRISED: return expressionSurprised;
    case SLEEPY:    return expressionSleepy;
    case CONFUSED:  return expressionConfused;
    case BLINK:     return expressionClosedEyes;
    default:        return expressionNeutral;
  }
}

void logFaceChange(EyeState face) {
  Serial.print("\n>>> [SCREEN UPDATE]: Drawing Face State -> ");
  switch (face) {
    case HAPPY:     Serial.println("HAPPY [^__^]"); break;
    case SAD:       Serial.println("SAD [-_-]"); break;
    case ANGRY:     Serial.println("ANGRY [>__<]"); break;
    case SURPRISED: Serial.println("SURPRISED [O__o]"); break;
    case SLEEPY:    Serial.println("SLEEPY [~__~]"); break;
    case CONFUSED:  Serial.println("CONFUSED [?__?]"); break;
    case BLINK:     Serial.println("BLINK [x__x]"); break;
    default:        Serial.println("DEFAULT [^__^]"); break;
  }
}

// Advances blink timing and returns the mood to draw this frame. Split
// out from renderFace() so SINGLE_SCREEN_MODE's combined render can
// reuse the same timing logic without duplicating it.
RenderState updateFaceState(unsigned long now) {
  if (!blinking && now - lastBlink > blinkInterval) {
    blinking = true;
    blinkStart = now;
    lastBlink = now;
    blinkInterval = random(2500, 5000);
  }
  if (blinking && now - blinkStart > blinkDuration) {
    blinking = false;
  }

  return faceForRender(now);
}

// Draws the face for the given state onto `d`. Caller handles
// clearDisplay()/display() — combined render (SINGLE_SCREEN_MODE) draws
// dialogue text into the same frame before flushing.
void drawFace(Adafruit_SSD1306 &d, const RenderState &r) {
  if (blinking) {
    expressionBlinkIdle(d, face);
  } else if (!r.moodActive) {
    expressionIdle(d, face);
  } else {
    expressionForState(r.face)(d, face);
  }
}

#if !SINGLE_SCREEN_MODE
// Draws one frame of whatever the face is currently doing. Only ever
// called from loop() on the main task — the LLM task never touches
// the display directly, it just updates mood state via setFace().
void renderFace() {
  unsigned long now = millis();
  RenderState r = updateFaceState(now);

  display.clearDisplay();
  drawFace(display, r);
  display.display();
}
#endif

// Convert string tag (e.g. "SURPRISED") into EyeState enum
void setFaceFromTag(const String& tag) {
  String upperTag = tag;
  upperTag.toUpperCase();
  upperTag.trim();

  EyeState f;
  if (upperTag == "HAPPY")          f = HAPPY;
  else if (upperTag == "SAD")       f = SAD;
  else if (upperTag == "ANGRY")     f = ANGRY;
  else if (upperTag == "SURPRISED") f = SURPRISED;
  else if (upperTag == "SLEEPY")    f = SLEEPY;
  else if (upperTag == "CONFUSED")  f = CONFUSED;
  else if (upperTag == "BLINK")     f = BLINK;
  else                              f = HAPPY;

  setFace(f);
  logFaceChange(f);
}

// ====================================================================
// 4. DIALOGUE DISPLAY STATE MANAGEMENT
// ====================================================================
// Same cross-task pattern as the face/mood state above: pendingDialogue
// is written by the LLM task and drained by the render loop, guarded by
// dialogueMutex. Everything past that point (the reveal timer, the
// full/revealed text) is only ever touched by the render loop.
String pendingDialogue = "";
SemaphoreHandle_t dialogueMutex = NULL;

String dialogueFullText = "";
size_t dialogueRevealCount = 0;
unsigned long lastDialogueReveal = 0;
// ~150 words/min average speaking pace ≈ 14 chars/sec ≈ 70ms/char.
const unsigned long dialogueCharDelay = 70;

// Queues new dialogue text from the LLM task. Safe to call from any task.
void setDialogue(const String &text) {
  xSemaphoreTake(dialogueMutex, portMAX_DELAY);
  pendingDialogue = text;
  xSemaphoreGive(dialogueMutex);
}

// Picks up any newly-queued dialogue and advances the typewriter reveal
// by one character every dialogueCharDelay ms. Split from the draw call
// so SINGLE_SCREEN_MODE's combined render can reuse this timing logic.
// Returns true if the reveal count just advanced (i.e. there's new text
// to redraw this call).
bool updateDialogueReveal() {
  xSemaphoreTake(dialogueMutex, portMAX_DELAY);
  if (pendingDialogue.length() > 0) {
    dialogueFullText = pendingDialogue;
    pendingDialogue = "";
    dialogueRevealCount = 0;
    audioStop(); // new response landed mid-reveal — don't let the old blip bleed into it
  }
  xSemaphoreGive(dialogueMutex);

  unsigned long now = millis();
  if (dialogueRevealCount < dialogueFullText.length() && now - lastDialogueReveal > dialogueCharDelay) {
    dialogueRevealCount++;
    lastDialogueReveal = now;
    audioOnCharacterRevealed(dialogueFullText.charAt(dialogueRevealCount - 1));
    return true;
  }
  return false;
}

#if SINGLE_SCREEN_MODE
// Single-panel version of renderFace(): one clearDisplay()/display() per
// frame, text in the yellow top strip, face in the blue bottom — see
// drawTopText() above and drawFace() below. Named renderFace() (not
// renderCombined()) so loop()/connectToWiFi() don't need to branch on
// SINGLE_SCREEN_MODE themselves; it now also does what tickDialogue()
// used to (there's no separate dialogue panel to tick).
void renderFace() {
  unsigned long now = millis();
  RenderState r = updateFaceState(now);
  updateDialogueReveal();

  display.clearDisplay();
  drawTopText(display, dialogueFullText.substring(0, dialogueRevealCount));
  drawFace(display, r);
  display.display();
}
#else
// Two-panel version: dialogue gets its own physical OLED, drawn only
// when a new character is revealed (rather than every loop() tick).
void tickDialogue() {
  if (updateDialogueReveal()) {
    drawDialogue(dialogueDisplay, dialogueFullText.substring(0, dialogueRevealCount));
  }
}
#endif

// ====================================================================
// 5. USER INPUT -> LLM PROMPT QUEUE
// ====================================================================
// Opposite direction from the mood/dialogue queues above: the main task
// (button poll in loop(), see input.h) is the producer, and llmTask is
// the consumer. Same mutex-guarded hand-off pattern either way.
String pendingPrompt = "";
SemaphoreHandle_t promptMutex = NULL;

// Called from loop() when inputPoll() reports a new prompt.
void requestPrompt(const String &prompt) {
  xSemaphoreTake(promptMutex, portMAX_DELAY);
  pendingPrompt = prompt;
  xSemaphoreGive(promptMutex);
}

// Called from llmTask to pick up a queued prompt, if any.
bool takePendingPrompt(String &outPrompt) {
  bool got = false;
  xSemaphoreTake(promptMutex, portMAX_DELAY);
  if (pendingPrompt.length() > 0) {
    outPrompt = pendingPrompt;
    pendingPrompt = "";
    got = true;
  }
  xSemaphoreGive(promptMutex);
  return got;
}

// ====================================================================
// 6. NETWORK / LLM HELPERS
// ====================================================================
void connectToWiFi() {
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    renderFace(); // still on the main task here, pre-LLM-task, so safe to draw
    delay(500);
    Serial.print(".");
  }

  Serial.println("\n[WiFi] Connected successfully!");
  Serial.print("[WiFi] ESP32-S3 IP Address: ");
  Serial.println(WiFi.localIP());
}

void askLLM(const String& userPrompt) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Error] Wi-Fi is disconnected!");
    return;
  }

  WiFiClient client;
  HTTPClient http;

  String endpoint = "http://" + String(OLLAMA_HOST) + ":" + String(OLLAMA_PORT) + "/api/generate";
  http.begin(client, endpoint);
  http.addHeader("Content-Type", "application/json");

  // Construct JSON Payload for Ollama
  JsonDocument requestDoc;
  requestDoc["model"] = MODEL_NAME;
  requestDoc["prompt"] = userPrompt;
  // Non-streaming: Ollama's streamed reply comes chunked-transfer-encoded,
  // and reading that raw via getStreamPtr() (as this used to) corrupts
  // every JSON line with leftover chunk-size framing. getString() below
  // de-chunks properly. TODO: bring streaming back once token-by-token
  // output is tied to audio playback instead of just Serial.
  requestDoc["stream"] = false;

  String requestBody;
  serializeJson(requestDoc, requestBody);

  Serial.println("\n==============================================");
  Serial.print("User: ");
  Serial.println(userPrompt);

  int httpCode = http.POST(requestBody);

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();

    JsonDocument responseDoc;
    DeserializationError error = deserializeJson(responseDoc, payload);

    if (!error) {
      const char* fullResponse = responseDoc["response"];
      if (fullResponse) {
        String text = String(fullResponse);

        int openBracket  = text.indexOf('[');
        int closeBracket = text.indexOf(']');

        if (openBracket != -1 && closeBracket != -1 && closeBracket > openBracket) {
          String faceTag = text.substring(openBracket + 1, closeBracket);
          setFaceFromTag(faceTag);
          text = text.substring(closeBracket + 1);
          text.trim();
        } else {
          Serial.println("[Warn] No [MOOD] tag found in response");
        }

        Serial.print("Stray-Bot Dialogue: ");
        Serial.println(text);
        setDialogue(text);
      }
    } else {
      Serial.printf("[JSON ERROR] Failed to parse response: %s\n", error.c_str());
    }
  } else {
    Serial.printf("[HTTP ERROR] Failed with code: %d\n", httpCode);
  }

  Serial.println("==============================================");
  http.end(); // Close connection
}

// FreeRTOS task: owns all LLM calls, runs on its own core so the
// render loop (on the main task) never blocks waiting on the network.
// Waits for prompts queued via requestPrompt() (currently from the
// button in input.h) instead of running a fixed script.
void llmTask(void* pvParameters) {
  for (;;) {
    String prompt;
    if (takePendingPrompt(prompt)) {
      askLLM(prompt);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ====================================================================
// 7. MAIN ENTRY POINTS
// ====================================================================
void setup() {
  Serial.begin(115200);
  delay(2000); // Allow USB CDC serial to start on ESP32-S3

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 not found — check wiring/address");
    while (true) delay(1000);
  }
  display.clearDisplay();
  display.display();
  randomSeed(analogRead(0));

#if !SINGLE_SCREEN_MODE
  I2CDialogue.begin(SDA2_PIN, SCL2_PIN);
  if (!dialogueDisplay.begin(SSD1306_SWITCHCAPVCC, OLED2_ADDR)) {
    Serial.println("Second SSD1306 (dialogue) not found — check wiring/address");
    while (true) delay(1000);
  }
  dialogueDisplay.clearDisplay();
  dialogueDisplay.display();
#endif

  faceMutex = xSemaphoreCreateMutex();
  dialogueMutex = xSemaphoreCreateMutex();
  promptMutex = xSemaphoreCreateMutex();
  inputBegin();
  audioBegin();

  connectToWiFi();

  // Pin the LLM task to core 0, away from the render loop on core 1,
  // so a slow/stuck request never stalls the face animation.
  xTaskCreatePinnedToCore(llmTask, "LLM Task", 8192, NULL, 1, NULL, 0);
}

void loop() {
  renderFace();
#if !SINGLE_SCREEN_MODE
  tickDialogue();
#endif

  String prompt;
  if (inputPoll(prompt)) {
    requestPrompt(prompt);
  }

  delay(16); // ~60fps-ish loop
}
