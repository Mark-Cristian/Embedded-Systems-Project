#pragma once
/**
 * GateX Smart Lock (ESP32) - Firmware
 *
 * PROJECT CONSTRAINTS (per your notes):
 *  - OLED has been removed: firmware MUST NOT include/use Wire/SSD1306.
 *  - All user-visible status is via: Web dashboard + Serial logs + LEDs + buzzer.
 *  - Arduino framework (ESP32 DevKit / PlatformIO).
 *  - HTTPClient + ArduinoJson.
 *  - Must be non-blocking (no while loops waiting for WiFi; no long delay()).
 *  - Must keep working offline:
 *      - Access still works locally (PIN + fingerprint)
 *      - Events are queued in RAM if server unreachable
 *      - When connection returns, flush queue + pending status
 *
 * IMPORTANT SETUP:
 *  - Edit WIFI_SSID / WIFI_PASS
 *  - Edit BASE_URL to match your LAN server + project folder.
 *    Example: http://192.168.1.3/<PROJECT_FOLDER>/api/
 */

#include <Arduino.h>
#include <functional>
#include <vector>

#define GATEX_FW_VERSION "1.1.0"

namespace GateXConfig {
  // --------------------
  // Serial debug
  // --------------------
  static const bool SERIAL_DEBUG = true;
  constexpr uint32_t SERIAL_HEARTBEAT_MS = 5000;

  // --------------------
  // Network / API
  // --------------------
  // NOTE: Must end with a trailing slash "/"
  static const char* WIFI_SSID = "Canetewifi";
  static const char* WIFI_PASS = "C@nete1_pass";

  // Your server PC IPv4 is 192.168.1.3 (per your notes).
  // Replace "gateX_proj" with your actual project folder name.
  static const char* BASE_URL  = "http://192.168.1.3/gateX_proj/api/";

  static const int DEVICE_ID = 1;

  // Optional: if you enable device-key auth in _config.php (enable_device_key_auth = true),
  // set your per-device key here.
  static const char* DEVICE_KEY_HEADER = "X-Device-Key";
  static const char* DEVICE_KEY        = "";   // e.g. "my-secret-device-key"
  static const bool  SEND_DEVICE_KEY_IF_SET = true;

  // HTTP timeouts (keep short to avoid blocking keypad/FP too long when server is down)
  constexpr uint16_t HTTP_CONNECT_TIMEOUT_MS = 1200;
  constexpr uint16_t HTTP_TIMEOUT_MS         = 2200;

  // Endpoints (MUST match your PHP file names exactly)
  static const char* EP_GET_SETTINGS          = "getSettings.php";
  static const char* EP_VERIFY_CURRENT_PIN    = "verifyCurrentPin.php";
  static const char* EP_POST_ACCESS_ATTEMPT   = "postAccessAttempt.php";
  static const char* EP_POST_STATUS           = "postStatus.php";
  static const char* EP_POST_ALERT            = "postAlert.php";
  static const char* EP_GET_FINGERPRINTS      = "getFingerprints.php";
  static const char* EP_ADD_FINGERPRINT       = "addFingerprint.php";
  static const char* EP_REMOVE_FINGERPRINT    = "removeFingerprint.php";

  // --------------------
  // Hardware pins (FROZEN)
  // --------------------
  // AS608 Fingerprint (UART2)
  constexpr uint8_t PIN_FP_RX = 16; // ESP32 RX2 <- sensor TX
  constexpr uint8_t PIN_FP_TX = 17; // ESP32 TX2 -> sensor RX

  // Reed switch (door sensor)
  constexpr uint8_t PIN_REED = 32; // INPUT_PULLUP, CLOSED = LOW

  // Keypad 4x4 matrix
  constexpr uint8_t PIN_KP_R1 = 25;
  constexpr uint8_t PIN_KP_R2 = 26;
  constexpr uint8_t PIN_KP_R3 = 27;
  constexpr uint8_t PIN_KP_R4 = 14;

  // Columns are input-only pins with EXTERNAL 10k pullups to 3.3V
  constexpr uint8_t PIN_KP_C1 = 34;
  constexpr uint8_t PIN_KP_C2 = 35;
  constexpr uint8_t PIN_KP_C3 = 36;
  constexpr uint8_t PIN_KP_C4 = 39;

  // LEDs
  constexpr uint8_t PIN_LED_GREEN = 13;
  constexpr uint8_t PIN_LED_RED   = 19;
  constexpr uint8_t PIN_LED_A     = 33;

  // Relay control (solenoid)
  constexpr uint8_t PIN_RELAY_SOL = 23;

  // Alarm buzzer (3.3V)
  constexpr uint8_t PIN_BUZZER = 18;

  // OLED I2C (SSD1306 128x64)
  constexpr uint8_t PIN_OLED_SDA = 21;
  constexpr uint8_t PIN_OLED_SCL = 22;

  // OLED behavior
  #define OLED_ENABLED 1
  #define OLED_SHOW_PLAINTEXT_PIN 1   // 1 = show digits as typed, 0 = show ****

  // --------------------
  // Logic config
  // --------------------
  // Relay active level
  static const bool RELAY_ACTIVE_LOW = true; // default per your relay plan; configurable

  // Solenoid behavior
  constexpr uint32_t UNLOCK_PULSE_MS    = 1000;   // <= 1s max
  constexpr uint32_t UNLOCK_COOLDOWN_MS = 10000;  // >= 10s

  // Relay click test pulse (for bench testing without solenoid connected)
  constexpr uint32_t RELAY_CLICK_TEST_MS = 60;

  // Settings + status intervals
  constexpr uint32_t SETTINGS_POLL_MS     = 15000; // poll settings
  constexpr uint32_t STATUS_INTERVAL_MS   = 8000;  // periodic status post (plus on-change)
  constexpr uint32_t FP_SYNC_MS           = 20000; // poll fingerprints mapping

  // Event queue / retry
  constexpr uint32_t API_RETRY_BACKOFF_MS = 3000;
  constexpr size_t   EVENT_QUEUE_MAX      = 30;    // RAM queue for access+alerts+fp finalize

  // Door monitoring
  constexpr uint32_t DOOR_DEBOUNCE_MS      = 35;
  constexpr uint32_t DOOR_HELD_OPEN_MS     = 15000;
  constexpr uint32_t ALARM_BLINK_MS        = 350;

  // UI timing
  constexpr uint32_t UI_TRANSIENT_MS = 1500;  // granted/denied hold time
  constexpr uint32_t ACCESS_GRACE_MS = 8000;  // door open grace after grant

  // Keypad behavior
  constexpr uint8_t  PIN_MIN_LEN = 4;
  constexpr uint8_t  PIN_MAX_LEN = 12;
  constexpr uint32_t KEYPAD_DEBOUNCE_MS = 40;
  constexpr uint32_t PIN_ENTRY_TIMEOUT_MS = 10000;

  // PIN offline behavior
  // Backend uses PHP password_hash()/password_verify() (bcrypt). ESP32 cannot practically verify bcrypt
  // without adding a heavy bcrypt implementation.
  // Strategy used here:
  //   - Online: verify PIN via verifyCurrentPin.php
  //   - Offline: if a PIN was verified successfully while online before, cache that plaintext PIN in NVS
  //              and compare locally.
  // If you don't want plaintext caching, set this to false (offline PIN will not work unless you change
  // backend to a hash scheme ESP32 can verify).
  static const bool CACHE_PLAINTEXT_PIN_IN_NVS = true;

  // Optional "bootstrap" PIN you can compile into firmware for initial bring-up ONLY.
  // Leave empty to disable.
  static const char* BOOTSTRAP_PIN = ""; // e.g. "0000" (disabled by default)

  // Fingerprint
  constexpr uint32_t FP_BAUD = 57600;
  constexpr uint32_t FP_POLL_MS = 200;
  constexpr uint32_t FP_COOLDOWN_MS = 1500;
  constexpr uint32_t FP_ENROLL_TIMEOUT_MS = 60000;

  // Buzzer (PWM tone)
  constexpr uint8_t  BUZZER_LEDC_CH = 0;
  constexpr uint32_t BUZZER_TONE_HZ = 2300;
  constexpr uint8_t  BUZZER_DUTY_ON = 128; // 50% duty for 8-bit
}

// --------------------
// Shared data types
// --------------------
struct DeviceSettings {
  int deviceId = GateXConfig::DEVICE_ID;
  int lockoutSeconds = 60;
  bool alarmEnabled = false;
  int maxFailedAttempts = 5;
  int settingsVersion = 0;
  bool pinIsDefault = false;
  String updatedAt = "";
  String pinHash = ""; // may be empty if include_pin_hash is not allowed
};

struct DeviceState {
  String lockState = "locked";     // locked | unlocked
  String doorState = "closed";     // open | closed
  String systemState = "disarmed"; // armed | disarmed
  int wifiRssi = -127;             // dBm
  int batteryPercent = -1;         // -1 unknown
};

struct AccessAttempt {
  String eventUuid;
  String method;           // fingerprint | keypad | remote | system
  String result;           // granted | denied
  String userCode;         // optional
  String usernameSnapshot; // optional
  String reason;           // optional
  String eventAtIso;       // optional ISO8601
};

struct AlertEvent {
  String eventUuid;
  String type;     // forced_entry | lockout | multiple_failed_attempts | tamper | door_held_open | system
  String severity; // info | warning | critical
  String message;
  bool isActive = false;
  String createdAtIso;
  String clearedAtIso;
};

struct FingerprintRecord {
  String userCode;
  String displayName;
  int sensorSlot = -1;
  String enrolledAtIso; // "" if null
};

// --------------------
// Public module APIs
// --------------------
namespace WifiManager {
  void begin();
  void update();
  bool connected();
  String ip();
  int rssi();
}

namespace ApiClient {
  void begin();
  void update();

  bool fetchSettings(DeviceSettings& out, bool includePinHash = true);

  // Online PIN verification (verifyCurrentPin.php)
  bool verifyCurrentPin(const String& pin, String& errorCodeOut);

  // Fingerprint mapping
  bool fetchFingerprints(std::vector<FingerprintRecord>& out);
  bool finalizeFingerprintEnrollment(const FingerprintRecord& rec, String& errorOut);

  // Status
  bool postStatus(const DeviceState& st, const String& eventUuid);
  void setPendingStatus(const DeviceState& st);

  // Logs / alerts
  bool postAccessAttempt(const AccessAttempt& a);
  bool postAlert(const AlertEvent& a);

  bool hasBacklog();
  String lastError();
}

namespace Sensors {
  void begin();
  void update();
  bool doorClosed();
  bool doorJustOpened();
  bool doorJustClosed();
}

namespace LockControl {
  void begin();
  void update();

  bool canUnlock();
  bool requestUnlock();

  // Bench test helpers
  bool relayClickTest(uint32_t pulseMs = GateXConfig::RELAY_CLICK_TEST_MS);

  bool isUnlocking();
  bool inCooldown();
  String lockState(); // locked/unlocked
}

namespace Keypad {
  using KeyCallback = std::function<void(char key)>;
  using PinCallback = std::function<void(const String& pin)>;

  void begin(KeyCallback onKey, PinCallback onPinComplete);
  void update();
  void setLockedOut(bool lockedOut, uint32_t secondsRemaining);
  void resetEntry();
  String maskedEntry();
  String rawEntry();
}

namespace Fingerprint {
  using ScanCallback = std::function<void(int templateId, int confidence)>;
  using EnrollEventCallback = std::function<void(const char* evt, int slot)>;

  void begin(ScanCallback cb);
  void setEnrollEventCallback(EnrollEventCallback cb);
  void update();

  bool ready();

  // Enrollment (non-blocking state machine)
  bool startEnrollment(int slot);
  bool enrolling();
  bool enrollmentDone();
  bool enrollmentSuccess();
  String enrollmentError();
  int enrollmentSlot();
  void cancelEnrollment();

  // Template utilities
  bool templateExists(int slot);
  bool deleteTemplate(int slot);
}

namespace Utils {
  // Time (NTP)
  void timeSyncBegin();
  bool timeValid();
  String iso8601UtcNow();

  // IDs & hashing
  String uuidV4();
  bool isHexString(const String& s);
  String sha256Hex(const String& s);

  // LEDs
  void ledsBegin();
  void ledsAllOff();
  void ledsReady(bool armed);
  void ledsGranted();
  void ledsDenied();
  void ledsAlarm(bool phaseOn);

  // Buzzer (non-blocking patterns)
  void buzzerBegin();
  void buzzerUpdate();
  void buzzerKey();
  void buzzerGrant();
  void buzzerDeny();
  void buzzerStartContinuous();
  void buzzerStop();
}
