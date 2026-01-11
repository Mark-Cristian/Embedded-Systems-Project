#include "config.h"

namespace {
  const uint8_t ROWS[4] = {
    GateXConfig::PIN_KP_R1,
    GateXConfig::PIN_KP_R2,
    GateXConfig::PIN_KP_R3,
    GateXConfig::PIN_KP_R4
  };

  const uint8_t COLS[4] = {
    GateXConfig::PIN_KP_C1,
    GateXConfig::PIN_KP_C2,
    GateXConfig::PIN_KP_C3,
    GateXConfig::PIN_KP_C4
  };

  const char KEYMAP[4][4] = {
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
  };

  Keypad::KeyCallback onKeyCb = nullptr;
  Keypad::PinCallback onPinCb = nullptr;

  bool lockedOut = false;
  uint32_t lockedOutSecondsRemaining = 0;

  // Debounce state
  char lastRawKey = 0;
  char stableKey = 0;
  unsigned long rawChangeMs = 0;

  String entry = "";
  unsigned long lastInputMs = 0;

  inline void rowsAllHigh() {
    for (int i = 0; i < 4; i++) {
      digitalWrite(ROWS[i], HIGH);
    }
  }

  char scanRawKey() {
    rowsAllHigh();

    for (int r = 0; r < 4; r++) {
      digitalWrite(ROWS[r], LOW);
      // read cols
      for (int c = 0; c < 4; c++) {
        if (digitalRead(COLS[c]) == LOW) {
          rowsAllHigh();
          return KEYMAP[r][c];
        }
      }
      digitalWrite(ROWS[r], HIGH);
    }

    rowsAllHigh();
    return 0;
  }

  inline bool isDigitKey(char k) {
    return (k >= '0' && k <= '9');
  }

  void clearEntry() {
    entry = "";
    lastInputMs = 0;
  }

  void handleKey(char k) {
    if (onKeyCb) onKeyCb(k);

    if (lockedOut) return;

    const unsigned long now = millis();

    if (k == '*') {
      clearEntry();
      return;
    }

    if (k == '#') {
      if (entry.length() >= GateXConfig::PIN_MIN_LEN && entry.length() <= GateXConfig::PIN_MAX_LEN) {
        if (onPinCb) onPinCb(entry);
      }
      clearEntry();
      return;
    }

    if (isDigitKey(k)) {
      if (entry.length() < GateXConfig::PIN_MAX_LEN) {
        entry += k;
        lastInputMs = now;
      }
      return;
    }

    // Ignore A/B/C/D for PIN entry
  }
}

namespace Keypad {

void begin(KeyCallback onKey, PinCallback onPinComplete) {
  onKeyCb = onKey;
  onPinCb = onPinComplete;

  // Rows: outputs, idle HIGH
  for (int i = 0; i < 4; i++) {
    pinMode(ROWS[i], OUTPUT);
    digitalWrite(ROWS[i], HIGH);
  }

  // Cols: inputs only, external pullups provided (GPIO34/35/36/39 have no internal pullups)
  for (int i = 0; i < 4; i++) {
    pinMode(COLS[i], INPUT);
  }

  lockedOut = false;
  lockedOutSecondsRemaining = 0;
  lastRawKey = 0;
  stableKey = 0;
  rawChangeMs = millis();
  clearEntry();
}

void update() {
  const unsigned long now = millis();

  // PIN entry timeout
  if (!lockedOut && entry.length() > 0 && lastInputMs > 0) {
    if (now - lastInputMs > GateXConfig::PIN_ENTRY_TIMEOUT_MS) {
      clearEntry();
    }
  }

  char raw = scanRawKey();

  if (raw != lastRawKey) {
    lastRawKey = raw;
    rawChangeMs = now;
  }

  // debounce to stableKey
  if ((now - rawChangeMs) >= GateXConfig::KEYPAD_DEBOUNCE_MS) {
    if (stableKey != raw) {
      stableKey = raw;
      if (stableKey != 0) {
        handleKey(stableKey);
      }
    }
  }
}

void setLockedOut(bool isLockedOut, uint32_t secondsRemaining) {
  lockedOut = isLockedOut;
  lockedOutSecondsRemaining = secondsRemaining;
  if (lockedOut) {
    clearEntry();
  }
}

void resetEntry() {
  clearEntry();
}

String maskedEntry() {
  String out = "";
  for (size_t i = 0; i < entry.length(); i++) out += "*";
  return out;
}

String rawEntry() {
  return entry;
}

} // namespace Keypad
