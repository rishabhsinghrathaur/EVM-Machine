# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based school voting machine with RFID authentication and a WiFi web admin UI. Uses an LCD display and buzzer for local feedback. Votes are persisted via the Preferences (NVS) library.

## Hardware Requirements

- ESP32 development board
- MFRC522 RFID reader (SPI: SS=5, RST=4)
- 16x2 I2C LCD (address 0x27)
- Piezo buzzer (GPIO 15)
- RFID tags: one master card, two candidate cards per section

## Pin Mapping

| Component   | Pin  | Notes            |
|-------------|------|------------------|
| RFID SS     | 5    | MFRC522 SDA      |
| RFID RST    | 4    | MFRC522 RST      |
| Buzzer      | 15   | Active high      |
| LCD         | I2C  | Default SDA/SCL  |

## WiFi (Soft AP)

- SSID: `SchoolVoting`, Password: `vote1234`
- Default IP: 192.168.4.1
- Web UI allows changing the active voting section and resetting votes

## Build & Flash

Open the `.ino` file in Arduino IDE (with ESP32 board support installed), select the correct board/port, and upload. Required libraries:
- `MFRC522` by GitHubCommunity
- `LiquidCrystal_I2C` by Frank de Brabander
- `WiFi`, `WebServer`, `Preferences` (ESP32 built-in)

To build from CLI (requires Arduino CLI or PlatformIO):

```bash
# Arduino CLI
arduino-cli compile --fqbn esp32:esp32:esp32 .
arduino-cli upload --fqbn esp32:esp32:esp32 -p /dev/ttyUSB0 .
```

## Architecture

The entire application is a single `.ino` file with these sections:

1. **Setup** — initializes LCD, RFID, SPI, Preferences NVS, WiFi soft AP, web routes, then shows locked screen
2. **Main loop** — polls web server, checks for new RFID cards, routes based on UID and voting state
3. **RFID flow** — master card enables voting mode (60s window), candidate card A/B registers +1 vote, any other card in voting mode shows error. Outside voting mode, tapping a non-master card shows "Machine Locked"
4. **Sections** — 6 independent voting sections: Sports Captain, Cultural Captain, Red/Blue/Green/Yellow House. Each has its own vote tally (stored with a key prefix in NVS)
5. **Web UI** — mobile-friendly HTML served from `handleRoot()`. Buttons to switch sections and reset votes for the current section
6. **Persistence** — `saveVotes()`/`loadVotes()` write/read candidate A/B counts to NVS using the section key as prefix

## Key Constants (configure before flash)

- `MASTER_UID`, `CANDIDATE_A_UID`, `CANDIDATE_B_UID` — read from actual RFID tags (see Serial output)
- `ssid`, `password` — WiFi credentials for the soft AP
- `currentSection`/`currentKey` — default voting section on boot (Sports Captain)
