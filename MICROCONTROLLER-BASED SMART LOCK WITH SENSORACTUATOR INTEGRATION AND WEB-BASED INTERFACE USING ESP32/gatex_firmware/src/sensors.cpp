#include "config.h"

namespace {
  bool stableDoorClosed = true;
  bool lastStableDoorClosed = true;

  bool rawDoorClosed = true;
  bool lastRawDoorClosed = true;

  unsigned long lastRawChangeMs = 0;

  bool flagJustOpened = false;
  bool flagJustClosed = false;

  inline bool readDoorClosedRaw() {
    // INPUT_PULLUP, CLOSED = LOW
    return digitalRead(GateXConfig::PIN_REED) == LOW;
  }
}

namespace Sensors {

void begin() {
  pinMode(GateXConfig::PIN_REED, INPUT_PULLUP);

  rawDoorClosed = readDoorClosedRaw();
  lastRawDoorClosed = rawDoorClosed;
  stableDoorClosed = rawDoorClosed;
  lastStableDoorClosed = stableDoorClosed;

  lastRawChangeMs = millis();
  flagJustOpened = false;
  flagJustClosed = false;
}

void update() {
  const unsigned long now = millis();
  rawDoorClosed = readDoorClosedRaw();

  if (rawDoorClosed != lastRawDoorClosed) {
    lastRawDoorClosed = rawDoorClosed;
    lastRawChangeMs = now;
  }

  // Debounce: only accept raw state if stable for DOOR_DEBOUNCE_MS
  if ((now - lastRawChangeMs) >= GateXConfig::DOOR_DEBOUNCE_MS) {
    if (stableDoorClosed != rawDoorClosed) {
      lastStableDoorClosed = stableDoorClosed;
      stableDoorClosed = rawDoorClosed;

      if (lastStableDoorClosed && !stableDoorClosed) {
        // closed -> open
        flagJustOpened = true;
      } else if (!lastStableDoorClosed && stableDoorClosed) {
        // open -> closed
        flagJustClosed = true;
      }
    }
  }
}

bool doorClosed() {
  return stableDoorClosed;
}

bool doorJustOpened() {
  bool v = flagJustOpened;
  flagJustOpened = false;
  return v;
}

bool doorJustClosed() {
  bool v = flagJustClosed;
  flagJustClosed = false;
  return v;
}

} // namespace Sensors
