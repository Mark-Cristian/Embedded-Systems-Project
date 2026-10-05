#include "config.h"

#include <Preferences.h>
#include <ArduinoJson.h>
#include <algorithm>

#if OLED_ENABLED
  #include <Wire.h>
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>
  static Adafruit_SSD1306 display(128, 64, &Wire, -1);
#endif

#if OLED_ENABLED
static void oledRenderMain(int uiMode, bool wifiOk, const String& ip, bool doorClosed, bool armed, const String& pinText);
#endif

// ------------------------------------------------------------
// NVS keys
// ------------------------------------------------------------

// ------------------------------------------------------------
// NVS keys
// ------------------------------------------------------------
namespace {
  const char* NVS_NS = "gatex";
  const char* NVS_KEY_SETTINGS_VER = "set_ver";
  const char* NVS_KEY_LOCKOUT_S = "lockout_s";
  const char* NVS_KEY_ALARM_EN = "alarm_en";
  const char* NVS_KEY_MAX_FAIL = "max_fail";
  const char* NVS_KEY_PIN_IS_DEF = "pin_def";
  const char* NVS_KEY_PIN_HASH = "pin_hash";
  const char* NVS_KEY_PIN_PLAIN = "pin_plain";
  const char* NVS_KEY_FP_MAP = "fp_map";
}

// ------------------------------------------------------------
// Globals
// ------------------------------------------------------------
namespace {
  Preferences prefs;

  DeviceSettings settings;
  bool haveSettings = false;

  // Cached plaintext PIN for offline (learned via successful online verify)
  String cachedPinPlain = "";
  bool cachedPinValid = false;

  // Fingerprint mapping cache (slot -> user)
  std::vector<FingerprintRecord> fpCache;
  bool fpCacheLoaded = false;

  DeviceState state;
  DeviceState lastSentState;

  unsigned long lastStatusMs = 0;
  unsigned long lastSettingsPollMs = 0;
  unsigned long lastFpSyncMs = 0;
  unsigned long lastHeartbeatMs = 0;

  // Access / lockout
  int failedAttempts = 0;
  unsigned long lockoutUntilMs = 0;
  unsigned long lastGrantMs = 0;

  // Door alarm
  bool forcedEntryActive = false;
  String forcedAlertUuid = "";
  String forcedCreatedAtIso = "";
  unsigned long doorOpenedMs = 0;
  bool doorHeldAlertSent = false;

  // Buzzer state
  bool lockoutBuzzerActive = false;

  // UI
  enum class UiMode : uint8_t { Boot, Wifi, Ready, Granted, Denied, Lockout, Alarm, Enroll };
  UiMode uiMode = UiMode::Boot;
  unsigned long uiUntilMs = 0;
  unsigned long lastBlinkMs = 0;
  bool blinkPhase = false;

  // Mirror of currently typed keypad digits for OLED display
  String oledPinEntry = "";

  #if OLED_ENABLED
    unsigned long lastOledRefreshMs = 0;   // <-- ADD
  #endif

  #if OLED_ENABLED
    static void oledRefresh() {
      // Choose PIN text based on config
      // Show the digits being typed on the keypad (updated in onKeypadKey)
      String pinText = oledPinEntry;


      oledRenderMain(
        (int)uiMode,
        WifiManager::connected(),
        WifiManager::ip(),
        Sensors::doorClosed(),
        settings.alarmEnabled,
        pinText
      );
    }
  #endif

  // WiFi edge detection
  bool wasWifiConnected = false;
  bool timeSyncRequested = false;

  // Keypad command detection ("*#" within window)
  bool starArmed = false;
  unsigned long starAtMs = 0;

  // Enrollment state
  bool enrollmentRequested = false;
  bool enrollmentInProgress = false;
  FingerprintRecord enrollTarget;

  // API retry throttles for polling calls (avoid hammering while server down)
  unsigned long nextSettingsAttemptMs = 0;
  unsigned long nextFpSyncAttemptMs = 0;

  const char* uiName(UiMode m) {
    switch (m) {
      case UiMode::Boot: return "BOOT";
      case UiMode::Wifi: return "WIFI";
      case UiMode::Ready: return "READY";
      case UiMode::Granted: return "GRANTED";
      case UiMode::Denied: return "DENIED";
      case UiMode::Lockout: return "LOCKOUT";
      case UiMode::Alarm: return "ALARM";
      case UiMode::Enroll: return "ENROLL";
      default: return "?";
    }
  }

  void setUi(UiMode m, unsigned long holdMs = 0) {
    if (uiMode != m) {
      uiMode = m;
      if (GateXConfig::SERIAL_DEBUG) {
        Serial.printf("[UI] -> %s\n", uiName(m));
      }
    }
    uiUntilMs = (holdMs > 0) ? (millis() + holdMs) : 0;

    #if OLED_ENABLED
      oledRefresh();   // <-- REPLACE WITH THIS
    #endif
  }

  bool isLockedOut() {
    return (lockoutUntilMs != 0) && (millis() < lockoutUntilMs);
  }

  // ------------------------------------------------------------
  // NVS helpers
  // ------------------------------------------------------------
  void loadFromNvs() {
    prefs.begin(NVS_NS, true);

    settings.settingsVersion = prefs.getInt(NVS_KEY_SETTINGS_VER, settings.settingsVersion);
    settings.lockoutSeconds = prefs.getInt(NVS_KEY_LOCKOUT_S, settings.lockoutSeconds);
    settings.alarmEnabled = prefs.getBool(NVS_KEY_ALARM_EN, settings.alarmEnabled);
    settings.maxFailedAttempts = prefs.getInt(NVS_KEY_MAX_FAIL, settings.maxFailedAttempts);
    settings.pinIsDefault = prefs.getBool(NVS_KEY_PIN_IS_DEF, settings.pinIsDefault);
    settings.pinHash = prefs.getString(NVS_KEY_PIN_HASH, settings.pinHash);

    if (GateXConfig::CACHE_PLAINTEXT_PIN_IN_NVS) {
      cachedPinPlain = prefs.getString(NVS_KEY_PIN_PLAIN, "");
      cachedPinValid = (cachedPinPlain.length() >= GateXConfig::PIN_MIN_LEN);
    }

    // Fingerprint mapping cache
    String fpJson = prefs.getString(NVS_KEY_FP_MAP, "");
    prefs.end();

    if (fpJson.length() > 0) {
      DynamicJsonDocument doc(8192);
      if (!deserializeJson(doc, fpJson)) {
        fpCache.clear();
        JsonArray items = doc["items"].as<JsonArray>();
        for (JsonObject it : items) {
          FingerprintRecord r;
          r.sensorSlot = it["slot"] | -1;
          r.userCode = (const char*)(it["user_code"] | "");
          r.displayName = (const char*)(it["display_name"] | "");
          r.enrolledAtIso = (const char*)(it["enrolled_at"] | "");
          if (r.sensorSlot >= 0 && r.userCode.length() > 0) {
            fpCache.push_back(r);
          }
        }
        fpCacheLoaded = true;
      }
    }

    haveSettings = true;

    if (GateXConfig::SERIAL_DEBUG) {
      Serial.printf("[NVS] loaded settings ver=%d lockout=%ds alarm=%d maxFail=%d pinDefault=%d pinHashLen=%d cachedPin=%d fpCache=%d\n",
                    settings.settingsVersion,
                    settings.lockoutSeconds,
                    settings.alarmEnabled ? 1 : 0,
                    settings.maxFailedAttempts,
                    settings.pinIsDefault ? 1 : 0,
                    settings.pinHash.length(),
                    cachedPinValid ? 1 : 0,
                    (int)fpCache.size());
    }
  }

  void saveSettingsToNvs() {
    prefs.begin(NVS_NS, false);
    prefs.putInt(NVS_KEY_SETTINGS_VER, settings.settingsVersion);
    prefs.putInt(NVS_KEY_LOCKOUT_S, settings.lockoutSeconds);
    prefs.putBool(NVS_KEY_ALARM_EN, settings.alarmEnabled);
    prefs.putInt(NVS_KEY_MAX_FAIL, settings.maxFailedAttempts);
    prefs.putBool(NVS_KEY_PIN_IS_DEF, settings.pinIsDefault);
    prefs.putString(NVS_KEY_PIN_HASH, settings.pinHash);
    if (GateXConfig::CACHE_PLAINTEXT_PIN_IN_NVS) {
      prefs.putString(NVS_KEY_PIN_PLAIN, cachedPinPlain);
    }
    prefs.end();
  }

  void saveFpCacheToNvs() {
    DynamicJsonDocument doc(8192);
    JsonArray items = doc.createNestedArray("items");
    for (const auto& r : fpCache) {
      JsonObject it = items.createNestedObject();
      it["slot"] = r.sensorSlot;
      it["user_code"] = r.userCode;
      it["display_name"] = r.displayName;
      it["enrolled_at"] = r.enrolledAtIso;
    }

    String out;
    serializeJson(doc, out);

    prefs.begin(NVS_NS, false);
    prefs.putString(NVS_KEY_FP_MAP, out);
    prefs.end();
  }

  const FingerprintRecord* findFpBySlot(int slot) {
    for (const auto& r : fpCache) {
      if (r.sensorSlot == slot) return &r;
    }
    return nullptr;
  }

  // ------------------------------------------------------------
  // Server sync (settings + fingerprint mapping)
  // ------------------------------------------------------------
  void pollSettingsIfDue() {
    const unsigned long now = millis();
    if (!WifiManager::connected()) return;
    if (now < nextSettingsAttemptMs) return;
    if (now - lastSettingsPollMs < GateXConfig::SETTINGS_POLL_MS) return;

    lastSettingsPollMs = now;

    DeviceSettings fresh = settings;

    bool ok = ApiClient::fetchSettings(fresh, true);
    if (!ok) {
      // If pin_hash is restricted, still fetch non-sensitive settings
      ok = ApiClient::fetchSettings(fresh, false);
      fresh.pinHash = settings.pinHash;
    }

    if (!ok) {
      // small backoff for polling calls if server is down
      nextSettingsAttemptMs = now + 5000;
      return;
    }

    nextSettingsAttemptMs = 0;

    bool changed = false;
    if (fresh.settingsVersion != settings.settingsVersion) changed = true;
    if (fresh.lockoutSeconds != settings.lockoutSeconds) changed = true;
    if (fresh.alarmEnabled != settings.alarmEnabled) changed = true;
    if (fresh.maxFailedAttempts != settings.maxFailedAttempts) changed = true;
    if (fresh.pinIsDefault != settings.pinIsDefault) changed = true;
    if (fresh.pinHash.length() > 0 && fresh.pinHash != settings.pinHash) changed = true;

    bool prevAlarm = settings.alarmEnabled;
    settings = fresh;
    haveSettings = true;

    if (changed) {
      saveSettingsToNvs();
      if (GateXConfig::SERIAL_DEBUG) {
        Serial.printf("[SET] updated ver=%d lockout=%ds alarm=%d maxFail=%d pinDefault=%d pinHashLen=%d\n",
                      settings.settingsVersion,
                      settings.lockoutSeconds,
                      settings.alarmEnabled ? 1 : 0,
                      settings.maxFailedAttempts,
                      settings.pinIsDefault ? 1 : 0,
                      settings.pinHash.length());
      }
    }

    // If dashboard disarms while alarm active, stop alarm
    if (prevAlarm && !settings.alarmEnabled && forcedEntryActive) {
      forcedEntryActive = false;
      Utils::buzzerStop();
      setUi(UiMode::Ready);
    }
  }

  void syncFingerprintsIfDue(bool forceNow = false) {
    const unsigned long now = millis();
    if (!WifiManager::connected()) return;
    if (!Fingerprint::ready()) return;
    if (!forceNow && (now - lastFpSyncMs < GateXConfig::FP_SYNC_MS)) return;
    if (now < nextFpSyncAttemptMs) return;

    lastFpSyncMs = now;

    std::vector<FingerprintRecord> fresh;
    if (!ApiClient::fetchFingerprints(fresh)) {
      nextFpSyncAttemptMs = now + 5000;
      return;
    }

    nextFpSyncAttemptMs = 0;

    // Detect deletions (server list is authoritative)
    for (const auto& oldRec : fpCache) {
      bool stillThere = false;
      for (const auto& newRec : fresh) {
        if (newRec.sensorSlot == oldRec.sensorSlot) {
          stillThere = true;
          break;
        }
      }
      if (!stillThere) {
        if (GateXConfig::SERIAL_DEBUG) {
          Serial.printf("[FP] slot %d removed on server -> deleting template\n", oldRec.sensorSlot);
        }
        Fingerprint::deleteTemplate(oldRec.sensorSlot);
      }
    }

    fpCache = fresh;
    fpCacheLoaded = true;
    saveFpCacheToNvs();

    if (GateXConfig::SERIAL_DEBUG) {
      Serial.printf("[FP] synced %d fingerprint mappings\n", (int)fpCache.size());
    }
  }

  // ------------------------------------------------------------
  // Alerts + logs
  // ------------------------------------------------------------
  void sendAccessAttempt(const String& method,
                         const String& result,
                         const String& reason,
                         const String& userCode,
                         const String& usernameSnapshot) {
    AccessAttempt a;
    a.eventUuid = Utils::uuidV4();
    a.method = method;
    a.result = result;
    a.reason = reason;
    a.userCode = userCode;
    a.usernameSnapshot = usernameSnapshot;
    a.eventAtIso = Utils::iso8601UtcNow();
    ApiClient::postAccessAttempt(a);
  }

  void sendAlert(const String& type,
                 const String& severity,
                 const String& message,
                 bool isActive,
                 const String& eventUuid,
                 const String& createdAtIso,
                 const String& clearedAtIso) {
    AlertEvent al;
    al.eventUuid = eventUuid.length() ? eventUuid : Utils::uuidV4();
    al.type = type;
    al.severity = severity;
    al.message = message;
    al.isActive = isActive;
    al.createdAtIso = createdAtIso;
    al.clearedAtIso = clearedAtIso;
    ApiClient::postAlert(al);
  }

  void startForcedEntryAlert() {
    if (forcedEntryActive) return;
    forcedEntryActive = true;

    forcedAlertUuid = Utils::uuidV4();
    forcedCreatedAtIso = Utils::iso8601UtcNow();

    sendAlert("forced_entry", "critical", "Forced entry detected: door opened while armed.", true,
              forcedAlertUuid, forcedCreatedAtIso, "");
  }

  void clearForcedEntryAlert() {
    if (!forcedEntryActive) return;
    forcedEntryActive = false;

    String clearedAt = Utils::iso8601UtcNow();
    sendAlert("forced_entry", "info", "Forced entry alarm cleared (door closed).", false,
              forcedAlertUuid, forcedCreatedAtIso, clearedAt);

    forcedAlertUuid = "";
    forcedCreatedAtIso = "";
  }

  void startLockout() {
    const unsigned long now = millis();
    oledPinEntry = "";
    lockoutUntilMs = now + (unsigned long)settings.lockoutSeconds * 1000UL;
    Keypad::setLockedOut(true, settings.lockoutSeconds);
    setUi(UiMode::Lockout);

    sendAlert("lockout", "warning", "Lockout active due to too many failed attempts.", true,
              Utils::uuidV4(), Utils::iso8601UtcNow(), "");
  }

  void endLockoutIfDone() {
    if (lockoutUntilMs != 0 && !isLockedOut()) {
      lockoutUntilMs = 0;
      failedAttempts = 0;
      Keypad::setLockedOut(false, 0);
      setUi(UiMode::Ready);

      sendAlert("lockout", "info", "Lockout cleared.", false,
                Utils::uuidV4(), Utils::iso8601UtcNow(), Utils::iso8601UtcNow());
    }
  }

  void handleDeniedAttempt(const String& method,
                           const String& reason,
                           const String& userCode,
                           const String& usernameSnapshot) {
    failedAttempts++;

    Utils::buzzerDeny();
    Utils::ledsDenied();
    setUi(UiMode::Denied, GateXConfig::UI_TRANSIENT_MS);

    sendAccessAttempt(method, "denied", reason, userCode, usernameSnapshot);

    if (settings.maxFailedAttempts > 1 && failedAttempts == (settings.maxFailedAttempts - 1)) {
      AlertEvent al;
      al.eventUuid = Utils::uuidV4();
      al.type = "multiple_failed_attempts";
      al.severity = "warning";
      al.message = "Multiple failed access attempts detected.";
      al.isActive = false;
      al.createdAtIso = Utils::iso8601UtcNow();
      ApiClient::postAlert(al);
    }

    if (settings.maxFailedAttempts > 0 && failedAttempts >= settings.maxFailedAttempts) {
      startLockout();
    }
  }

  void handleGrantedAttempt(const String& method,
                            const String& userCode,
                            const String& usernameSnapshot) {
    if (!LockControl::canUnlock()) {
      handleDeniedAttempt(method, "cooldown", userCode, usernameSnapshot);
      return;
    }

    if (!LockControl::requestUnlock()) {
      handleDeniedAttempt(method, "cooldown", userCode, usernameSnapshot);
      return;
    }

    failedAttempts = 0;
    lockoutUntilMs = 0;
    Keypad::setLockedOut(false, 0);

    lastGrantMs = millis();

    Utils::buzzerGrant();
    Utils::ledsGranted();
    setUi(UiMode::Granted, GateXConfig::UI_TRANSIENT_MS);

    sendAccessAttempt(method, "granted", "", userCode, usernameSnapshot);
  }

  // ------------------------------------------------------------
  // PIN verification
  // ------------------------------------------------------------
  bool verifyPinOffline(const String& pin, String& reasonOut) {
    reasonOut = "";

    // 1) bootstrap PIN (compile-time, optional)
    if (GateXConfig::BOOTSTRAP_PIN && strlen(GateXConfig::BOOTSTRAP_PIN) > 0) {
      if (pin == String(GateXConfig::BOOTSTRAP_PIN)) return true;
    }

    // 2) cached plaintext PIN (learned via successful online verify)
    if (cachedPinValid && pin == cachedPinPlain) return true;

    // 3) developer-friendly schemes (if backend ever switches to SHA-256 or plaintext)
    if (settings.pinHash.length() > 0) {
      // SHA-256 hex (64 chars)
      if (settings.pinHash.length() == 64 && Utils::isHexString(settings.pinHash)) {
        if (Utils::sha256Hex(pin) == settings.pinHash) return true;
      }

      // (DEV ONLY) plaintext stored in pin_hash
      if (pin == settings.pinHash) return true;
    }

    reasonOut = cachedPinValid ? "pin_incorrect" : "offline_no_cached_pin";
    return false;
  }

  // ------------------------------------------------------------
  // Fingerprint enrollment orchestration
  // ------------------------------------------------------------
  bool startNextEnrollment() {
    if (!WifiManager::connected()) {
      Serial.println("[ENROLL] WiFi offline - cannot fetch pending enrollments");
      Utils::buzzerDeny();
      return false;
    }
    if (!Fingerprint::ready()) {
      Serial.println("[ENROLL] Fingerprint sensor not ready");
      Utils::buzzerDeny();
      return false;
    }

    // Get freshest mapping
    syncFingerprintsIfDue(true);

    if (fpCache.empty()) {
      Serial.println("[ENROLL] No fingerprint records found on server");
      Utils::buzzerDeny();
      return false;
    }

    // Choose the first server record whose sensor slot is currently empty
    std::vector<FingerprintRecord> sorted = fpCache;
    std::sort(sorted.begin(), sorted.end(), [](const FingerprintRecord& a, const FingerprintRecord& b) {
      return a.sensorSlot < b.sensorSlot;
    });

    for (const auto& rec : sorted) {
      if (rec.sensorSlot < 0) continue;

      bool exists = Fingerprint::templateExists(rec.sensorSlot);
      if (!exists) {
        enrollTarget = rec;
        enrollmentInProgress = Fingerprint::startEnrollment(rec.sensorSlot);
        if (!enrollmentInProgress) {
          Serial.println("[ENROLL] Failed to start enrollment state machine");
          Utils::buzzerDeny();
          return false;
        }

        Serial.printf("[ENROLL] Starting: user_code=%s name=%s slot=%d\n",
                      rec.userCode.c_str(), rec.displayName.c_str(), rec.sensorSlot);

        setUi(UiMode::Enroll);
        return true;
      }
    }

    Serial.println("[ENROLL] No pending enrollment found (all DB slots exist on sensor)");
    Utils::buzzerDeny();
    return false;
  }

  // ------------------------------------------------------------
  // Callbacks
  // ------------------------------------------------------------
  void onKeypadKey(char k) {
    Utils::buzzerKey();

    const unsigned long now = millis();

    // Detect "*#" within 1.5s to trigger enrollment
    if (k == '*') {
      starArmed = true;
      starAtMs = now;
      // Clear OLED PIN mirror on '*' (commonly used as clear/cancel)
      oledPinEntry = "";
      #if OLED_ENABLED
        if (uiMode == UiMode::Ready) oledRefresh();
      #endif

      return;
    }
    if (k == '#') {
      if (starArmed && (now - starAtMs) <= 1500) {
        starArmed = false;



        if (isLockedOut()) {
          Serial.println("[CMD] *# ignored (lockout)");
          Utils::buzzerDeny();
          return;
        }

        Serial.println("[CMD] *# -> enrollment requested");
        enrollmentRequested = true;

        // Enrollment command consumes keypad entry; clear OLED mirror
        oledPinEntry = "";
        #if OLED_ENABLED
          if (uiMode == UiMode::Ready) oledRefresh();
        #endif

        return;
      }
    }

    // Cancel the command if any other key pressed
    starArmed = false;

    // Mirror keypad digit entry to OLED so the user can see what they're typing
    if (k >= '0' && k <= '9') {
      oledPinEntry += k;
      #if OLED_ENABLED
        if (uiMode == UiMode::Ready) oledRefresh();
      #endif
    }


    
  }

  void onPinComplete(const String& pin) {
    // PIN submitted -> clear OLED mirror immediately
    oledPinEntry = "";
    #if OLED_ENABLED
      oledRefresh();
    #endif

    if (isLockedOut()) {
      handleDeniedAttempt("keypad", "lockout", "", "Keypad");
      return;
    }

    if (WifiManager::connected()) {
      String errCode;
      bool ok = ApiClient::verifyCurrentPin(pin, errCode);
      if (ok) {
        // Learn PIN for offline use
        if (GateXConfig::CACHE_PLAINTEXT_PIN_IN_NVS) {
          cachedPinPlain = pin;
          cachedPinValid = true;
          saveSettingsToNvs();
        }
        handleGrantedAttempt("keypad", "", "Keypad");
      } else {
        String reason = errCode.length() ? errCode : "pin_denied";
        handleDeniedAttempt("keypad", reason, "", "Keypad");
      }
      return;
    }

    // Offline
    String reason;
    bool ok = verifyPinOffline(pin, reason);
    if (ok) {
      handleGrantedAttempt("keypad", "", "Keypad");
    } else {
      handleDeniedAttempt("keypad", reason, "", "Keypad");
    }
  }

  void onFingerprintScan(int templateId, int confidence) {
    (void)confidence;

    if (isLockedOut()) {
      handleDeniedAttempt("fingerprint", "lockout", "", "Fingerprint");
      return;
    }

    if (templateId >= 0) {
      const FingerprintRecord* rec = findFpBySlot(templateId);
      String userCode = rec ? rec->userCode : "";
      String name = rec ? rec->displayName : (String("Fingerprint #") + String(templateId));
      handleGrantedAttempt("fingerprint", userCode, name);
    } else {
      handleDeniedAttempt("fingerprint", "no_match", "", "Fingerprint");
    }
  }

  void onEnrollEvent(const char* evt, int slot) {
    // Keep it simple + reliable: serial logs + small beeps
    if (GateXConfig::SERIAL_DEBUG) {
      Serial.printf("[ENROLL] evt=%s slot=%d\n", evt, slot);
    }

    if (strcmp(evt, "place_finger_1") == 0 || strcmp(evt, "place_finger_2") == 0) {
      Utils::buzzerKey();
      return;
    }
    if (strcmp(evt, "remove_finger") == 0) {
      Utils::buzzerKey();
      return;
    }
    if (strcmp(evt, "success") == 0) {
      Utils::buzzerGrant();
      return;
    }
    if (strcmp(evt, "fail") == 0) {
      Utils::buzzerDeny();
      return;
    }
  }
}

// OLED helper functions
#if OLED_ENABLED
static uint8_t detectOledAddress() {
  // Common I2C addresses: 0x3C, 0x3D
  for (uint8_t a : { (uint8_t)0x3C, (uint8_t)0x3D }) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) return a;
  }
  return 0x3C; // fallback
}

static void oledInit() {
  Wire.begin(GateXConfig::PIN_OLED_SDA, GateXConfig::PIN_OLED_SCL);

  uint8_t addr = detectOledAddress();
  if (!display.begin(SSD1306_SWITCHCAPVCC, addr)) {
    Serial.println("[OLED] init failed");
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("GateX Boot");
  display.println("Starting...");
  display.display();
}

static void oledRenderWifi(bool wifiOk, const String& ip) {
  display.println("WiFi");
  if (wifiOk) {
    display.println("Connected");
    display.print("IP: ");
    display.println(ip);
  } else {
    display.println("Connecting...");
  }
}

static void oledRenderReady(bool doorClosed, bool armed, const String& pinText, bool wifiOk) {
  display.println("READY");
  display.print("Door: ");
  display.println(doorClosed ? "CLOSED" : "OPEN");

  display.print("Mode: ");
  display.println(armed ? "ARMED" : "DISARMED");

  display.print("PIN: ");
  display.println(pinText.length() ? pinText : "____");

  if (!wifiOk) display.println("OFFLINE");
}

static void oledRenderStatus(const char* title, const char* subtitle=nullptr) {
  display.println(title);
  if (subtitle && subtitle[0]) display.println(subtitle);
}

static void oledRenderMain(
  int uiMode,                 // your UiMode enum value
  bool wifiOk,
  const String& ip,
  bool doorClosed,
  bool armed,
  const String& pinText
) {
  display.clearDisplay();
  display.setCursor(0, 0);

  // Replace these cases to match your UiMode enum names:
  switch (uiMode) {
    case 0: // Boot
      oledRenderStatus("GateX Boot");
      break;

    case 1: // Wifi
      oledRenderWifi(wifiOk, ip);
      break;

    case 2: // Ready
      oledRenderReady(doorClosed, armed, pinText, wifiOk);
      break;

    case 3: // Granted
      oledRenderStatus("ACCESS GRANTED");
      break;

    case 4: // Denied
      oledRenderStatus("ACCESS DENIED");
      break;

    case 5: // Lockout
      oledRenderStatus("LOCKOUT");
      break;

    case 6: // Alarm
      oledRenderStatus("!!! ALARM !!!", "FORCED ENTRY");
      break;
  }

  display.display();
}
#endif

// ------------------------------------------------------------
// Setup / Loop
// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(30);

  Serial.printf("\nGateX FW %s booting...\n", GATEX_FW_VERSION);

  Utils::ledsBegin();
  Utils::buzzerBegin();

  WifiManager::begin();
  ApiClient::begin();

  Sensors::begin();
  LockControl::begin();

  Keypad::begin(onKeypadKey, onPinComplete);
  Fingerprint::begin(onFingerprintScan);
  Fingerprint::setEnrollEventCallback(onEnrollEvent);

  #if OLED_ENABLED
    oledInit();
  #endif

  loadFromNvs();

  // initial state
  state.lockState = LockControl::lockState();
  state.doorState = Sensors::doorClosed() ? "closed" : "open";
  state.systemState = settings.alarmEnabled ? "armed" : "disarmed";
  state.wifiRssi = WifiManager::rssi();

  lastSentState = state;

  setUi(UiMode::Wifi);

  if (settings.pinIsDefault) {
    Serial.println("[PIN] Server reports PIN is DEFAULT. If you don't know it, reset it in DB or add a reset endpoint.");
  }

  if (GateXConfig::CACHE_PLAINTEXT_PIN_IN_NVS && !cachedPinValid) {
    Serial.println("[PIN] No cached PIN yet. Offline keypad unlock will be unavailable until a PIN is verified online once.");
  }

  if (!Fingerprint::ready()) {
    Serial.println("[FP] WARNING: fingerprint sensor not detected. Check RX/TX pins and 3.3V power.");
  }
}

void loop() {
  const unsigned long now = millis();

  // Heartbeat
  if (GateXConfig::SERIAL_DEBUG && (now - lastHeartbeatMs) >= GateXConfig::SERIAL_HEARTBEAT_MS) {
    lastHeartbeatMs = now;
    Serial.printf("[HB] WiFi=%s IP=%s RSSI=%d UI=%s backlog=%d\n",
                  WifiManager::connected() ? "OK" : "NO",
                  WifiManager::ip().c_str(),
                  WifiManager::rssi(),
                  uiName(uiMode),
                  ApiClient::hasBacklog() ? 1 : 0);
  }

  WifiManager::update();

  const bool wifiOk = WifiManager::connected();
  if (wifiOk && !wasWifiConnected) {
    wasWifiConnected = true;
    Serial.printf("[WIFI] Connected. IP=%s RSSI=%d\n", WifiManager::ip().c_str(), WifiManager::rssi());

    setUi(UiMode::Ready);

    if (!timeSyncRequested) {
      Utils::timeSyncBegin();
      timeSyncRequested = true;
    }

    // Immediately sync settings + fingerprints on connect
    lastSettingsPollMs = 0;
    lastFpSyncMs = 0;
  }
  if (!wifiOk && wasWifiConnected) {
    wasWifiConnected = false;
    Serial.println("[WIFI] Disconnected");
    setUi(UiMode::Wifi);
  }

  // Non-blocking peripheral updates
  Sensors::update();
  LockControl::update();
  Keypad::update();
  Fingerprint::update();
  Utils::buzzerUpdate();

  #if OLED_ENABLED
    // Refresh OLED while in READY so typed PIN updates on-screen
    if (uiMode == UiMode::Ready) {
      if (now - lastOledRefreshMs >= 120) {   // ~8x per second
        lastOledRefreshMs = now;
        oledRefresh();
      }
    }
  #endif

  // Poll server settings and fingerprint list (when online)
  pollSettingsIfDue();
  syncFingerprintsIfDue();

  // Enrollment requested via keypad command
  if (enrollmentRequested && !enrollmentInProgress) {
    enrollmentRequested = false;
    enrollmentInProgress = startNextEnrollment();
  }

  // Enrollment completion
  if (enrollmentInProgress && Fingerprint::enrollmentDone()) {
    bool ok = Fingerprint::enrollmentSuccess();
    String err = Fingerprint::enrollmentError();
    int slot = Fingerprint::enrollmentSlot();

    Fingerprint::cancelEnrollment();
    enrollmentInProgress = false;

    if (ok) {
      Serial.printf("[ENROLL] SUCCESS slot=%d -> finalizing on server\n", slot);

      // Update server record (sets enrolled_at)
      String apiErr;
      bool apiOk = ApiClient::finalizeFingerprintEnrollment(enrollTarget, apiErr);
      if (!apiOk) {
        Serial.printf("[ENROLL] Server finalize not confirmed yet (%s). It is queued and will retry.\n", apiErr.c_str());
      }

      // Refresh cache so the web reflects enrolled_at ASAP
      syncFingerprintsIfDue(true);

      setUi(UiMode::Ready);
    } else {
      Serial.printf("[ENROLL] FAIL slot=%d err=%s\n", slot, err.c_str());
      setUi(UiMode::Ready);
    }
  }

  // Door transitions (forced entry alarm)
  if (Sensors::doorJustOpened()) {
    doorOpenedMs = now;
    doorHeldAlertSent = false;

    const bool armed = settings.alarmEnabled;
    const bool withinGrace = (now - lastGrantMs) <= GateXConfig::ACCESS_GRACE_MS;

    if (armed && !withinGrace) {
      startForcedEntryAlert();
      setUi(UiMode::Alarm);
      Utils::buzzerStartContinuous();
    }
  }

  if (Sensors::doorJustClosed()) {
    doorHeldAlertSent = false;

    if (forcedEntryActive) {
      clearForcedEntryAlert();
      Utils::buzzerStop();
      setUi(UiMode::Ready);
    }
  }

  // Door held open alert
  if (settings.alarmEnabled && !Sensors::doorClosed()) {
    if (!doorHeldAlertSent && (now - doorOpenedMs) >= GateXConfig::DOOR_HELD_OPEN_MS) {
      doorHeldAlertSent = true;

      AlertEvent al;
      al.eventUuid = Utils::uuidV4();
      al.type = "door_held_open";
      al.severity = "warning";
      al.message = "Door held open while armed.";
      al.isActive = false;
      al.createdAtIso = Utils::iso8601UtcNow();
      ApiClient::postAlert(al);
    }
  }

  // Lockout end
  endLockoutIfDone();

  // Ensure lockout buzzer stays ON for the entire lockout window when Alarm toggle is ON.
  // This "keepalive" also re-starts the continuous buzzer if it was interrupted by other short beeps.
  if (isLockedOut() && settings.alarmEnabled && !forcedEntryActive) {
    if (!lockoutBuzzerActive) {
      lockoutBuzzerActive = true;
      Utils::buzzerStartContinuous();
    }
  } else {
    // If not locked out (or Alarm OFF), stop the lockout buzzer (but never interrupt a forced-entry alarm).
    if (lockoutBuzzerActive && !forcedEntryActive) {
      lockoutBuzzerActive = false;
      Utils::buzzerStop();
    }
  }
  
  // UI transient timeout
  if (uiUntilMs > 0 && now >= uiUntilMs) {
    uiUntilMs = 0;
    if (forcedEntryActive) setUi(UiMode::Alarm);
    else if (isLockedOut()) setUi(UiMode::Lockout);
    else if (enrollmentInProgress) setUi(UiMode::Enroll);
    else setUi(UiMode::Ready);
  }

  // LED patterns
  if (forcedEntryActive) {
    if (now - lastBlinkMs >= GateXConfig::ALARM_BLINK_MS) {
      lastBlinkMs = now;
      blinkPhase = !blinkPhase;
      Utils::ledsAlarm(blinkPhase);
    }
  } else if (isLockedOut()) {
    if (now - lastBlinkMs >= GateXConfig::ALARM_BLINK_MS) {
      lastBlinkMs = now;
      blinkPhase = !blinkPhase;
      digitalWrite(GateXConfig::PIN_LED_RED, blinkPhase ? HIGH : LOW);
      digitalWrite(GateXConfig::PIN_LED_GREEN, LOW);
      digitalWrite(GateXConfig::PIN_LED_A, settings.alarmEnabled ? HIGH : LOW);
    }
  } else if (enrollmentInProgress) {
    if (now - lastBlinkMs >= 250) {
      lastBlinkMs = now;
      blinkPhase = !blinkPhase;
      digitalWrite(GateXConfig::PIN_LED_GREEN, blinkPhase ? HIGH : LOW);
      digitalWrite(GateXConfig::PIN_LED_RED, LOW);
      digitalWrite(GateXConfig::PIN_LED_A, settings.alarmEnabled ? HIGH : LOW);
    }
  } else {
    if (uiMode == UiMode::Ready) {
      Utils::ledsReady(settings.alarmEnabled);
    }
  }

  // Update state struct
  state.lockState = LockControl::lockState();
  state.doorState = Sensors::doorClosed() ? "closed" : "open";
  state.systemState = settings.alarmEnabled ? "armed" : "disarmed";
  state.wifiRssi = WifiManager::rssi();

  // Status posting (periodic + on change)
  bool changed = (state.lockState != lastSentState.lockState) ||
                 (state.doorState != lastSentState.doorState) ||
                 (state.systemState != lastSentState.systemState);

  if (changed || (now - lastStatusMs >= GateXConfig::STATUS_INTERVAL_MS)) {
    lastStatusMs = now;
    lastSentState = state;

    const String uuid = Utils::uuidV4();
    bool ok = ApiClient::postStatus(state, uuid);
    if (!ok) {
      ApiClient::setPendingStatus(state);
    }
  }

  // Flush queued events when possible
  ApiClient::update();
}
