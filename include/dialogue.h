/*
  dialogue.h — word-wrapped text renderer for a 128x64 SSD1306 OLED

      #include "dialogue.h"

  Usage in your sketch:

      drawDialogue(dialogueDisplay, revealedTextSoFar);

  Pure rendering only — word-wraps whatever text you pass in across the
  screen, and once it overflows one screen's worth of lines, scrolls to
  show the most recent lines (like a terminal). This file doesn't own
  any typewriter-reveal timing; that's sketch state (see MomoRTOS.ino's
  tickDialogue()), since it needs to persist across loop() iterations.
*/

#ifndef DIALOGUE_H
#define DIALOGUE_H

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Splits `text` into lines that fit `maxCharsPerLine`, breaking at the
// last space before the limit where possible (hard-breaks a single
// word that alone exceeds the width). Returns the number of lines
// written into `lines` (capped at `maxLines`).
inline int wrapText(const String &text, int maxCharsPerLine, String lines[], int maxLines) {
  int lineCount = 0;
  int start = 0;
  int len = text.length();

  while (start < len && lineCount < maxLines) {
    int remaining = len - start;
    int take = remaining < maxCharsPerLine ? remaining : maxCharsPerLine;
    int breakAt = start + take;

    if (take == maxCharsPerLine && breakAt < len) {
      int lastSpace = text.lastIndexOf(' ', breakAt - 1);
      if (lastSpace > start) {
        breakAt = lastSpace;
      }
    }

    lines[lineCount++] = text.substring(start, breakAt);
    start = breakAt;
    while (start < len && text.charAt(start) == ' ') start++; // skip the space we broke on
  }

  return lineCount;
}

// Draws `revealed` word-wrapped onto the screen, scrolling to the last
// screenful of lines once it overflows.
inline void drawDialogue(Adafruit_SSD1306 &d, const String &revealed) {
  const int topOffset = 16;       // the top ~16px of these panels is yellow, not blue — stay clear of it
  const int maxCharsPerLine = 20; // 128px / 6px-per-char at text size 1, with margin
  const int linesOnScreen = (64 - topOffset) / 8; // fit within the usable (non-yellow) height
  const int maxWrappedLines = 40; // generous cap for a short 1-3 sentence reply

  String lines[maxWrappedLines];
  int lineCount = wrapText(revealed, maxCharsPerLine, lines, maxWrappedLines);

  int firstVisible = lineCount > linesOnScreen ? lineCount - linesOnScreen : 0;

  d.clearDisplay();
  d.setTextSize(1);
  d.setTextColor(SSD1306_WHITE);
  for (int i = firstVisible; i < lineCount; i++) {
    d.setCursor(0, topOffset + (i - firstVisible) * 8);
    d.print(lines[i]);
  }
  d.display();
}

#endif // DIALOGUE_H
