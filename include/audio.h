/*
  audio.h — robotic "talking" blips on a passive buzzer

      #include "audio.h"

  Usage in your sketch:

      audioBegin();                          // in setup()
      audioOnCharacterRevealed(c);           // in updateDialogueReveal(), each
                                              // time a new character is revealed
      audioStop();                           // when a new response cuts a blip short

  Modeled on Audio_files/Voicy_Stray _ Droid Talking Voice [Sound Effect].mp3,
  which turned out to be 40 discrete square-wave-ish blips over ~9.9s (not
  continuous audio): ~80-250ms per blip, ~40-100ms silence between, pitch
  wandering ~250-700Hz with occasional spikes toward 900Hz. A passive buzzer
  can't play the recording itself (no DAC — tone() is a square wave only),
  but it can reproduce this same blip pattern live via tone(), synced for
  free to the typewriter reveal instead of needing a separate audio file.

  Paced by audioNextBlipAt rather than firing on every reveal tick, so the
  ~4 blips/sec cadence holds steady even if dialogueCharDelay changes.
*/

#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>

// ---------------- Buzzer wiring ----------------
// + leg to BUZZER_PIN, - leg to GND. Passive buzzer — driven with tone()
// (a square wave at a chosen frequency), not a fixed on/off drive.
#ifndef BUZZER_PIN
#define BUZZER_PIN 12
#endif

// Measured off the reference sound effect (see file comment above).
const unsigned int audioBlipFreqMin        = 250;
const unsigned int audioBlipFreqMax        = 700;
const unsigned int audioBlipFreqSpikeMax   = 900; // occasional high note, like the file's 831/962Hz outliers
const int audioBlipSpikeChancePct          = 15;

const unsigned long audioBlipDurationMin = 80;  // ms
const unsigned long audioBlipDurationMax = 250; // ms
const unsigned long audioBlipGapMin      = 40;  // ms, silence after a blip before the next is allowed
const unsigned long audioBlipGapMax      = 100; // ms

// Only ever touched from the render loop (same task that drives the
// dialogue reveal and calls audioOnCharacterRevealed()).
inline unsigned long audioNextBlipAt = 0;

inline void audioBegin() {
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);
}

// Cuts off whatever blip is currently sounding. Call this when a new
// response interrupts dialogue reveal mid-blip, so the old note doesn't
// bleed into the new one.
inline void audioStop() {
  noTone(BUZZER_PIN);
  audioNextBlipAt = 0;
}

// Fires one randomized blip and schedules when the next one is allowed.
inline void audioBlip() {
  unsigned int freq = (random(100) < audioBlipSpikeChancePct)
    ? random(audioBlipFreqMax, audioBlipFreqSpikeMax + 1)
    : random(audioBlipFreqMin, audioBlipFreqMax + 1);
  unsigned long dur = random(audioBlipDurationMin, audioBlipDurationMax + 1);

  tone(BUZZER_PIN, freq, dur);
  audioNextBlipAt = millis() + dur + random(audioBlipGapMin, audioBlipGapMax + 1);
}

// Call every time the dialogue typewriter reveals one more character,
// passing that character in. Skips spaces (blips land on "syllables", not
// the gaps between words) and self-paces via audioNextBlipAt.
inline void audioOnCharacterRevealed(char justRevealedChar) {
  if (justRevealedChar == ' ') return;
  if (millis() < audioNextBlipAt) return;
  audioBlip();
}

#endif // AUDIO_H