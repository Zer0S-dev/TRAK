#include "TrackserverClient.h"

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
constexpr uint32_t WIFI_HTTP_TIMEOUT_MS = 10000;
constexpr uint32_t CELLULAR_HTTP_TIMEOUT_MS = 30000;
constexpr size_t MAX_URL_LEN = 160;

String urlEncode(const String& value) {
  const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(value.length() + 16);
  for (size_t i = 0; i < value.length(); ++i) {
    const unsigned char c = static_cast<unsigned char>(value[i]);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

String buildUrl(const GnssPosition& position) {
  String url = smsConfigTrackserverUrl();
  if (url.isEmpty() || url.length() > MAX_URL_LEN) return "";

  url.replace("{0}", String(position.latitude, 6));
  url.replace("{1}", String(position.longitude, 6));
  url.replace("{2}", urlEncode(position.timestamp));
  url.replace("{4}", String(position.altitude, 1));
  url.replace("{5}", String(position.speedKnots, 2));
  url.replace("{6}", String(position.courseDeg, 1));
  return url;
}

bool readHttpAction(int& statusCode, uint32_t timeoutMs) {
  statusCode = -1;
  String response;
  response.reserve(128);
  const uint32_t start = millis();

  while (millis() - start < timeoutMs) {
    while (modem.available()) {
      response += static_cast<char>(modem.read());
      const int marker = response.indexOf("+HTTPACTION:");
      if (marker >= 0) {
        const int firstComma = response.indexOf(',', marker);
        const int secondComma = firstComma >= 0 ? response.indexOf(',', firstComma + 1) : -1;
        if (firstComma >= 0 && secondComma > firstComma) {
          statusCode = response.substring(firstComma + 1, secondComma).toInt();
          return true;
        }
      }
      if (response.indexOf("+CME ERROR:") >= 0 ||
          response.indexOf("+CMS ERROR:") >= 0 ||
          response.indexOf("\r\nERROR\r\n") >= 0) {
        return false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return false;
}

TrackserverResult sendOverWiFi(const String& url) {
  if (WiFi.status() != WL_CONNECTED) return TrackserverResult::NotReady;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(WIFI_HTTP_TIMEOUT_MS);
  http.setTimeout(WIFI_HTTP_TIMEOUT_MS);

  if (!http.begin(client, url)) {
    Serial.println("[TRACKSERVER] Wi-Fi HTTP begin ERROR");
    devLog("TRACKSERVER | WiFi | begin ERROR");
    return TrackserverResult::Failed;
  }

  const int code = http.GET();
  http.end();

  if (code >= 200 && code < 300) {
    Serial.printf("[TRACKSERVER] Wi-Fi GET OK | HTTP=%d\n", code);
    devLog(String("TRACKSERVER | WiFi | HTTP=") + String(code) + " | OK");
    return TrackserverResult::Success;
  }

  Serial.printf("[TRACKSERVER] Wi-Fi GET ERROR | HTTP=%d\n", code);
  devLog(String("TRACKSERVER | WiFi | HTTP=") + String(code) + " | ERROR");
  return TrackserverResult::Failed;
}

TrackserverResult sendOverCellular(const String& url) {
  if (!modemReady || !cellularReady) return TrackserverResult::NotReady;

  // A7670 HTTPS HTTP stack. All commands are executed in the same
  // communication task as GNSS/modem access, so there is no concurrent
  // access to the modem UART.
  at("AT+HTTPTERM", 1000);
  const String init = at("AT+HTTPINIT", 3000);
  if (init.indexOf("OK") < 0) {
    Serial.println("[TRACKSERVER] 4G HTTPINIT ERROR");
    devLog("TRACKSERVER | 4G | HTTPINIT ERROR");
    return TrackserverResult::Failed;
  }

  at("AT+HTTPSSL=1", 3000);
  const String urlCommand = String("AT+HTTPPARA=\"URL\",\"") + url + "\"";
  if (at(urlCommand, 5000).indexOf("OK") < 0) {
    at("AT+HTTPTERM", 1000);
    Serial.println("[TRACKSERVER] 4G HTTP URL ERROR");
    devLog("TRACKSERVER | 4G | URL ERROR");
    return TrackserverResult::Failed;
  }

  while (modem.available()) modem.read();
  modem.print("AT+HTTPACTION=0\r\n");

  int statusCode = -1;
  const bool gotAction = readHttpAction(statusCode, CELLULAR_HTTP_TIMEOUT_MS);
  at("AT+HTTPTERM", 3000);

  if (gotAction && statusCode >= 200 && statusCode < 300) {
    Serial.printf("[TRACKSERVER] 4G GET OK | HTTP=%d\n", statusCode);
    devLog(String("TRACKSERVER | 4G | HTTP=") + String(statusCode) + " | OK");
    return TrackserverResult::Success;
  }

  Serial.printf("[TRACKSERVER] 4G GET ERROR | HTTP=%d\n", statusCode);
  devLog(String("TRACKSERVER | 4G | HTTP=") + String(statusCode) + " | ERROR");
  return TrackserverResult::Failed;
}
}

void trackserverBegin() {
  const String url = smsConfigTrackserverUrl();
  if (smsConfigIsConfigured()) {
    Serial.printf("[TRACKSERVER] URL configuree : %s\n", url.c_str());
    devLog(String("TRACKSERVER | configured | url=") + url);
  } else {
    Serial.println("[TRACKSERVER] Pas encore configure.");
  }
}

TrackserverResult trackserverSend(const GnssPosition& position) {
  if (!smsConfigIsConfigured() || !position.valid) {
    return TrackserverResult::NotReady;
  }

  const String url = buildUrl(position);
  if (url.isEmpty()) {
    Serial.println("[TRACKSERVER] URL invalide ou absente.");
    devLog("TRACKSERVER | URL invalide");
    return TrackserverResult::Failed;
  }

  Serial.printf("[TRACKSERVER] GET %s\n", url.c_str());

  if (WiFi.status() == WL_CONNECTED) {
    return sendOverWiFi(url);
  }

  if (cellularReady) {
    return sendOverCellular(url);
  }

  return TrackserverResult::NotReady;
}

TrackserverResult trackserverSend(const GnssPosition& position, const String&) {
  return trackserverSend(position);
}
