# MomoRTOS — Codebase Overview

PlatformIO/Arduino firmware for an ESP32-S3 robot ("Stray-Bot") with an animated
face, typewriter dialogue, and an Ollama-backed LLM brain. Two FreeRTOS tasks:
render loop (core 1) and LLM networking (core 0), synced via mutex-guarded
shared state.

## Files

- **`src/main.cpp`** — entry point and glue. Wi-Fi setup, HTTP calls to Ollama
  (`askLLM`), face/mood + dialogue state machines, the `llmTask` FreeRTOS task,
  and `setup()`/`loop()`. Currently in `SINGLE_SCREEN_MODE` (one OLED split
  into a text strip + face, vs. the two-panel wiring it can fall back to).
- **`include/expressions.h`** — draws faces (eyes + mouth) on the SSD1306 OLED:
  Happy/Sad/Angry/Surprised/Sleepy/Confused/Idle/Blink, built from text glyphs
  (`^__^`, `-_-`, etc.) plus rounded-rect mouths.
- **`include/dialogue.h`** — word-wrapping + scrolling text renderer for the
  OLED (terminal-style tail scroll once text overflows the screen).
- **`include/audio.h`** — synthesizes robotic "talking" blips on a passive
  buzzer via `tone()`, paced to sync with the dialogue typewriter reveal.
- **`include/input.h`** — debounced push-button input that cycles through
  canned prompts (placeholder until mic/STT input is added).
- **`platformio.ini`** — ESP32-S3-DevKitC-1 board config, PSRAM/flash settings,
  library deps (NeoPixel, GFX, SSD1306, WiFi, HTTPClient, ArduinoJson).
- **`Audio_files/`** — reference sound effect that `audio.h`'s blip pattern was
  modeled on.
- **`lib/`, `test/`** — empty PlatformIO scaffolding (unused so far).

## Data flow

Button press (`input.h`) → prompt queued → `llmTask` POSTs to Ollama →
response parsed for a `[MOOD]` tag + dialogue text → mood and text handed off
to the render loop → face drawn (`expressions.h`) and text typewriter-revealed
(`dialogue.h`) with synced buzzer blips (`audio.h`).

## Note

`src/main.cpp` has the Wi-Fi SSID/password and Ollama host hardcoded in plain
text — worth pulling into a gitignored config if this repo is ever made public.
