#include "config.h"

namespace {
  enum class PulseMode : uint8_t { None = 0, Unlock, ClickTest };

  PulseMode mode = PulseMode::None;
  unsigned long relayOffAtMs = 0;
  unsigned long cooldownUntilMs = 0;

  inline void relayWrite(bool active) {
    if (GateXConfig::RELAY_ACTIVE_LOW) {
      digitalWrite(GateXConfig::PIN_RELAY_SOL, active ? LOW : HIGH);
    } else {
      digitalWrite(GateXConfig::PIN_RELAY_SOL, active ? HIGH : LOW);
    }
  }
}

namespace LockControl {

void begin() {
  // Safe boot default: relay OFF (inactive) before configuring OUTPUT
  relayWrite(false);
  pinMode(GateXConfig::PIN_RELAY_SOL, OUTPUT);
  relayWrite(false);

  mode = PulseMode::None;
  relayOffAtMs = 0;
  cooldownUntilMs = 0;
}

void update() {
  const unsigned long now = millis();
  if (mode != PulseMode::None && now >= relayOffAtMs) {
    mode = PulseMode::None;
    relayWrite(false);
  }
}

bool canUnlock() {
  const unsigned long now = millis();
  if (mode != PulseMode::None) return false;
  if (now < cooldownUntilMs) return false;
  return true;
}

bool requestUnlock() {
  if (!canUnlock()) return false;

  const unsigned long now = millis();
  mode = PulseMode::Unlock;
  relayOffAtMs = now + GateXConfig::UNLOCK_PULSE_MS;
  cooldownUntilMs = now + GateXConfig::UNLOCK_COOLDOWN_MS;
  relayWrite(true);
  return true;
}

bool relayClickTest(uint32_t pulseMs) {
  const unsigned long now = millis();
  if (mode != PulseMode::None) return false;

  // Click test intentionally does NOT apply the long solenoid cooldown.
  mode = PulseMode::ClickTest;
  relayOffAtMs = now + (pulseMs > 0 ? pulseMs : GateXConfig::RELAY_CLICK_TEST_MS);
  relayWrite(true);
  return true;
}

bool isUnlocking() {
  return mode == PulseMode::Unlock;
}

bool inCooldown() {
  return millis() < cooldownUntilMs;
}

String lockState() {
  // Expose relay active state as "unlocked" for dashboard visibility.
  return (mode == PulseMode::None) ? "locked" : "unlocked";
}

} // namespace LockControl
