# GateX ESP32 Firmware

This folder contains production-oriented ESP32 firmware (Arduino framework) that integrates **directly** with your existing PHP JSON APIs + MySQL + JS dashboard.

## What this firmware does

- Connects to Wi‑Fi with reconnect/backoff
- Fetches settings from:
  - `getSettings.php?device_id=<id>&include_pin_hash=1`
- Logs **every** access attempt via:
  - `postAccessAttempt.php` (JSON)
- Posts current status (periodic + on change) via:
  - `postStatus.php` (JSON)
- Sends alerts via:
  - `postAlert.php` (JSON)
- Operates locally if server is unreachable:
  - Access attempts + alerts are queued in RAM and resent later
  - Status is coalesced (keeps latest unsent status only)

## Hardware pin map (FROZEN)

Matches your Final_wire plan:

- OLED I2C: SDA GPIO21, SCL GPIO22 (3.3V)
- Fingerprint AS608 UART2: RX GPIO16, TX GPIO17 (3.3V)
- Reed switch: GPIO32 INPUT_PULLUP, CLOSED = LOW
- Keypad rows: GPIO25,26,27,14 (outputs)
- Keypad cols: GPIO34,35,36,39 (inputs, external pullups)
- LEDs: Green GPIO13, Red GPIO19, LED_A GPIO33
- Relay IN1: GPIO23 (active-LOW by default)
- Buzzer: GPIO18 (PWM tone)

## IMPORTANT: PIN hash format (offline verification)

This firmware can verify keypad PIN **offline** only if `pin_hash` in your DB is:

- **SHA‑256 hex** (64 hex characters), compared as `sha256(pin) == pin_hash`

If your server stores PIN in a different format, update the firmware verification logic in `main.cpp -> verifyPinLocal()`.

## Arduino IDE build notes

Arduino IDE works best when the folder is a sketch folder. You can:

1. Create a new sketch named `gatex_firmware`
2. Copy all files in `/firmware` into that sketch folder
3. Make sure `main.cpp` is present (Arduino will compile .cpp files)
4. Install libraries (see below)
5. Select board: **ESP32 Dev Module**
6. Select the right COM port (CP2102)
7. Upload

## PlatformIO (recommended)

1. Create a new PlatformIO project:
   - Board: `esp32dev`
   - Framework: Arduino
2. Copy `firmware/*` into `src/` (or keep as `firmware/` and adjust `src_dir`)
3. Add dependencies to `platformio.ini` (example below)

Example `platformio.ini`:

```
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200

lib_deps =
  bblanchon/ArduinoJson
  adafruit/Adafruit GFX Library
  adafruit/Adafruit SSD1306
  adafruit/Adafruit Fingerprint Sensor Library
```

## Required libraries

- ArduinoJson
- Adafruit GFX Library
- Adafruit SSD1306
- Adafruit Fingerprint Sensor Library

(ESP32 core provides WiFi + HTTPClient + Preferences.)

## Bring-up checklist (recommended order)

### 1) OLED
- Power ESP32
- OLED should show Boot/WiFi/Ready screens
- If blank:
  - Check SDA=21, SCL=22
  - Try I2C address 0x3C vs 0x3D

### 2) Reed switch
- Open Serial Monitor @115200
- Watch `door_state` updates on OLED (OPEN/CLOSED)

### 3) Keypad
- Press digits, OLED should show masked `PIN: ****`
- `*` clears entry
- `#` submits PIN

### 4) Relay (NO solenoid first)
- Enter correct PIN (requires correct pin_hash in server)
- Relay should click for 1 second
- Cooldown prevents re-trigger for 10 seconds

### 5) Solenoid
- Connect solenoid + flyback diode correctly
- Verify 1 second unlock pulse only

### 6) Fingerprint
- If sensor is wired and powered, it will grant on match.
- Unknown finger produces a denied event with reason `no_match`.

### 7) API verification
From your LAN PC, you can test endpoints with curl (adjust BASE_URL):

- Settings (GET):
  `getSettings.php?device_id=1&include_pin_hash=1`

- Status (POST JSON):
  `postStatus.php`
  ```json
  {
    "device_id": 1,
    "event_uuid": "uuid-here",
    "lock_state": "locked",
    "door_state": "closed",
    "system_state": "armed",
    "wifi_rssi": -50
  }
  ```

- Access attempt (POST JSON):
  `postAccessAttempt.php`
  ```json
  {
    "device_id": 1,
    "event_uuid": "uuid-here",
    "method": "keypad",
    "result": "granted",
    "username_snapshot": "Keypad"
  }
  ```

- Alert (POST JSON):
  `postAlert.php`
  ```json
  {
    "device_id": 1,
    "event_uuid": "uuid-here",
    "type": "forced_entry",
    "severity": "critical",
    "message": "Door opened while armed (forced entry).",
    "is_active": true
  }
  ```

## Config changes

Edit `firmware/config.h`:

- `WIFI_SSID`, `WIFI_PASS`
- `BASE_URL` (must end with `/`)
- `DEVICE_ID`
- `RELAY_ACTIVE_LOW` if your relay module is opposite
- (Optional) `DEVICE_KEY` if you enable auth in your PHP
