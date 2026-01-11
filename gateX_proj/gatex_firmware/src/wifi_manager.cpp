#include "config.h"
#include <WiFi.h>

namespace {
  enum class WifiState : uint8_t { Idle, Connecting, Connected };

  WifiState state = WifiState::Idle;
  unsigned long connectStartedMs = 0;
  unsigned long nextAttemptAtMs = 0;
  unsigned long backoffMs = 2000;

  String lastIp = "";
}

namespace WifiManager {

void begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false); // often improves ESP32 WiFi stability
  WiFi.disconnect(true, true);
  delay(20);

  state = WifiState::Idle;
  connectStartedMs = 0;
  nextAttemptAtMs = 0;
  backoffMs = 2000;
  lastIp = "";
}

static void startConnect() {
  connectStartedMs = millis();
  WiFi.begin(GateXConfig::WIFI_SSID, GateXConfig::WIFI_PASS);
  state = WifiState::Connecting;
}

void update() {
  const unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    if (state != WifiState::Connected) {
      state = WifiState::Connected;
      lastIp = WiFi.localIP().toString();
      backoffMs = 2000;
    }
    return;
  }

  // Not connected
  if (state == WifiState::Connected) {
    state = WifiState::Idle;
    lastIp = "";
    nextAttemptAtMs = now; // try immediately
  }

  if (state == WifiState::Idle) {
    if (now >= nextAttemptAtMs) {
      startConnect();
    }
    return;
  }

  // Connecting
  if (state == WifiState::Connecting) {
    // If connection attempt takes too long, restart with backoff
    if (now - connectStartedMs > 8000) {
      WiFi.disconnect(true, true);
      delay(10);

      // exponential-ish backoff (cap at 30s)
      backoffMs = (unsigned long)min((uint32_t)30000, (uint32_t)(backoffMs * 2));
      nextAttemptAtMs = now + backoffMs;
      state = WifiState::Idle;
    }
    return;
  }
}

bool connected() {
  return WiFi.status() == WL_CONNECTED;
}

String ip() {
  if (!connected()) return "";
  return WiFi.localIP().toString();
}

int rssi() {
  if (!connected()) return -127;
  return WiFi.RSSI();
}

} // namespace WifiManager
