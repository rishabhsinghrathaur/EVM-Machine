# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based school voting machine with RFID authentication and a WiFi web admin UI. Uses an LCD display and buzzer for local feedback. Votes are persisted via the Preferences (NVS) library.

## Hardware

- ESP32 dev board
- MFRC522 RFID reader (SPI: SS=5, RST=4)
- 16x2 I2C LCD (address 0x27)
- Piezo buzzer (GPIO 15)
- RFID tags: master card + candidate A/B cards

## WiFi (Soft AP)

- SSID: `SchoolVoting`, Password: `vote1234`
- Default IP: 192.168.4.1

## Build & Flash

Arduino IDE with ESP32 board support. Required libraries:
- `MFRC522` by GithubCommunity
- `LiquidCrystal_I2C` by Frank de Brabander

CLI:
```bash
arduino-cli compile --fqbn esp32:esp32:esp32 .
arduino-cli upload --fqbn esp32:esp32:esp32 -p /dev/ttyUSB0 .
```

## Architecture

Single `.ino` file, structured as:

### State Machine (non-blocking)
All user-visible delays are handled by `tickStateMachine()` using `millis()` comparisons — no `delay()` calls in the main loop. States:
- `ST_LOCKED` / `ST_VOTING_READY` — wait indefinitely for RFID
- `ST_VOTE_CONFIRMED` / `ST_INVALID_CARD` / `ST_LOCKED_ALERT` — show feedback for 2 s, then auto-lock
- `ST_SECTION_LOADED` — show confirmation for 1.5 s, then lock
- `ST_RESET_PENDING` — waiting for master card tap (10 s window)
- `ST_RESET_CONFIRMED` — show confirmation for 2 s, then lock

### NVS Persistence
Two Preferences namespaces:
- `"voting"` — vote counts per section (keys: `{sectionKey}A`, `{sectionKey}B`)
- `"uids"` — RFID UIDs (keys: `master`, `candA`, `candB`); written once with defaults on first boot, then configurable via web UI

### RFID Flow
1. Locked → tap master → voting enabled
2. Voting → tap candidate A/B → vote recorded → auto-lock
3. Voting → tap invalid card → error shown → auto-lock
4. Locked → tap non-master → "Machine Locked" alert → auto-lock
5. Reset pending → tap master → votes reset → confirmation → lock

### Web UI
- `GET /` — full admin page with live JS polling (`/api/status` every 2 s)
- `GET /api/status` — JSON: `section`, `sectionKey`, `votesA`, `votesB`, `state`
- `POST /api/reset` — triggers reset-pending state (requires physical master card tap to confirm)
- `GET/POST /api/uids` — read/update RFID card UIDs without re-flashing
- `GET /{sectionKey}` — switch voting section

### Audit Trail
All significant actions logged to Serial with millis timestamp: `[ms] action | Section  A=N B=N`

## Key Differences From a Basic Sketch

- **No blocking delays** — state machine handles all timing, web server stays responsive during feedback displays
- **Reset requires physical master card** — web UI button triggers a confirmation window; tap master card within 10 s to complete
- **UIDs configurable via web UI** — change RFID cards without re-flashing; stored in NVS "uids" namespace
- **Real-time web UI** — JavaScript polling updates vote counts and machine state every 2 s
