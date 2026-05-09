# EVM Machine — ESP32 RFID School Voting System

A secure, RFID-based electronic voting machine built on the ESP32. Designed for school elections (house captain, cultural captain, sports captain), it combines physical RFID card authentication with a WiFi web interface for admin control.

![Hardware: ESP32 + MFRC522 RFID + 16x2 LCD + Buzzer]

## Features

- **RFID-based voting** — Voters authenticate using physical RFID cards. Only pre-registered cards (master + candidates) are accepted.
- **Master card unlock** — An admin/master card enables voting mode. No votes can be cast without it, preventing unauthorized voting.
- **6 independent sections** — Separate vote tallies for Sports Captain, Cultural Captain, Red House, Blue House, Green House, and Yellow House.
- **WiFi web admin panel** — The ESP32 acts as a WiFi access point. Connect to the hotspot and use the browser to switch sections or reset votes on the current section.
- **Non-volatile storage** — Vote counts persist across power cycles using the ESP32's NVS (Preferences) library.
- **LCD + buzzer feedback** — Each action is confirmed via the 16x2 LCD display and piezo buzzer tones.

## How It Works

```
                 ┌─────────────┐
                 │  ESP32      │
                 │  (AP Mode)  │
                 │             │
   ┌─────────────┤  Web Server ├────── Admin Phone/Tablet
   │             │  Port 80    │       (WiFi: SchoolVoting)
   │             └──────┬──────┘
   │                    │
   ▼                    ▼
┌──────┐          ┌──────────┐
│ RFID │◄────────►│  LCD     │
│Reader│  SPI     │ 16x2 I2C │
└──────┘          └──────────┘
   │                    ▲
   ▼                    │
┌──────┐          ┌──────────┐
│RFID  │          │  Buzzer  │
│Cards │          │  GPIO 15 │
└──────┘          └──────────┘
```

### Voting Flow

1. The machine boots in **locked state**. The LCD shows the current section and prompts "Tap Master".
2. An admin taps the **master card**. The LCD shows "Vote Enabled" and prompts "Tap Candidate".
3. A voter taps a **candidate card** (A or B). The vote is recorded, the tally is saved to NVS, and the machine re-locks.
4. If a non-registered card is tapped during voting mode, the machine shows "Invalid Card" and re-locks.
5. If any card (other than master) is tapped while the machine is locked, it shows "Machine Locked".

### Admin Web UI

Connect to the `SchoolVoting` WiFi network (password: `vote1234`), then open **http://192.168.4.1**. From the web panel you can:

- View the current section and live vote counts
- Switch between any of the 6 voting sections
- Reset votes for the currently active section

## Hardware Requirements

| Component           | Specification              | Qty |
|---------------------|----------------------------|-----|
| ESP32 Dev Board     | Any ESP32 development board| 1   |
| MFRC522 RFID Reader | SPI interface              | 1   |
| 16x2 LCD            | I2C (address 0x27)         | 1   |
| Piezo Buzzer        | 5V active buzzer           | 1   |
| RFID Tags           | 13.56 MHz (MIFARE)         | 3+  |
| Jumper Wires        | Female-to-female           | Set |
| Breadboard          | Optional                   | 1   |

## Pin Connections

| ESP32 GPIO | Connected To     | Notes              |
|------------|------------------|--------------------|
| GPIO 5     | RFID SDA (SS)    | Slave select       |
| GPIO 4     | RFID RST         | Reset              |
| GPIO 15    | Buzzer (+)       | PWM-capable        |
| SDA (GPIO21)| LCD SDA         | I2C data           |
| SCL (GPIO22)| LCD SCL         | I2C clock          |
| 3.3V       | RFID 3.3V, LCD   | Common power rail  |
| GND        | RFID GND, LCD    | Common ground      |

RFID also requires MOSI (GPIO23), MISO (GPIO19), and SCK (GPIO18) connected to the ESP32's default SPI pins.

## Setup Instructions

### 1. Install Dependencies

- Install the **Arduino IDE** (or PlatformIO)
- Add **ESP32 board support** via Boards Manager (`esp32` by Espressif)
- Install these libraries via Library Manager:
  - `MFRC522` by GithubCommunity
  - `LiquidCrystal_I2C` by Frank de Brabander

### 2. Configure RFID UIDs

Upload the sketch, open the **Serial Monitor** (115200 baud), and tap each RFID card. Note the UID printed for each one. Then update these lines in the sketch:

```cpp
String MASTER_UID = "f5ecaa4";
String CANDIDATE_A_UID = "f2236c5";
String CANDIDATE_B_UID = "c7ef6c5";
```

### 3. (Optional) Configure WiFi

```cpp
const char* ssid = "SchoolVoting";
const char* password = "vote1234";
```

### 4. Wire the Hardware

Follow the pin connections table above. Power the ESP32 via USB — the board provides 3.3V to the RFID and LCD modules.

### 5. Upload

Select your board and port in the Arduino IDE, then upload.

## Library Reference

| Library              | Purpose                           | Included With ESP32? |
|----------------------|-----------------------------------|---------------------|
| `WiFi.h`             | Soft access point mode            | Yes (built-in)      |
| `WebServer.h`        | HTTP server for admin panel       | Yes (built-in)      |
| `Preferences.h`      | Non-volatile key-value storage    | Yes (built-in)      |
| `SPI.h`              | SPI bus for RFID reader           | Yes (built-in)      |
| `MFRC522`            | RFID reader driver                | No (install via Library Manager) |
| `Wire.h`             | I2C bus for LCD                   | Yes (built-in)      |
| `LiquidCrystal_I2C`  | I2C LCD driver                    | No (install via Library Manager) |

## File Structure

```
EVM-Machine/
├── esp32_robotichand_copy_20260508171514/
│   └── esp32_robotichand_copy_20260508171514.ino   # Main sketch (~470 lines)
├── CLAUDE.md                                        # Guidance for AI-assisted development
└── README.md
```

## Customization

- **Add more candidates** — Extend the `loop()` function with additional UID checks and vote variables
- **Add more sections** — Add a new web route in `setup()` following the existing pattern, then add the button in `handleRoot()`
- **Change LCD rows** — The code assumes a 16×2 LCD. Update `lcd.print()` calls and row positions for other sizes
- **Add an admin password** — Require a button press or secondary RFID tap on the web UI to prevent unauthorized section switching

## Security Notes

This is a **physical-access-gated system** suitable for school environments. The master card serves as the sole authentication mechanism — anyone with physical access to the master card can vote. For higher-security deployments, consider:

- Requiring a PIN via the web panel after master card authentication
- Logging all votes with timestamps to serial output
- Adding an audit trail (print vote summary to serial when section is changed)

## License

MIT
