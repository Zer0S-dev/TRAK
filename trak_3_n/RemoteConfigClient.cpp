#include <Arduino.h>
#include "RemoteConfigClient.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HardwareSerial.h>
#include <ctype.h>
#include "SmsConfigManager.h"

extern HardwareSerial modem;
extern volatile bool modemReady;
extern volatile bool cellularReady;
extern String at(const String& command, uint32_t timeoutMs);
extern void devLog(const String& message);

namespace {
constexpr uint32_t FIRST_CHECK_DELAY_MS = 0UL;
constexpr uint32_t RETRY_INTERVAL_MS = 10000UL;
constexpr uint32_t CHECK_INTERVAL_MS = 300000UL;
constexpr uint32_t WIFI_TIMEOUT_MS = 2500UL;
constexpr uint32_t CELLULAR_TIMEOUT_MS = 8000UL;
constexpr size_t MAX_URL_LEN = 160;

uint32_t lastCheck = 0;
bool initialized = false;
bool busy = false;

String configApiUrl() {
  String url = smsConfigDashboardUrl();
  url.trim();
  if (url.isEmpty() || url.length() > MAX_URL_LEN) return "";

  const int query = url.indexOf('?');
  if (query >= 0) url = url.substring(0, query);
  while (url.endsWith("/")) url.remove(url.length() - 1);

  if (url.endsWith("/api/trak_config.php")) return url;

  const int slash = url.lastIndexOf('/');
  if (slash < 0) return "";

  url = url.substring(0, slash + 1) + "api/trak_config.php";
  return url.length() <= MAX_URL_LEN ? url : "";
}

bool jsonBool(const String& json, const char* key, bool& value) {
  const String needle = String("\"") + key + "\":";
  int p = json.indexOf(needle);
  if (p < 0) return false;
  p += needle.length();
  while (p < (int)json.length() && isspace((unsigned char)json[p])) ++p;
  if (json.startsWith("true", p) || json.startsWith("1", p)) { value = true; return true; }
  if (json.startsWith("false", p) || json.startsWith("0", p)) { value = false; return true; }
  return false;
}

bool jsonUint64(const String& json, const char* key, uint64_t& value) {
  const String needle = String("\"") + key + "\":";
  int p = json.indexOf(needle);
  if (p < 0) return false;
  p += needle.length();
  while (p < (int)json.length() && (isspace((unsigned char)json[p]) || json[p] == '"')) ++p;
  uint64_t n = 0;
  bool found = false;
  while (p < (int)json.length() && isdigit((unsigned char)json[p])) {
    found = true;
    n = n * 10ULL + (uint64_t)(json[p++] - '0');
  }
  if (!found) return false;
  value = n;
  return true;
}

bool jsonString(const String& json, const char* key, String& value) {
  const String needle = String("\"") + key + "\":\"";
  const int start = json.indexOf(needle);
  if (start < 0) return false;

  int p = start + needle.length();
  String out;
  bool escaped = false;
  while (p < (int)json.length()) {
    const char c = json[p++];
    if (escaped) {
      if (c == 'n') out += '\n';
      else if (c == 'r') out += '\r';
      else if (c == 't') out += '\t';
      else out += c;
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      value = out;
      return true;
    } else {
      out += c;
    }
  }
  return false;
}

bool jsonObjectForKey(const String& json, const char* key, String& object) {
  const String needle = String("\"") + key + "\":{";
  const int start = json.indexOf(needle);
  if (start < 0) return false;

  int p = start + needle.length() - 1;
  int depth = 0;
  bool inString = false;
  bool escaped = false;
  for (; p < (int)json.length(); ++p) {
    const char c = json[p];
    if (inString) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') inString = false;
      continue;
    }
    if (c == '"') { inString = true; continue; }
    if (c == '{') ++depth;
    else if (c == '}' && --depth == 0) {
      object = json.substring(start + needle.length() - 1, p + 1);
      return true;
    }
  }
  return false;
}

bool jsonWifiSlot(const String& object, uint8_t slot, String& ssid, String& password) {
  const String needle = String("\"slot\":") + String(slot);
  const int slotPos = object.indexOf(needle);
  if (slotPos < 0) return false;
  const int objectStart = object.lastIndexOf('{', slotPos);
  const int objectEnd = object.indexOf('}', slotPos);
  if (objectStart < 0 || objectEnd <= objectStart) return false;
  const String item = object.substring(objectStart, objectEnd + 1);
  return jsonString(item, "ssid", ssid) && jsonString(item, "password", password);
}

bool responseOk(const String& response) {
  bool ok = false;
  return jsonBool(response, "ok", ok) && ok;
}

bool postWiFi(const String& url, const String& body, String& response) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(WIFI_TIMEOUT_MS);
  http.setTimeout(WIFI_TIMEOUT_MS);
  if (!http.begin(client, url)) return false;

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + smsConfigApiKey());
  const int code = http.POST(body);
  response = code > 0 ? http.getString() : "";
  http.end();

  if (code >= 200 && code < 300) return true;
  Serial.printf("[CONFIG] Wi-Fi HTTP=%d\n", code);
  devLog(String("CONFIG | WiFi | HTTP=") + String(code));
  return false;
}

bool readHttpAction(int& statusCode, uint32_t timeoutMs) {
  statusCode = -1;
  String response;
  const uint32_t start = millis();

  while (millis() - start < timeoutMs) {
    while (modem.available()) {
      response += (char)modem.read();
      const int marker = response.indexOf("+HTTPACTION:");
      if (marker >= 0) {
        const int c1 = response.indexOf(',', marker);
        const int c2 = c1 >= 0 ? response.indexOf(',', c1 + 1) : -1;
        if (c1 >= 0 && c2 > c1) {
          statusCode = response.substring(c1 + 1, c2).toInt();
          return true;
        }
      }
      if (response.indexOf("+CME ERROR:") >= 0 ||
          response.indexOf("+CMS ERROR:") >= 0 ||
          response.indexOf("\r\nERROR\r\n") >= 0) return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return false;
}

bool postCellular(const String& url, const String& body, String& response) {
  if (!modemReady || !cellularReady) return false;

  at("AT+HTTPTERM", 1000);
  if (at("AT+HTTPINIT", 3000).indexOf("OK") < 0) return false;
  at("AT+HTTPSSL=1", 3000);
  at("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 3000);

  if (at(String("AT+HTTPPARA=\"URL\",\"") + url + "\"", 5000).indexOf("OK") < 0) {
    at("AT+HTTPTERM", 1000);
    return false;
  }

  const String header = String("Authorization: Bearer ") + smsConfigApiKey();
  if (at(String("AT+HTTPPARA=\"USERDATA\",\"") + header + "\"", 3000).indexOf("OK") < 0) {
    at("AT+HTTPTERM", 1000);
    return false;
  }

  while (modem.available()) modem.read();
  modem.print(String("AT+HTTPDATA=") + String(body.length()) + ",10000\r\n");

  String prompt;
  const uint32_t promptStart = millis();
  while (millis() - promptStart < 5000) {
    while (modem.available()) prompt += (char)modem.read();
    if (prompt.indexOf("DOWNLOAD") >= 0) break;
    if (prompt.indexOf("ERROR") >= 0) {
      at("AT+HTTPTERM", 1000);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  if (prompt.indexOf("DOWNLOAD") < 0) {
    at("AT+HTTPTERM", 1000);
    return false;
  }

  modem.print(body);

  String uploadAck;
  const uint32_t uploadStart = millis();
  while (millis() - uploadStart < 10000) {
    while (modem.available()) uploadAck += (char)modem.read();
    if (uploadAck.indexOf("OK") >= 0 || uploadAck.indexOf("ERROR") >= 0) break;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  if (uploadAck.indexOf("OK") < 0) {
    at("AT+HTTPTERM", 1000);
    return false;
  }

  while (modem.available()) modem.read();
  modem.print("AT+HTTPACTION=1\r\n");

  int statusCode = -1;
  if (!readHttpAction(statusCode, CELLULAR_TIMEOUT_MS) ||
      statusCode < 200 || statusCode >= 300) {
    at("AT+HTTPTERM", 3000);
    Serial.printf("[CONFIG] 4G HTTP=%d\n", statusCode);
    devLog(String("CONFIG | 4G | HTTP=") + String(statusCode));
    return false;
  }

  response = at("AT+HTTPREAD", 5000);
  at("AT+HTTPTERM", 3000);
  return true;
}

bool postRequest(const String& url, const String& body, String& response) {
  if (WiFi.status() == WL_CONNECTED) return postWiFi(url, body, response);
  if (cellularReady) return postCellular(url, body, response);
  return false;
}

bool fetchConfig(const String& url, uint64_t serverTimestamp) {
  const String body = String("{\"action\":\"config\",\"trak_id\":\"") +
                      smsConfigTrakId() +
                      "\",\"last_config_timestamp\":" +
                      String((unsigned long long)smsConfigLastConfigTimestamp()) + "}";

  String response;
  if (!postRequest(url, body, response) || !responseOk(response)) {
    Serial.println("[CONFIG] Recuperation impossible.");
    devLog("CONFIG | fetch ERROR");
    return false;
  }

  bool available = false;
  uint64_t receivedTimestamp = 0;
  String configObject;
  if (!jsonBool(response, "config_available", available) || !available ||
      !jsonUint64(response, "config_updated_at", receivedTimestamp) ||
      receivedTimestamp != serverTimestamp ||
      !jsonObjectForKey(response, "config", configObject)) {
    Serial.println("[CONFIG] Reponse configuration invalide.");
    devLog("CONFIG | invalid config response");
    return false;
  }

  String trakId, trakPhone, userPhone, apiKey, trackserverUrl, dashboardUrl;
  String s1, p1, s2, p2, s3, p3;
  if (!jsonString(configObject, "trak_id", trakId) ||
      !jsonString(configObject, "trak_phone", trakPhone) ||
      !jsonString(configObject, "user_phone", userPhone) ||
      !jsonString(configObject, "api_key", apiKey) ||
      !jsonString(configObject, "trackserver_url", trackserverUrl) ||
      !jsonString(configObject, "dashboard_url", dashboardUrl) ||
      !jsonWifiSlot(configObject, 1, s1, p1) ||
      !jsonWifiSlot(configObject, 2, s2, p2) ||
      !jsonWifiSlot(configObject, 3, s3, p3)) {
    Serial.println("[CONFIG] Champs configuration manquants.");
    devLog("CONFIG | validation ERROR");
    return false;
  }

   Serial.printf("[CONFIG] Nouvelle configuration | TRAK_ID=%s | TRACKSERVER=%s | WIFI1=%s\n",
                trakId.c_str(), trackserverUrl.c_str(), s1.c_str());

  if (!smsConfigApplyRemoteConfig(
          trakId, trakPhone, userPhone, apiKey, trackserverUrl, dashboardUrl,
          s1, p1, s2, p2, s3, p3, receivedTimestamp)) {
    Serial.println("[CONFIG] Application refusee.");
    return false;
  }

  return smsConfigLastConfigTimestamp() == receivedTimestamp;
}

bool checkConfig() {
  const String url = configApiUrl();
  const String trakId = smsConfigTrakId();
  const String apiKey = smsConfigApiKey();
  if (url.isEmpty() || trakId.isEmpty() || apiKey.isEmpty()) return false;

  const String body = String("{\"action\":\"status\",\"trak_id\":\"") + trakId + "\"}";
  String response;
  if (!postRequest(url, body, response) || !responseOk(response)) {
    Serial.println("[CONFIG] STATUS indisponible.");
    devLog("CONFIG | status ERROR");
    return false;
  }

  bool pending = false;
  uint64_t serverTimestamp = 0;
  if (!jsonBool(response, "config_pending", pending) ||
      !jsonUint64(response, "config_updated_at", serverTimestamp)) {
    Serial.println("[CONFIG] STATUS invalide.");
    return false;
  }

  const uint64_t localTimestamp = smsConfigLastConfigTimestamp();
  Serial.printf("[CONFIG] status pending=%d server=%llu local=%llu\n",
                pending ? 1 : 0,
                (unsigned long long)serverTimestamp,
                (unsigned long long)localTimestamp);

  if (serverTimestamp <= localTimestamp) return true;

  if (!fetchConfig(url, serverTimestamp)) return false;

  const String ack = String("{\"action\":\"ack\",\"trak_id\":\"") + smsConfigTrakId() +
                     "\",\"config_updated_at\":" +
                     String((unsigned long long)serverTimestamp) + "}";
  String ackResponse;
  if (!postRequest(url, ack, ackResponse) || !responseOk(ackResponse)) {
    Serial.println("[CONFIG] ACK indisponible.");
    devLog("CONFIG | ACK ERROR");
    return false;
  }

  Serial.println("[CONFIG] Configuration distante appliquee + ACK.");
  devLog("CONFIG | remote apply + ACK OK");
  return true;
}
}

void remoteConfigBegin() {
  lastCheck = 0;
  initialized = true;
  busy = false;
}

void remoteConfigTick() {
  if (!initialized || busy) return;
  if (!smsConfigIsConfigured()) return;

  const uint32_t now = millis();
  const uint32_t interval = (lastCheck == 0) ? FIRST_CHECK_DELAY_MS : CHECK_INTERVAL_MS;
  if (now - lastCheck < interval) return;

  busy = true;
  const bool success = checkConfig();
  busy = false;

  // Controle immediat au boot. En cas d'echec HTTP/reseau, on reessaie
  // rapidement au lieu d'attendre les 5 minutes normales.
  if (success) lastCheck = millis();
  else lastCheck = millis() - (CHECK_INTERVAL_MS - RETRY_INTERVAL_MS);
}
