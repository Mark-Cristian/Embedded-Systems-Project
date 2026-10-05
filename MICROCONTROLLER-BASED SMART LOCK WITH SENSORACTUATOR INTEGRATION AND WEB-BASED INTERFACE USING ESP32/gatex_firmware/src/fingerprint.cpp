#include "config.h"
#include <Adafruit_Fingerprint.h>

namespace {
  HardwareSerial& fpSerial = Serial2;
  Adafruit_Fingerprint finger(&fpSerial);

  Fingerprint::ScanCallback scanCb = nullptr;
  Fingerprint::EnrollEventCallback enrollEvtCb = nullptr;

  bool isReady = false;

  // Normal scan cooldown
  bool waitingFingerRelease = false;
  unsigned long lastPollMs = 0;
  unsigned long lastScanMs = 0;

  // Enrollment state machine
  enum class EnrollStage : uint8_t {
    Idle = 0,
    WaitFinger1,
    Image2Tz1,
    WaitRelease1,
    WaitFinger2,
    Image2Tz2,
    CreateModel,
    StoreModel,
    Success,
    Fail
  };

  EnrollStage enrollStage = EnrollStage::Idle;
  int enrollSlot = -1;
  bool enrollDone = false;
  bool enrollOk = false;
  String enrollErr = "";
  unsigned long enrollStartMs = 0;

  void evt(const char* e) {
    if (enrollEvtCb) enrollEvtCb(e, enrollSlot);
  }

  void setWaitingRelease() {
    waitingFingerRelease = true;
    lastScanMs = millis();
  }

  void resetEnrollState() {
    enrollStage = EnrollStage::Idle;
    enrollSlot = -1;
    enrollDone = false;
    enrollOk = false;
    enrollErr = "";
    enrollStartMs = 0;
  }

  void failEnroll(const String& err) {
    enrollErr = err;
    enrollOk = false;
    enrollDone = true;
    enrollStage = EnrollStage::Fail;
    evt("fail");
  }

  void succeedEnroll() {
    enrollErr = "";
    enrollOk = true;
    enrollDone = true;
    enrollStage = EnrollStage::Success;
    evt("success");
  }
}

namespace Fingerprint {

void begin(ScanCallback cb) {
  scanCb = cb;

  fpSerial.begin(GateXConfig::FP_BAUD, SERIAL_8N1, GateXConfig::PIN_FP_RX, GateXConfig::PIN_FP_TX);
  delay(30);

  finger.begin(GateXConfig::FP_BAUD);
  delay(30);

  isReady = finger.verifyPassword();

  waitingFingerRelease = false;
  lastPollMs = 0;
  lastScanMs = 0;

  resetEnrollState();

  if (GateXConfig::SERIAL_DEBUG) {
    if (isReady) Serial.println("[FP] Sensor ready");
    else Serial.println("[FP] Sensor NOT ready (verifyPassword failed)");
  }
}

void setEnrollEventCallback(EnrollEventCallback cb) {
  enrollEvtCb = cb;
}

bool ready() {
  return isReady;
}

bool startEnrollment(int slot) {
  if (!isReady) return false;
  if (enrollStage != EnrollStage::Idle) return false;
  if (slot < 0) return false;

  enrollSlot = slot;
  enrollStage = EnrollStage::WaitFinger1;
  enrollDone = false;
  enrollOk = false;
  enrollErr = "";
  enrollStartMs = millis();

  evt("start");
  evt("place_finger_1");
  return true;
}

bool enrolling() {
  return enrollStage != EnrollStage::Idle && enrollStage != EnrollStage::Success && enrollStage != EnrollStage::Fail;
}

bool enrollmentDone() {
  return enrollDone;
}

bool enrollmentSuccess() {
  return enrollDone && enrollOk;
}

String enrollmentError() {
  return enrollErr;
}

int enrollmentSlot() {
  return enrollSlot;
}

void cancelEnrollment() {
  resetEnrollState();
}

bool templateExists(int slot) {
  if (!isReady) return false;
  if (slot < 0) return false;

  int p = finger.loadModel((uint16_t)slot);
  return (p == FINGERPRINT_OK);
}

bool deleteTemplate(int slot) {
  if (!isReady) return false;
  if (slot < 0) return false;

  int p = finger.deleteModel((uint16_t)slot);
  return (p == FINGERPRINT_OK);
}

void update() {
  if (!isReady) return;

  const unsigned long now = millis();
  if (now - lastPollMs < GateXConfig::FP_POLL_MS) return;
  lastPollMs = now;

  // --------------------
  // Enrollment flow
  // --------------------
  if (enrollStage != EnrollStage::Idle && !enrollDone) {
    if ((now - enrollStartMs) > GateXConfig::FP_ENROLL_TIMEOUT_MS) {
      failEnroll("timeout");
      return;
    }

    switch (enrollStage) {
      case EnrollStage::WaitFinger1: {
        int p = finger.getImage();
        if (p == FINGERPRINT_OK) {
          enrollStage = EnrollStage::Image2Tz1;
        }
        break;
      }

      case EnrollStage::Image2Tz1: {
        int p = finger.image2Tz(1);
        if (p == FINGERPRINT_OK) {
          enrollStage = EnrollStage::WaitRelease1;
          evt("remove_finger");
        } else {
          failEnroll("image2Tz1");
        }
        break;
      }

      case EnrollStage::WaitRelease1: {
        int p = finger.getImage();
        if (p == FINGERPRINT_NOFINGER) {
          enrollStage = EnrollStage::WaitFinger2;
          evt("place_finger_2");
        }
        break;
      }

      case EnrollStage::WaitFinger2: {
        int p = finger.getImage();
        if (p == FINGERPRINT_OK) {
          enrollStage = EnrollStage::Image2Tz2;
        }
        break;
      }

      case EnrollStage::Image2Tz2: {
        int p = finger.image2Tz(2);
        if (p == FINGERPRINT_OK) {
          enrollStage = EnrollStage::CreateModel;
        } else {
          failEnroll("image2Tz2");
        }
        break;
      }

      case EnrollStage::CreateModel: {
        int p = finger.createModel();
        if (p == FINGERPRINT_OK) {
          enrollStage = EnrollStage::StoreModel;
        } else {
          failEnroll("createModel");
        }
        break;
      }

      case EnrollStage::StoreModel: {
        int p = finger.storeModel((uint16_t)enrollSlot);
        if (p == FINGERPRINT_OK) {
          succeedEnroll();
        } else {
          failEnroll("storeModel");
        }
        break;
      }

      default:
        break;
    }

    return; // don't scan while enrolling
  }

  // If enrollment is done, stay idle until main clears it.
  if (enrollDone) return;

  // --------------------
  // Normal scan flow
  // --------------------

  // If we just scanned, wait for finger release to avoid spamming events
  if (waitingFingerRelease) {
    int p = finger.getImage();
    if (p == FINGERPRINT_NOFINGER) {
      waitingFingerRelease = false;
    }
    return;
  }

  int p = finger.getImage();
  if (p != FINGERPRINT_OK) {
    return; // no finger
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    setWaitingRelease();
    return;
  }

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    if (scanCb) scanCb((int)finger.fingerID, (int)finger.confidence);
  } else if (p == FINGERPRINT_NOTFOUND) {
    if (scanCb) scanCb(-1, 0);
  }

  setWaitingRelease();
}

} // namespace Fingerprint
