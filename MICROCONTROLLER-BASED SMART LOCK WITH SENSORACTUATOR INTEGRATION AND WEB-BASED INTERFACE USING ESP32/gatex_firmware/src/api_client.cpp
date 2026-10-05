#include "config.h"

#include <WiFi.h>
#include <cstring>
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace {
  WiFiClient wifiClient;
  String lastErr = "";

  // Event queue (access attempts + alerts + fingerprint finalize)
  struct QItem {
    uint8_t type; // 1=access, 2=alert, 3=fp_finalize
    String json;
  };

  QItem q[GateXConfig::EVENT_QUEUE_MAX];
  size_t qHead = 0;
  size_t qTail = 0;
  size_t qCount = 0;

  // Pending/coalesced status (keep only latest)
  bool hasPendingStatus = false;
  String pendingStatusJson = "";

  unsigned long lastSendFailMs = 0; // backoff timer

  inline bool canRetryNow() {
    if (lastSendFailMs == 0) return true;
    return (millis() - lastSendFailMs) >= GateXConfig::API_RETRY_BACKOFF_MS;
  }

  String joinUrl(const char* endpoint) {
    String base = String(GateXConfig::BASE_URL);
    if (!base.endsWith("/")) base += "/";
    return base + endpoint;
  }

  void addCommonHeaders(HTTPClient& http) {
    http.addHeader("Accept", "application/json");
    if (GateXConfig::SEND_DEVICE_KEY_IF_SET && GateXConfig::DEVICE_KEY && strlen(GateXConfig::DEVICE_KEY) > 0) {
      http.addHeader(GateXConfig::DEVICE_KEY_HEADER, GateXConfig::DEVICE_KEY);
    }
  }

  void debugHttp(const char* label, const String& url, int httpCode, const String& body) {
    if (!GateXConfig::SERIAL_DEBUG) return;

    Serial.printf("[HTTP] %s %s -> %d\n", label, url.c_str(), httpCode);

    if (httpCode <= 0) {
      Serial.printf("[HTTP]   err=%s\n", HTTPClient().errorToString(httpCode).c_str());
    }

    if (body.length() > 0) {
      String snip = body;
      if (snip.length() > 120) snip = snip.substring(0, 120);
      snip.replace("\n", " ");
      snip.replace("\r", " ");
      Serial.printf("[HTTP]   body=%s\n", snip.c_str());
    }
  }

  bool parseApiOk(const String& body, DynamicJsonDocument& doc, String& errOut, String* errorCodeOut = nullptr) {
    DeserializationError e = deserializeJson(doc, body);
    if (e) {
      errOut = String("json_parse_error: ") + e.c_str();
      return false;
    }

    bool ok = doc["ok"] | false;
    if (!ok) {
      const char* msg = doc["error"]["message"] | "Unknown error";
      const char* code = doc["error"]["code"] | "";
      if (errorCodeOut) *errorCodeOut = String(code);
      errOut = String(code) + String(": ") + String(msg);
      return false;
    }

    return true;
  }

  bool httpGet(const String& url, String& responseBody, int& httpCode) {
    HTTPClient http;
    http.setConnectTimeout(GateXConfig::HTTP_CONNECT_TIMEOUT_MS);
    http.setTimeout(GateXConfig::HTTP_TIMEOUT_MS);

    if (!http.begin(wifiClient, url)) {
      httpCode = -1;
      responseBody = "";
      lastErr = "http_begin_failed";
      debugHttp("GET", url, httpCode, responseBody);
      return false;
    }

    addCommonHeaders(http);

    httpCode = http.GET();
    if (httpCode > 0) {
      responseBody = http.getString();
      debugHttp("GET", url, httpCode, responseBody);
      http.end();
      return true;
    }

    responseBody = "";
    lastErr = String("http_get_failed: ") + http.errorToString(httpCode);
    debugHttp("GET", url, httpCode, responseBody);
    http.end();
    return false;
  }

  bool httpPostJson(const String& url, const String& payloadJson, String& responseBody, int& httpCode) {
    HTTPClient http;
    http.setConnectTimeout(GateXConfig::HTTP_CONNECT_TIMEOUT_MS);
    http.setTimeout(GateXConfig::HTTP_TIMEOUT_MS);

    if (!http.begin(wifiClient, url)) {
      httpCode = -1;
      responseBody = "";
      lastErr = "http_begin_failed";
      debugHttp("POST", url, httpCode, responseBody);
      return false;
    }

    addCommonHeaders(http);
    http.addHeader("Content-Type", "application/json");

    httpCode = http.POST((uint8_t*)payloadJson.c_str(), payloadJson.length());
    if (httpCode > 0) {
      responseBody = http.getString();
      debugHttp("POST", url, httpCode, responseBody);
      http.end();
      return true;
    }

    responseBody = "";
    lastErr = String("http_post_failed: ") + http.errorToString(httpCode);
    debugHttp("POST", url, httpCode, responseBody);
    http.end();
    return false;
  }

  bool enqueue(uint8_t type, const String& json) {
    if (qCount >= GateXConfig::EVENT_QUEUE_MAX) {
      lastErr = "event_queue_full_drop_oldest";
      qHead = (qHead + 1) % GateXConfig::EVENT_QUEUE_MAX;
      qCount--;
    }

    q[qTail] = {type, json};
    qTail = (qTail + 1) % GateXConfig::EVENT_QUEUE_MAX;
    qCount++;
    return true;
  }

  bool peek(QItem& out) {
    if (qCount == 0) return false;
    out = q[qHead];
    return true;
  }

  void pop() {
    if (qCount == 0) return;
    qHead = (qHead + 1) % GateXConfig::EVENT_QUEUE_MAX;
    qCount--;
  }

  bool httpCodeOkForType(uint8_t type, int code) {
    if (type == 1) return code == 201;          // postAccessAttempt.php
    if (type == 2) return code == 201;          // postAlert.php
    if (type == 3) return (code == 200 || code == 201); // addFingerprint.php (finalize)
    return false;
  }

  const char* endpointForType(uint8_t type) {
    if (type == 1) return GateXConfig::EP_POST_ACCESS_ATTEMPT;
    if (type == 2) return GateXConfig::EP_POST_ALERT;
    if (type == 3) return GateXConfig::EP_ADD_FINGERPRINT;
    return GateXConfig::EP_POST_ALERT;
  }
}

namespace ApiClient {

void begin() {
  lastErr = "";
  qHead = qTail = qCount = 0;
  hasPendingStatus = false;
  pendingStatusJson = "";
  lastSendFailMs = 0;
}

String lastError() { return lastErr; }

bool hasBacklog() {
  return hasPendingStatus || (qCount > 0);
}

bool fetchSettings(DeviceSettings& out, bool includePinHash) {
  if (!WifiManager::connected()) {
    lastErr = "wifi_not_connected";
    return false;
  }

  String url = joinUrl(GateXConfig::EP_GET_SETTINGS);
  url += "?device_id=" + String(GateXConfig::DEVICE_ID);
  if (includePinHash) url += "&include_pin_hash=1";

  String body;
  int code = 0;
  if (!httpGet(url, body, code)) {
    return false;
  }

  if (code != 200) {
    lastErr = String("getSettings_http_") + String(code);
    return false;
  }

  DynamicJsonDocument doc(2048);
  String parseErr;
  if (!parseApiOk(body, doc, parseErr)) {
    lastErr = String("getSettings_") + parseErr;
    return false;
  }

  JsonObject data = doc["data"];
  out.deviceId = data["device_id"] | GateXConfig::DEVICE_ID;
  out.lockoutSeconds = data["lockout_seconds"] | out.lockoutSeconds;
  out.alarmEnabled = data["alarm_enabled"] | out.alarmEnabled;
  out.maxFailedAttempts = data["max_failed_attempts"] | out.maxFailedAttempts;
  out.settingsVersion = data["settings_version"] | out.settingsVersion;
  out.updatedAt = (const char*)(data["updated_at"] | "");
  out.pinIsDefault = data["pin_is_default"] | out.pinIsDefault;

  if (data.containsKey("pin_hash") && !data["pin_hash"].isNull()) {
    out.pinHash = (const char*)data["pin_hash"];
  }

  return true;
}

bool verifyCurrentPin(const String& pin, String& errorCodeOut) {
  errorCodeOut = "";

  DynamicJsonDocument doc(256);
  doc["device_id"] = GateXConfig::DEVICE_ID;
  doc["current_pin"] = pin;

  String payload;
  serializeJson(doc, payload);

  if (!WifiManager::connected()) {
    errorCodeOut = "offline";
    lastErr = "verify_pin_offline";
    return false;
  }

  String url = joinUrl(GateXConfig::EP_VERIFY_CURRENT_PIN);
  String resp;
  int code = 0;
  if (!httpPostJson(url, payload, resp, code)) {
    errorCodeOut = "http_error";
    return false;
  }

  // verifyCurrentPin.php returns 200 on success, 401 on incorrect.
  DynamicJsonDocument outDoc(1024);
  String parseErr;
  String errCode;

  if (code == 200) {
    if (!parseApiOk(resp, outDoc, parseErr, &errCode)) {
      lastErr = String("verify_pin_") + parseErr;
      errorCodeOut = (errCode.length() ? errCode : "api_error");
      return false;
    }
    bool verified = outDoc["data"]["verified"] | false;
    if (!verified) {
      errorCodeOut = "not_verified";
      return false;
    }
    return true;
  }

  // For non-200, try to extract server error code
  if (!parseApiOk(resp, outDoc, parseErr, &errCode)) {
    // parseApiOk will populate errCode if present
    errorCodeOut = errCode.length() ? errCode : String("http_") + String(code);
    lastErr = String("verify_pin_") + parseErr;
    return false;
  }

  errorCodeOut = String("http_") + String(code);
  lastErr = String("verify_pin_http_") + String(code);
  return false;
}

bool fetchFingerprints(std::vector<FingerprintRecord>& out) {
  out.clear();

  if (!WifiManager::connected()) {
    lastErr = "wifi_not_connected";
    return false;
  }

  String url = joinUrl(GateXConfig::EP_GET_FINGERPRINTS);
  url += "?device_id=" + String(GateXConfig::DEVICE_ID) + "&limit=500&offset=0";

  String body;
  int code = 0;
  if (!httpGet(url, body, code)) {
    return false;
  }

  if (code != 200) {
    lastErr = String("getFingerprints_http_") + String(code);
    return false;
  }

  DynamicJsonDocument doc(8192);
  String parseErr;
  if (!parseApiOk(body, doc, parseErr)) {
    lastErr = String("getFingerprints_") + parseErr;
    return false;
  }

  JsonArray fps = doc["data"]["fingerprints"].as<JsonArray>();
  for (JsonObject fp : fps) {
    FingerprintRecord r;
    r.userCode = (const char*)(fp["user_code"] | "");
    r.displayName = (const char*)(fp["display_name"] | "");
    r.sensorSlot = fp["sensor_slot"] | -1;
    if (fp.containsKey("enrolled_at") && !fp["enrolled_at"].isNull()) {
      r.enrolledAtIso = (const char*)fp["enrolled_at"];
    } else {
      r.enrolledAtIso = "";
    }

    if (r.sensorSlot >= 0 && r.userCode.length() > 0) {
      out.push_back(r);
    }
  }

  return true;
}

bool finalizeFingerprintEnrollment(const FingerprintRecord& rec, String& errorOut) {
  errorOut = "";

  DynamicJsonDocument doc(768);
  doc["device_id"] = GateXConfig::DEVICE_ID;
  doc["name"] = rec.displayName;
  doc["user_code"] = rec.userCode;
  doc["simulate"] = false;
  // Optional: let server validate slot if it supports it (safe to ignore if not used)
  doc["sensor_slot"] = rec.sensorSlot;

  String payload;
  serializeJson(doc, payload);

  if (!WifiManager::connected()) {
    enqueue(3, payload);
    lastErr = "wifi_offline_queue_fp_finalize";
    errorOut = "offline_queued";
    return false;
  }

  String url = joinUrl(GateXConfig::EP_ADD_FINGERPRINT);
  String resp;
  int code = 0;
  if (!httpPostJson(url, payload, resp, code)) {
    enqueue(3, payload);
    errorOut = "http_error_queued";
    return false;
  }

  if (!(code == 200 || code == 201)) {
    enqueue(3, payload);
    lastErr = String("fp_finalize_http_") + String(code);
    errorOut = String("http_") + String(code);
    return false;
  }

  DynamicJsonDocument outDoc(1024);
  String parseErr;
  String errCode;
  if (!parseApiOk(resp, outDoc, parseErr, &errCode)) {
    enqueue(3, payload);
    lastErr = String("fp_finalize_") + parseErr;
    errorOut = errCode.length() ? errCode : "api_error";
    return false;
  }

  return true;
}

bool postStatus(const DeviceState& st, const String& eventUuid) {
  if (!WifiManager::connected()) {
    lastErr = "wifi_not_connected";
    return false;
  }

  DynamicJsonDocument doc(768);
  doc["device_id"] = GateXConfig::DEVICE_ID;
  doc["event_uuid"] = eventUuid;
  doc["lock_state"] = st.lockState;
  doc["door_state"] = st.doorState;
  doc["system_state"] = st.systemState;

  int rssi = st.wifiRssi;
  if (rssi < -120) rssi = -120;
  if (rssi > 0) rssi = 0;
  doc["wifi_rssi"] = rssi;

  if (st.batteryPercent >= 0) doc["battery_percent"] = st.batteryPercent;

  String payload;
  serializeJson(doc, payload);

  String url = joinUrl(GateXConfig::EP_POST_STATUS);
  String resp;
  int code = 0;
  if (!httpPostJson(url, payload, resp, code)) {
    return false;
  }

  if (code != 200) {
    lastErr = String("postStatus_http_") + String(code);
    return false;
  }

  DynamicJsonDocument outDoc(768);
  String parseErr;
  if (!parseApiOk(resp, outDoc, parseErr)) {
    lastErr = String("postStatus_") + parseErr;
    return false;
  }

  return true;
}

bool postAccessAttempt(const AccessAttempt& a) {
  DynamicJsonDocument doc(1024);
  doc["device_id"] = GateXConfig::DEVICE_ID;
  doc["event_uuid"] = a.eventUuid;
  doc["method"] = a.method;
  doc["result"] = a.result;

  if (a.userCode.length() > 0) doc["user_code"] = a.userCode;
  if (a.usernameSnapshot.length() > 0) doc["username_snapshot"] = a.usernameSnapshot;
  if (a.reason.length() > 0) doc["reason"] = a.reason;
  if (a.eventAtIso.length() > 0) doc["event_at"] = a.eventAtIso;

  String payload;
  serializeJson(doc, payload);

  if (!WifiManager::connected()) {
    enqueue(1, payload);
    lastErr = "wifi_offline_queue_access";
    return false;
  }

  String url = joinUrl(GateXConfig::EP_POST_ACCESS_ATTEMPT);
  String resp;
  int code = 0;
  if (!httpPostJson(url, payload, resp, code)) {
    enqueue(1, payload);
    return false;
  }

  if (code != 201) {
    lastErr = String("postAccessAttempt_http_") + String(code);
    enqueue(1, payload);
    return false;
  }

  DynamicJsonDocument outDoc(768);
  String parseErr;
  if (!parseApiOk(resp, outDoc, parseErr)) {
    lastErr = String("postAccessAttempt_") + parseErr;
    enqueue(1, payload);
    return false;
  }

  return true;
}

bool postAlert(const AlertEvent& a) {
  DynamicJsonDocument doc(1024);
  doc["device_id"] = GateXConfig::DEVICE_ID;
  doc["event_uuid"] = a.eventUuid;
  doc["type"] = a.type;
  doc["severity"] = a.severity;
  doc["message"] = a.message;
  doc["is_active"] = a.isActive;

  if (a.createdAtIso.length() > 0) doc["created_at"] = a.createdAtIso;
  if (a.clearedAtIso.length() > 0) doc["cleared_at"] = a.clearedAtIso;

  String payload;
  serializeJson(doc, payload);

  if (!WifiManager::connected()) {
    enqueue(2, payload);
    lastErr = "wifi_offline_queue_alert";
    return false;
  }

  String url = joinUrl(GateXConfig::EP_POST_ALERT);
  String resp;
  int code = 0;
  if (!httpPostJson(url, payload, resp, code)) {
    enqueue(2, payload);
    return false;
  }

  if (code != 201) {
    lastErr = String("postAlert_http_") + String(code);
    enqueue(2, payload);
    return false;
  }

  DynamicJsonDocument outDoc(768);
  String parseErr;
  if (!parseApiOk(resp, outDoc, parseErr)) {
    lastErr = String("postAlert_") + parseErr;
    enqueue(2, payload);
    return false;
  }

  return true;
}

void setPendingStatus(const DeviceState& st) {
  DynamicJsonDocument doc(768);
  doc["device_id"] = GateXConfig::DEVICE_ID;
  doc["event_uuid"] = Utils::uuidV4();
  doc["lock_state"] = st.lockState;
  doc["door_state"] = st.doorState;
  doc["system_state"] = st.systemState;

  int rssi = st.wifiRssi;
  if (rssi < -120) rssi = -120;
  if (rssi > 0) rssi = 0;
  doc["wifi_rssi"] = rssi;

  if (st.batteryPercent >= 0) doc["battery_percent"] = st.batteryPercent;

  pendingStatusJson = "";
  serializeJson(doc, pendingStatusJson);
  hasPendingStatus = true;
}

void update() {
  if (!WifiManager::connected()) return;
  if (!canRetryNow()) return;

  // 1) Pending status first
  if (hasPendingStatus) {
    String url = joinUrl(GateXConfig::EP_POST_STATUS);
    String resp;
    int code = 0;
    if (!httpPostJson(url, pendingStatusJson, resp, code) || code != 200) {
      lastSendFailMs = millis();
      if (code > 0) lastErr = String("retry_postStatus_http_") + String(code);
      return;
    }

    DynamicJsonDocument outDoc(768);
    String parseErr;
    if (!parseApiOk(resp, outDoc, parseErr)) {
      lastSendFailMs = millis();
      lastErr = String("retry_postStatus_") + parseErr;
      return;
    }

    hasPendingStatus = false;
    pendingStatusJson = "";
    lastSendFailMs = 0;
    return;
  }

  // 2) Flush one queued item per call
  if (qCount == 0) return;

  QItem item;
  if (!peek(item)) return;

  const char* ep = endpointForType(item.type);
  String url = joinUrl(ep);

  String resp;
  int code = 0;
  if (!httpPostJson(url, item.json, resp, code) || !httpCodeOkForType(item.type, code)) {
    lastSendFailMs = millis();
    if (code > 0) lastErr = String("retry_queue_http_") + String(code);
    return;
  }

  DynamicJsonDocument outDoc(1024);
  String parseErr;
  if (!parseApiOk(resp, outDoc, parseErr)) {
    lastSendFailMs = millis();
    lastErr = String("retry_queue_") + parseErr;
    return;
  }

  // success
  pop();
  lastSendFailMs = 0;
}

} // namespace ApiClient
