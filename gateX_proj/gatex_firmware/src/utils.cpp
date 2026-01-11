#include "config.h"

#include <WiFi.h>
#include <time.h>
#include <esp_system.h>
#include "mbedtls/sha256.h"

namespace {
  // ---------- Time ----------
  bool ntpRequested = false;

  // ---------- Buzzer ----------
  enum class BuzzMode : uint8_t { None, Key, Grant, Deny, Continuous };

  BuzzMode buzzMode = BuzzMode::None;
  uint8_t buzzStep = 0;
  unsigned long buzzUntilMs = 0;

  inline void buzzerHwOn() {
    // Set frequency + duty (tone)
    ledcWriteTone(GateXConfig::BUZZER_LEDC_CH, GateXConfig::BUZZER_TONE_HZ);
    ledcWrite(GateXConfig::BUZZER_LEDC_CH, GateXConfig::BUZZER_DUTY_ON);
  }

  inline void buzzerHwOff() {
    ledcWrite(GateXConfig::BUZZER_LEDC_CH, 0);
  }

  // ---------- LEDs ----------
  inline void pinOut(uint8_t pin, bool level) {
    digitalWrite(pin, level ? HIGH : LOW);
  }
}

namespace Utils {

void timeSyncBegin() {
  if (ntpRequested) return;
  // Use UTC; your PHP uses UTC_TIMESTAMP() in DB.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  ntpRequested = true;
}

bool timeValid() {
  time_t now = time(nullptr);
  // If NTP not synced, ESP32 often returns Jan 1 1970 (0) or a small value.
  // Threshold: 2023-01-01
  return now > 1672531200;
}

String iso8601UtcNow() {
  if (!timeValid()) return "";
  time_t now = time(nullptr);
  struct tm tmUtc;
  gmtime_r(&now, &tmUtc);
  char buf[25];
  // 2025-12-26T18:22:33Z
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
  return String(buf);
}

String uuidV4() {
  uint8_t b[16];
  for (int i = 0; i < 16; i++) {
    uint32_t r = esp_random();
    b[i] = (uint8_t)(r & 0xFF);
  }
  // Set version to 4
  b[6] = (b[6] & 0x0F) | 0x40;
  // Set variant to 10xxxxxx
  b[8] = (b[8] & 0x3F) | 0x80;

  char out[37];
  snprintf(out, sizeof(out),
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           b[0], b[1], b[2], b[3],
           b[4], b[5],
           b[6], b[7],
           b[8], b[9],
           b[10], b[11], b[12], b[13], b[14], b[15]);
  return String(out);
}

bool isHexString(const String& s) {
  if (s.length() == 0) return false;
  for (size_t i = 0; i < (size_t)s.length(); i++) {
    char c = s[i];
    bool ok = (c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F');
    if (!ok) return false;
  }
  return true;
}

String sha256Hex(const String& s) {
  uint8_t digest[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts_ret(&ctx, 0 /*is224=0 => SHA-256*/);
  mbedtls_sha256_update_ret(&ctx, (const unsigned char*)s.c_str(), s.length());
  mbedtls_sha256_finish_ret(&ctx, digest);
  mbedtls_sha256_free(&ctx);

  static const char* hex = "0123456789abcdef";
  char out[65];
  for (int i = 0; i < 32; i++) {
    out[i * 2 + 0] = hex[(digest[i] >> 4) & 0xF];
    out[i * 2 + 1] = hex[digest[i] & 0xF];
  }
  out[64] = '\0';
  return String(out);
}

void ledsBegin() {
  pinMode(GateXConfig::PIN_LED_GREEN, OUTPUT);
  pinMode(GateXConfig::PIN_LED_RED, OUTPUT);
  pinMode(GateXConfig::PIN_LED_A, OUTPUT);
  ledsAllOff();
}

void ledsAllOff() {
  pinOut(GateXConfig::PIN_LED_GREEN, false);
  pinOut(GateXConfig::PIN_LED_RED, false);
  pinOut(GateXConfig::PIN_LED_A, false);
}

void ledsReady(bool armed) {
  pinOut(GateXConfig::PIN_LED_GREEN, false);
  pinOut(GateXConfig::PIN_LED_RED, false);
  pinOut(GateXConfig::PIN_LED_A, armed);
}

void ledsGranted() {
  pinOut(GateXConfig::PIN_LED_GREEN, true);
  pinOut(GateXConfig::PIN_LED_RED, false);
  // LED_A left unchanged by design (armed indicator managed by main)
}

void ledsDenied() {
  pinOut(GateXConfig::PIN_LED_GREEN, false);
  pinOut(GateXConfig::PIN_LED_RED, true);
  // LED_A left unchanged by design
}

void ledsAlarm(bool phaseOn) {
  // Blink red + LED_A (armed/alarm indicator)
  pinOut(GateXConfig::PIN_LED_GREEN, false);
  pinOut(GateXConfig::PIN_LED_RED, phaseOn);
  pinOut(GateXConfig::PIN_LED_A, phaseOn);
}

void buzzerBegin() {
  // 8-bit PWM is enough for a buzzer.
  ledcSetup(GateXConfig::BUZZER_LEDC_CH, GateXConfig::BUZZER_TONE_HZ, 8);
  ledcAttachPin(GateXConfig::PIN_BUZZER, GateXConfig::BUZZER_LEDC_CH);
  buzzerHwOff();
  buzzMode = BuzzMode::None;
  buzzStep = 0;
  buzzUntilMs = 0;
}

void buzzerUpdate() {
  if (buzzMode == BuzzMode::None) return;

  const unsigned long now = millis();
  if (buzzMode == BuzzMode::Continuous) {
    // stays on until buzzerStop()
    return;
  }

  if (now < buzzUntilMs) return;

  switch (buzzMode) {
    case BuzzMode::Key:
      buzzerHwOff();
      buzzMode = BuzzMode::None;
      break;

    case BuzzMode::Deny:
      buzzerHwOff();
      buzzMode = BuzzMode::None;
      break;

    case BuzzMode::Grant:
      if (buzzStep == 0) {
        // off gap
        buzzerHwOff();
        buzzStep = 1;
        buzzUntilMs = now + 60;
      } else if (buzzStep == 1) {
        // second beep
        buzzerHwOn();
        buzzStep = 2;
        buzzUntilMs = now + 120;
      } else {
        buzzerHwOff();
        buzzMode = BuzzMode::None;
      }
      break;

    default:
      // safety
      buzzerHwOff();
      buzzMode = BuzzMode::None;
      break;
  }
}

void buzzerKey() {
  if (buzzMode == BuzzMode::Continuous) return;
  buzzMode = BuzzMode::Key;
  buzzStep = 0;
  buzzerHwOn();
  buzzUntilMs = millis() + 25;
}

void buzzerGrant() {
  if (buzzMode == BuzzMode::Continuous) return;
  buzzMode = BuzzMode::Grant;
  buzzStep = 0;
  buzzerHwOn();
  buzzUntilMs = millis() + 80;
}

void buzzerDeny() {
  if (buzzMode == BuzzMode::Continuous) return;
  buzzMode = BuzzMode::Deny;
  buzzStep = 0;
  buzzerHwOn();
  buzzUntilMs = millis() + 220;
}

void buzzerStartContinuous() {
  buzzMode = BuzzMode::Continuous;
  buzzerHwOn();
}

void buzzerStop() {
  buzzerHwOff();
  buzzMode = BuzzMode::None;
  buzzStep = 0;
  buzzUntilMs = 0;
}

} // namespace Utils
