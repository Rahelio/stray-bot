/*
  input.h — physical trigger for sending a prompt to the LLM

      #include "input.h"

  Usage in your sketch:

      inputBegin();               // in setup()

      String prompt;
      if (inputPoll(prompt)) {    // in loop(), every iteration
        // got a new prompt — hand it off to whatever calls askLLM()
      }

  Currently backed by a single debounced push-button, which cycles
  through a small list of canned prompts on each press (there's no
  freeform text without speech-to-text). To swap in a microphone
  later: keep inputBegin()/inputPoll()'s signatures the same, but have
  inputPoll() listen for speech and return its transcription instead
  of walking the canned list. Nothing outside this file needs to change.
*/

#ifndef INPUT_H
#define INPUT_H

#include <Arduino.h>

// ---------------- Button wiring ----------------
// One leg to INPUT_BUTTON_PIN, the other to GND. Uses the internal
// pull-up, so the pin reads HIGH when idle and LOW when pressed.
#ifndef INPUT_BUTTON_PIN
#define INPUT_BUTTON_PIN 1
// pick a free GPIO — change this if 4 is already in use
#endif

const unsigned long inputDebounceDelay = 40; // ms

// A handful of prompts to rotate through on each press, until a
// mic/STT input replaces this with freeform transcribed speech.
inline const char *inputCannedPrompts[] = {
  "Hello there!",
  "What do you remember about humans?",
  "What is your saddest memory?",
  "Did you hear that loud crash down the alleyway?",
};
const int inputCannedPromptCount = sizeof(inputCannedPrompts) / sizeof(inputCannedPrompts[0]);

// Debounce/edge-detect state — only ever touched by whichever task
// calls inputPoll() (expected to be the main loop).
inline int inputLastRawState = HIGH;
inline int inputStableState = HIGH;
inline unsigned long inputLastChangeTime = 0;
inline int inputNextPromptIndex = 0;

inline void inputBegin() {
  pinMode(INPUT_BUTTON_PIN, INPUT_PULLUP);
}

// Call every loop() iteration. Returns true (with `outPrompt` filled
// in) exactly once per debounced button press.
inline bool inputPoll(String &outPrompt) {
  int raw = digitalRead(INPUT_BUTTON_PIN);
  unsigned long now = millis();

  if (raw != inputLastRawState) {
    inputLastChangeTime = now;
    inputLastRawState = raw;
  }

  if (now - inputLastChangeTime > inputDebounceDelay && raw != inputStableState) {
    inputStableState = raw;
    if (inputStableState == LOW) { // pull-up wiring: LOW means pressed
      outPrompt = inputCannedPrompts[inputNextPromptIndex];
      inputNextPromptIndex = (inputNextPromptIndex + 1) % inputCannedPromptCount;
      return true;
    }
  }

  return false;
}

#endif // INPUT_H
