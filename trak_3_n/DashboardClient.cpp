#include "DashboardClient.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HardwareSerial.h>
#include "SmsConfigManager.h"

extern HardwareSerial modem;
extern volatile bool modemReady;
extern volatile bool cellularReady;
extern String at(const String& command, uint32_t timeoutMs);
extern void devLog(const String& message);

namespace {
constexpr uint32_t HTTP_TIMEOUT_MS = 5000;
constexpr size_t MAX_URL_LEN = 160;

String jsonEscape(const String& value) {
  String out;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '\\') out += "\\\\";
    else if (c == '"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  return out;
}

String payload(const GnssPosition& p) {
  String body = "{\"trak_id\":\"";
  body += jsonEscape(smsConfigTrakId());
  body += "\",\"api_key\":\"";
  body += jsonEscape(smsConfigApiKey());
  body += "\",\"lat\":";
  body += String(p.latitude, 6);
  body += ",\"lon\":";
  body += String(p.longitude, 6);
  body += ",\"timestamp\":\"";
  body += jsonEscape(p.timestamp);
  body += "\"}";
  return body;
}

bool httpAction(int& statusCode, uint32_t timeoutMs) {
  statusCode = -1;
  String response;
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (modem.available()) {
      response += static_cast<char>(modem.read());
      const int marker = response.indexOf("+HTTPACTION:");
      if (marker >= 0) {
        const int c1 = response.indexOf(',', marker);
        const int c2 = c1 >= 0 ? response.indexOf(',', c1 + 1) : -1;
        if (c1 >= 0 && c2 > c1) {
          statusCode = response.substring(c1 + 1, c2).toInt();
          return true;
        }
      }
      if (response.indexOf("ERROR") >= 0) return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return false;
}

bool waitDownload(uint32_t timeoutMs) {
  String response;
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (modem.available()) {
      response += static_cast<char>(modem.read());
      if (response.indexOf("DOWNLOAD") >= 0) return true;
      if (response.indexOf("ERROR") >= 0) return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return false;
}

DashboardResult wifiPost(const String& url, const String& body) {
  if (WiFi.status() != WL_CONNECTED) return DashboardResult::NotReady;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) return DashboardResult::Failed;
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(body);
  http.end();
  if (code >= 200 && code < 300) {
    Serial.printf("[DASHBOARD] Wi-Fi POST OK | HTTP=%d\n", code);
    devLog(String("DASHBOARD | WiFi | HTTP=") + String(code) + " | OK");
    return DashboardResult::Success;
  }
  Serial.printf("[DASHBOARD] Wi-Fi POST ERROR | HTTP=%d\n", code);
  devLog(String("DASHBOARD | WiFi | HTTP=") + String(code) + " | ERROR");
  return DashboardResult::Failed;
}

DashboardResult cellularPost(const String& url, const String& body) {
  if (!modemReady || !cellularReady) return DashboardResult::NotReady;
  at("AT+HTTPTERM", 1000);
  if (at("AT+HTTPINIT", 3000).indexOf("OK") < 0) return DashboardResult::Failed;
  at("AT+HTTPSSL=1", 3000);
  if (at(String("AT+HTTPPARA=\"URL\",\"") + url + "\"", 5000).indexOf("OK") < 0) {
    at("AT+HTTPTERM", 1000);
    return DashboardResult::Failed;
  }
  if (at("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 3000).indexOf("OK") < 0) {
    at("AT+HTTPTERM", 1000);
    return DashboardResult::Failed;
  }
  while (modem.available()) modem.read();
  modem.print("AT+HTTPDATA=");
  modem.print(body.length());
  modem.print(",10000\r\n");
  if (!waitDownload(5000)) {
    at("AT+HTTPTERM", 1000);
    return DashboardResult::Failed;
  }
  modem.print(body);
  String dataResponse;
  const uint32_t start = millis();
  while (millis() - start < 12000) {
    while (modem.available()) dataResponse += static_cast<char>(modem.read());
    if (dataResponse.indexOf("OK") >= 0 || dataResponse.indexOf("ERROR") >= 0) break;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  if (dataResponse.indexOf("OK") < 0) {
    at("AT+HTTPTERM", 1000);
    return DashboardResult::Failed;
  }
  while (modem.available()) modem.read();
  modem.print("AT+HTTPACTION=1\r\n");
  int statusCode = -1;
  const bool ok = httpAction(statusCode, 30000);
  at("AT+HTTPTERM", 3000);
  if (ok && statusCode >= 200 && statusCode < 300) {
    Serial.printf("[DASHBOARD] 4G POST OK | HTTP=%d\n", statusCode);
    devLog(String("DASHBOARD | 4G | HTTP=") + String(statusCode) + " | OK");
    return DashboardResult::Success;
  }
  Serial.printf("[DASHBOARD] 4G POST ERROR | HTTP=%d\n", statusCode);
  devLog(String("DASHBOARD | 4G | HTTP=") + String(statusCode) + " | ERROR");
  return DashboardResult::Failed;
}
}

void dashboardBegin() {
  const String url = smsConfigDashboardUrl();
  if (url.isEmpty()) Serial.println("[DASHBOARD] Pas encore configure.");
  else Serial.printf("[DASHBOARD] URL configuree : %s\n", url.c_str());
}

DashboardResult dashboardSendPosition(const GnssPosition& position) {
  const String url = smsConfigDashboardUrl();
  if (!smsConfigIsConfigured() || url.isEmpty() || !position.valid) return DashboardResult::NotReady;
  if (url.length() > MAX_URL_LEN || !url.startsWith("https://")) return DashboardResult::Failed;

  const String body = payload(position);
  Serial.printf("[DASHBOARD] POST %s | lat=%.6f lon=%.6f\n", url.c_str(), position.latitude, position.longitude);
  if (WiFi.status() == WL_CONNECTED) {
    const DashboardResult wifiResult = wifiPost(url, body);
    if (wifiResult == DashboardResult::Success ||
        wifiResult == DashboardResult::NotReady ||
        !cellularReady) {
      return wifiResult;
    }

    Serial.println("[DASHBOARD] Wi-Fi echec -> bascule immediate 4G");
    devLog("DASHBOARD | WiFi failed | fallback 4G");
    return cellularPost(url, body);
  }

  if (cellularReady) return cellularPost(url, body);
  return DashboardResult::NotReady;
}
