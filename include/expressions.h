/*
  expressions.h — facial expression library for a 128x64 SSD1306 OLED

      #include "expressions.h"

  Usage in your sketch:

      FaceGeometry face = defaultFaceGeometry();

      void loop() {
        display.clearDisplay();
        expressionHappy(display, face);   // or Sad / Angry / Surprised / Sleepy / Confused / Neutral
        display.display();
        delay(2000);
      }

  Each expressionX() function only draws — it does not call
  clearDisplay() or display() itself, so you can combine expressions
  with your own blink/look-around logic before flushing the frame.

  Eyes are drawn as single large text glyphs matching the ASCII mood
  faces used for Serial logging (^__^, -_-, >__<, O__o, ~__~, ?__?,
  x__x) rather than custom vector shapes.
*/

#ifndef EXPRESSIONS_H
#define EXPRESSIONS_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------- Face geometry ----------------
// Positions/sizes for eyes and mouth. Change defaultFaceGeometry()
// if you resize or reposition things, everything below reads from it.
struct FaceGeometry {
  int leftEyeX, rightEyeX, eyeY;
  int mouthX, mouthY, mouthWidth;
};

inline FaceGeometry defaultFaceGeometry() {
  FaceGeometry g;
  int centerX = 64;
  int eyeSlotWidth = 28; // width reserved for each eye glyph, for centering/spacing
  int eyeSpacing = 12;

  g.eyeY = 20;
  g.leftEyeX = centerX - eyeSlotWidth - eyeSpacing / 2;
  g.rightEyeX = centerX + eyeSpacing / 2;

  g.mouthWidth = 36;
  g.mouthX = centerX - g.mouthWidth / 2;
  g.mouthY = 56;

  return g;
}

// ---------------- Low-level drawing helpers ----------------

// Draws one "eye" as a single big text glyph, centered in a 28px-wide
// slot at (slotX, slotY). Adafruit_GFX's built-in font is 6x8px per
// character at size 1, so glyphs scale predictably with `size`.
inline void drawEyeGlyph(Adafruit_SSD1306 &d, int slotX, int slotY, char c, uint8_t size, uint16_t color) {
  const int slotWidth = 28;
  const int glyphWidth = 6 * size;
  d.setTextSize(size);
  d.setTextColor(color);
  d.setCursor(slotX + (slotWidth - glyphWidth) / 2, slotY);
  d.print(c);
}

// Flat line eye for the quick idle blink — kept visually distinct from
// the deliberate BLINK mood's x_x glyphs (drawn, not a text glyph, so
// it reads as a clean line regardless of font quirks).
inline void drawEyeFlat(Adafruit_SSD1306 &d, int slotX, int slotY, uint16_t c) {
  const int slotWidth = 28;
  const int lineWidth = 20;
  const int lineHeight = 3;
  d.fillRoundRect(slotX + (slotWidth - lineWidth) / 2, slotY + 14, lineWidth, lineHeight, 1, c);
}

// Classic rounded-square eye — used only for the idle resting face
// (expressionIdle below), kept visually distinct from the glyph-based
// moods so a real LLM response is noticeable when it lands.
inline void drawEyeRounded(Adafruit_SSD1306 &d, int slotX, int slotY, uint16_t c) {
  const int eyeWidth = 28;
  const int eyeHeight = 28;
  const int eyeRadius = 8;
  d.fillRoundRect(slotX, slotY, eyeWidth, eyeHeight, eyeRadius, c);
}

// Mouth as a single rectangle, sized/shaped per expression: width,
// height and corner radius set how big/square it reads, and offsetX
// shifts it off-center (e.g. for an asymmetric confused look). Always
// centered on the geometry's mouth slot and top-aligned to mouthY.
inline void drawMouth(Adafruit_SSD1306 &d, const FaceGeometry &g, int width, int height, int radius, uint16_t c, int offsetX = 0) {
  int x = g.mouthX + (g.mouthWidth - width) / 2 + offsetX;
  d.fillRoundRect(x, g.mouthY, width, height, radius, c);
}

// ---------------- Full expressions ----------------
// Each one draws both eyes + a mouth using the given geometry.
// Caller handles clearDisplay() before and display() after.

inline void expressionNeutral(Adafruit_SSD1306 &d, const FaceGeometry &g) {
  drawEyeGlyph(d, g.leftEyeX, g.eyeY, '^', 4, SSD1306_WHITE);
  drawEyeGlyph(d, g.rightEyeX, g.eyeY, '^', 4, SSD1306_WHITE);
  drawMouth(d, g, g.mouthWidth, 6, 3, SSD1306_WHITE);
}

// Resting face shown when no LLM mood is currently active — the
// original rounded-square eyes, so a genuine [HAPPY] response (glyph
// eyes, below) reads as a visible change rather than "nothing happened."
inline void expressionIdle(Adafruit_SSD1306 &d, const FaceGeometry &g) {
  drawEyeRounded(d, g.leftEyeX, g.eyeY, SSD1306_WHITE);
  drawEyeRounded(d, g.rightEyeX, g.eyeY, SSD1306_WHITE);
  drawMouth(d, g, g.mouthWidth, 6, 3, SSD1306_WHITE);
}

inline void expressionHappy(Adafruit_SSD1306 &d, const FaceGeometry &g) {
  drawEyeGlyph(d, g.leftEyeX, g.eyeY, '^', 4, SSD1306_WHITE);
  drawEyeGlyph(d, g.rightEyeX, g.eyeY, '^', 4, SSD1306_WHITE);
  drawMouth(d, g, g.mouthWidth + 6, 5, 2, SSD1306_WHITE); // wide, thin bar
}

inline void expressionSad(Adafruit_SSD1306 &d, const FaceGeometry &g) {
  drawEyeGlyph(d, g.leftEyeX, g.eyeY, '-', 4, SSD1306_WHITE);
  drawEyeGlyph(d, g.rightEyeX, g.eyeY, '-', 4, SSD1306_WHITE);
  drawMouth(d, g, g.mouthWidth - 14, 6, 2, SSD1306_WHITE); // small, narrow bar
}

inline void expressionAngry(Adafruit_SSD1306 &d, const FaceGeometry &g) {
  drawEyeGlyph(d, g.leftEyeX, g.eyeY, '>', 4, SSD1306_WHITE);
  drawEyeGlyph(d, g.rightEyeX, g.eyeY, '<', 4, SSD1306_WHITE);
  drawMouth(d, g, g.mouthWidth - 4, 4, 1, SSD1306_WHITE); // thin, sharp-edged line
}

// O vs o at the same text size naturally renders as a big/small circle
// pair — the lopsided "startled" look comes from the font, not extra logic.
inline void expressionSurprised(Adafruit_SSD1306 &d, const FaceGeometry &g) {
  drawEyeGlyph(d, g.leftEyeX, g.eyeY, 'O', 4, SSD1306_WHITE);
  drawEyeGlyph(d, g.rightEyeX, g.eyeY, 'o', 4, SSD1306_WHITE);
  drawMouth(d, g, 14, 14, 3, SSD1306_WHITE); // big square "O" mouth
}

// Smaller glyph size than the others reads as narrow/tired eyes.
inline void expressionSleepy(Adafruit_SSD1306 &d, const FaceGeometry &g) {
  drawEyeGlyph(d, g.leftEyeX, g.eyeY + 4, '~', 3, SSD1306_WHITE);
  drawEyeGlyph(d, g.rightEyeX, g.eyeY + 4, '~', 3, SSD1306_WHITE);
  drawMouth(d, g, 8, 8, 2, SSD1306_WHITE); // small square
}

inline void expressionConfused(Adafruit_SSD1306 &d, const FaceGeometry &g) {
  drawEyeGlyph(d, g.leftEyeX, g.eyeY, '?', 4, SSD1306_WHITE);
  drawEyeGlyph(d, g.rightEyeX, g.eyeY, '?', 4, SSD1306_WHITE);
  drawMouth(d, g, g.mouthWidth - 14, 6, 2, SSD1306_WHITE); // narrow mouth
}

// The deliberate BLINK mood, from the LLM's [BLINK] tag.
inline void expressionClosedEyes(Adafruit_SSD1306 &d, const FaceGeometry &g) {
  drawEyeGlyph(d, g.leftEyeX, g.eyeY, 'x', 4, SSD1306_WHITE);
  drawEyeGlyph(d, g.rightEyeX, g.eyeY, 'x', 4, SSD1306_WHITE);
  drawMouth(d, g, g.mouthWidth, 6, 3, SSD1306_WHITE);
}

// The fast idle auto-blink — a flat line rather than x_x, so it doesn't
// look like the deliberate BLINK mood firing every few seconds.
inline void expressionBlinkIdle(Adafruit_SSD1306 &d, const FaceGeometry &g) {
  drawEyeFlat(d, g.leftEyeX, g.eyeY, SSD1306_WHITE);
  drawEyeFlat(d, g.rightEyeX, g.eyeY, SSD1306_WHITE);
  drawMouth(d, g, g.mouthWidth, 6, 3, SSD1306_WHITE);
}

// ---------------- Optional: lookup table for cycling expressions ----------------
// Handy if you want to demo/rotate through all of them, e.g.:
//   expressions[i].draw(display, face);

typedef void (*ExpressionFunc)(Adafruit_SSD1306 &, const FaceGeometry &);

struct Expression {
  const char *name;
  ExpressionFunc draw;
};

inline Expression expressions[] = {
  {"NEUTRAL",     expressionNeutral},
  {"HAPPY",       expressionHappy},
  {"SAD",         expressionSad},
  {"ANGRY",       expressionAngry},
  {"SURPRISED",   expressionSurprised},
  {"SLEEPY",      expressionSleepy},
  {"CONFUSED",    expressionConfused},
  {"EYES CLOSED", expressionClosedEyes},
};
const int NUM_EXPRESSIONS = sizeof(expressions) / sizeof(expressions[0]);

#endif // EXPRESSIONS_H
