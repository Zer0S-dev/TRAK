#include <Arduino.h>
#include <HardwareSerial.h>
#include <Adafruit_NeoPixel.h>
#include <SPI.h>
#include <SD.h>
#include <math.h>
#include "Config.h"
#include "TrakRuntime.h"
#include "MotionManager.h"

// Runtime implementation. Public task/init functions must have external
// linkage because they are declared in TrakRuntime.h and called by the .ino.

HardwareSerial modem(1);
Adafruit_NeoPixel leds(WS2812_RING_COUNT + WS2812_CENTER_COUNT, WS2812_PIN, NEO_GRB + NEO_KHZ800);

struct NetworkAPN {
  const char* mccmnc;
  const char* apn;
};

static const NetworkAPN apnDatabase[] = {
  {"20810", "sl2sfr"},
  {"20809", "sl2sfr"},
  {"20815", "free"},
  {"20801", "orange"},
  {"20802", "orange"},
  {"20820", "ebouygtel.com"}
};

static constexpr size_t apnDatabaseSize = sizeof(apnDatabase) / sizeof(apnDatabase[0]);

String detectedApn = DEFAULT_APN;
String trakId;
volatile bool modemReady = false;
volatile bool cellularReady = false;
volatile bool gnssFix = false;
volatile uint32_t centerBlinkUntil = 0;
volatile uint32_t sendIntervalMs = SEND_INTERVAL_MS;
bool devLogReady = false;

enum class HttpPostResult : uint8_t { Ok, ServerError, TransportError };

constexpr uint16_t CENTER_LED = 0;
constexpr uint16_t RING_FIRST = 1;
constexpr uint32_t LED_FRAME_MS = 10;
constexpr uint32_t RING_STEP_MS = 100;
constexpr uint32_t BLINK_MS = 180;
constexpr uint32_t GNSS_LOG_MS = 5000;
constexpr uint32_t CELLULAR_RETRY_MS = 30000;

uint16_t ringIndex = 0;

void devLog(const String& message) {
  if (!DEV_LOG || !devLogReady) return;
  File file = SD.open("/dev.log", FILE_APPEND);
  if (!file) return;
  file.print(millis());
  file.print(" ");
  file.println(message);
  file.close();
}

void initDevLog() {
  if (!DEV_LOG) return;
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, SPI, 10000000)) {
    Serial.println("[DEV-LOG] SD indisponible, dev.log non actif.");
    return;
  }
  devLogReady = true;
  devLog("=== TRAK " TRAK_VERSION " dev.log ===");
  Serial.println("[DEV-LOG] SD active: /dev.log");
}

String readModem(uint32_t timeoutMs) {
  String response;
  response.reserve(256);
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (modem.available()) {
      response += static_cast<char>(modem.read());
      if (response.endsWith("\r\nOK\r\n") || response.endsWith("\nOK\n") || response.endsWith("\r\nERROR\r\n") || response.endsWith("\nERROR\n") || response.indexOf("+CME ERROR:") >= 0 || response.indexOf("+CMS ERROR:") >= 0) return response;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return response;
}

String at(const String& command, uint32_t timeoutMs = MODEM_TIMEOUT_MS) {
  while (modem.available()) modem.read();
  Serial.print("[AT] "); Serial.println(command);
  modem.print(command); modem.print("\r\n");
  String response = readModem(timeoutMs);
  Serial.println(response);
  return response;
}

String waitForHttpAction(uint8_t method, uint32_t timeoutMs) {
  String response;
  response.reserve(256);
  const String prefix = String("+HTTPACTION: ") + String(method) + ",";
  const uint32_t start = millis();
  bool foundPrefix = false;
  while (millis() - start < timeoutMs) {
    while (modem.available()) {
      response += static_cast<char>(modem.read());
      if (!foundPrefix && response.indexOf(prefix) >= 0) foundPrefix = true;
      if (foundPrefix) {
        const int marker = response.indexOf(prefix);
        const int lineEnd = response.indexOf('\n', marker);
        if (marker >= 0 && lineEnd >= 0) return response.substring(marker, lineEnd + 1);
      }
      if (response.length() > 1024) response.remove(0, 512);
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  return response;
}

String waitForResponse(const char* expected, uint32_t timeoutMs) {
  String response;
  response.reserve(256);
  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (modem.available()) {
      response += static_cast<char>(modem.read());
      if (response.indexOf(expected) >= 0) return response;
      if (response.length() > 1024) response.remove(0, 512);
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  return response;
}

bool powerOnModem() {
  Serial.println("[4G] Demarrage du modem A7670...");
  if (at("AT", 1500).indexOf("OK") >= 0) {
    at("ATE0", 1500);
    modemReady = true;
    Serial.println("[4G] Modem deja actif.");
    devLog("Modem deja actif");
    return true;
  }
  pinMode(MODEM_PWRKEY_PIN, OUTPUT);
  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_RESET_PIN, HIGH);
  digitalWrite(MODEM_PWRKEY_PIN, HIGH);
  digitalWrite(MODEM_PWRKEY_PIN, LOW);
  vTaskDelay(pdMS_TO_TICKS(1000));
  digitalWrite(MODEM_PWRKEY_PIN, HIGH);
  vTaskDelay(pdMS_TO_TICKS(5000));
  for (uint8_t i = 0; i < 5; ++i) {
    if (at("AT", 1500).indexOf("OK") >= 0) {
      at("ATE0", 1500);
      modemReady = true;
      Serial.println("[4G] Modem repond.");
      devLog("Modem repond apres demarrage");
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  modemReady = false;
  cellularReady = false;
  Serial.println("[4G] Modem ne repond pas.");
  devLog("ERREUR modem ne repond pas");
  return false;
}

void detectApn() {
  const String response = at("AT+CIMI", 3000);
  String imsi;

  // The A7670 may return unsolicited STK lines (+MSTK) and an error around
  // AT+CIMI. Accept only a standalone 15-digit IMSI line.
  int cursor = 0;
  while (cursor < (int)response.length()) {
    int lineEnd = response.indexOf('\n', cursor);
    if (lineEnd < 0) lineEnd = response.length();
    String line = response.substring(cursor, lineEnd);
    line.trim();
    if (line.length() == 15) {
      bool allDigits = true;
      for (size_t i = 0; i < line.length(); ++i) {
        if (line[i] < '0' || line[i] > '9') { allDigits = false; break; }
      }
      if (allDigits) {
        imsi = line;
        break;
      }
    }
    cursor = lineEnd + 1;
  }

  Serial.print("[AUTO-APN] IMSI : "); Serial.println(imsi.length() ? imsi : "inconnu");
  detectedApn = DEFAULT_APN;
  if (imsi.length() < 5) {
    Serial.print("[AUTO-APN] MCC/MNC introuvable -> APN defaut : "); Serial.println(detectedApn);
    return;
  }

  const String mccmnc = imsi.substring(0, 5);
  Serial.print("[AUTO-APN] MCC/MNC : "); Serial.println(mccmnc);
  for (size_t i = 0; i < apnDatabaseSize; ++i) {
    if (mccmnc.equals(apnDatabase[i].mccmnc)) {
      detectedApn = apnDatabase[i].apn;
      break;
    }
  }
  Serial.print("[AUTO-APN] APN : "); Serial.println(detectedApn);
}

bool attachCellular() {
  if (!modemReady) return false;
  Serial.println("[4G] Activation connexion data...");
  at("AT+CFUN=1", 5000);
  const String attach = at("AT+CGATT=1", 5000);
  if (attach.indexOf("ERROR") >= 0) {
    cellularReady = false;
    Serial.println("[4G] Echec attachement packet domain.");
    devLog("ERREUR attachement data");
    return false;
  }
  at(String("AT+CGDCONT=1,\"IP\",\"") + detectedApn + "\"", 3000);
  const String response = at("AT+CGACT=1,1", 10000);
  cellularReady = response.indexOf("OK") >= 0;
  if (!cellularReady) {
    Serial.println("[4G] Echec activation PDP.");
    devLog("ERREUR activation PDP");
    return false;
  }
  at("AT+CGPADDR=1", 3000);
  Serial.println("[4G] PDP actif.");
  devLog("PDP actif");
  return true;
}

bool configureGnss() {
  Serial.println("[GNSS] Activation GNSS...");
  at("AT+CGNSSPWR=0", 2000);
  const String mode = at("AT+CGNSSMODE=15", 2000);
  if (mode.indexOf("ERROR") >= 0) Serial.println("[GNSS] CGNSSMODE non supporte, on continue.");
  at("AT+CGNSSTST=0", 2000);
  const String response = at("AT+CGNSSPWR=1", 3000);
  if (response.indexOf("OK") < 0) {
    devLog("ERREUR activation GNSS");
    return false;
  }
  Serial.println("[GNSS] GNSS actif.");
  devLog("GNSS actif");
  return true;
}

bool digitsOnly(const String& value) {
  if (value.isEmpty()) return false;
  for (size_t i = 0; i < value.length(); ++i) if (value[i] < '0' || value[i] > '9') return false;
  return true;
}

String isoTimestamp(const String& date, const String& utc) {
  if (date.length() < 6 || utc.length() < 6) return "";
  if (!digitsOnly(date.substring(0, 6)) || !digitsOnly(utc.substring(0, 6))) return "";
  const int day = date.substring(0, 2).toInt();
  const int month = date.substring(2, 4).toInt();
  const int hour = utc.substring(0, 2).toInt();
  const int minute = utc.substring(2, 4).toInt();
  const int second = utc.substring(4, 6).toInt();
  if (day < 1 || day > 31 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 60) return "";
  String out;
  out.reserve(21);
  out += "20"; out += date.substring(4, 6); out += '-'; out += date.substring(2, 4); out += '-'; out += date.substring(0, 2);
  out += 'T'; out += utc.substring(0, 2); out += ':'; out += utc.substring(2, 4); out += ':'; out += utc.substring(4, 6); out += 'Z';
  return out;
}

bool nextField(const String& line, int& cursor, String& value) {
  if (cursor > static_cast<int>(line.length())) return false;
  const int comma = line.indexOf(',', cursor);
  if (comma < 0) { value = line.substring(cursor); cursor = line.length() + 1; }
  else { value = line.substring(cursor, comma); cursor = comma + 1; }
  value.trim();
  return true;
}

double dmToDegrees(double value, bool latitude) {
  const double limit = latitude ? 90.0 : 180.0;
  if (fabs(value) <= limit) return value;
  const double degrees = floor(value / 100.0);
  const double minutes = value - degrees * 100.0;
  if (minutes < 0.0 || minutes >= 60.0 || degrees < 0.0 || degrees > limit) return NAN;
  return degrees + minutes / 60.0;
}

bool readGnss(GnssPosition& p) {
  if (!modemReady) return false;
  const String response = at("AT+CGNSSINFO", 1500);
  const int marker = response.indexOf("+CGNSSINFO:");
  if (marker < 0) return false;
  String line = response.substring(marker + 11);
  const int nl = line.indexOf('\n');
  if (nl >= 0) line = line.substring(0, nl);
  line.trim();
  if (line.isEmpty()) return false;
  String fields[20];
  int cursor = 0;
  uint8_t count = 0;
  while (count < 20 && nextField(line, cursor, fields[count])) ++count;
  if (count < 11) return false;
  int ns = -1, ew = -1;
  for (uint8_t i = 1; i < count; ++i) {
    if (fields[i].equalsIgnoreCase("N") || fields[i].equalsIgnoreCase("S")) ns = i;
    if (fields[i].equalsIgnoreCase("E") || fields[i].equalsIgnoreCase("W")) ew = i;
  }
  if (ns < 1 || ew != ns + 2 || ew + 3 >= count) return false;
  if (fields[0].toInt() <= 0) return false;
  const double rawLat = fields[ns - 1].toDouble();
  const double rawLon = fields[ew - 1].toDouble();
  double lat = dmToDegrees(rawLat, true);
  double lon = dmToDegrees(rawLon, false);
  if (!isfinite(lat) || !isfinite(lon)) return false;
  if (fields[ns].equalsIgnoreCase("S")) lat = -lat;
  if (fields[ew].equalsIgnoreCase("W")) lon = -lon;
  if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return false;
  const String date = fields[ew + 1];
  const String utc = fields[ew + 2];
  const double altitude = fields[ew + 3].toDouble();
  if (!isfinite(altitude)) return false;
  p.valid = true;
  p.latitude = lat;
  p.longitude = lon;
  p.altitude = altitude;
  p.timestamp = isoTimestamp(date, utc);
  return true;
}

String jsonEscape(const String& value) {
  String out;
  out.reserve(value.length() + 4);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '\\' || c == '"') out += '\\';
    out += c;
  }
  return out;
}

String buildJson(const GnssPosition& p) {
  String json;
  json.reserve(270);
  json += "{\"trak_id\":\""; json += jsonEscape(trakId);
  json += "\",\"version\":\""; json += TRAK_VERSION;
  json += "\",\"latitude\":"; json += String(p.latitude, 6);
  json += ",\"longitude\":"; json += String(p.longitude, 6);
  json += ",\"altitude\":"; json += String(p.altitude, 1);
  if (p.timestamp.length()) { json += ",\"timestamp\":\""; json += jsonEscape(p.timestamp); json += '"'; }
  json += ",\"motion\":\""; json += motionModeName(); json += '"';
  json += ",\"motion_return_ms\":"; json += String(motionStationaryConfirmationRemainingMs());
  json += '}';
  return json;
}

bool parseRemoteInterval(const String& response, uint32_t& intervalMs) {
  const int marker = response.indexOf("\"interval_seconds\"");
  if (marker < 0) return false;
  const int colon = response.indexOf(':', marker);
  if (colon < 0) return false;
  int cursor = colon + 1;
  while (cursor < static_cast<int>(response.length()) && (response[cursor] == ' ' || response[cursor] == '\t')) ++cursor;
  int end = cursor;
  while (end < static_cast<int>(response.length()) && response[end] >= '0' && response[end] <= '9') ++end;
  if (end == cursor) return false;
  const int seconds = response.substring(cursor, end).toInt();
  if (seconds != 5 && seconds != 10 && seconds != 15 && seconds != 20 && seconds != 25) return false;
  intervalMs = static_cast<uint32_t>(seconds) * 1000UL;
  return true;
}

bool fetchRemoteInterval() {
  if (!modemReady || !cellularReady) return false;
  String url;
  url.reserve(strlen(TRAK_INTERVAL_URL) + 1);
  url += TRAK_INTERVAL_URL;
  at("AT+HTTPTERM", 1000);
  if (at("AT+HTTPINIT", 3000).indexOf("OK") < 0) return false;
  if (at(String("AT+HTTPPARA=\"URL\",\"") + url + "\"", 3000).indexOf("OK") < 0) { at("AT+HTTPTERM", 1000); return false; }
  while (modem.available()) modem.read();
  modem.print("AT+HTTPACTION=0\r\n");
  const String action = waitForHttpAction(0, 10000);
  const int marker = action.indexOf("+HTTPACTION:");
  if (marker < 0) { at("AT+HTTPTERM", 1000); return false; }
  String result = action.substring(marker + 12); result.trim();
  const int c1 = result.indexOf(',');
  const int c2 = result.indexOf(',', c1 + 1);
  const int status = c1 > 0 ? result.substring(c1 + 1, c2 > 0 ? c2 : result.length()).toInt() : 0;
  Serial.print("[HTTP] Intervalle statut serveur : "); Serial.println(status);
  if (status < 200 || status >= 300) { at("AT+HTTPTERM", 1000); return false; }
  const String body = at("AT+HTTPREAD", 5000);
  at("AT+HTTPTERM", 1000);
  uint32_t newIntervalMs = sendIntervalMs;
  if (!parseRemoteInterval(body, newIntervalMs)) { devLog("ERREUR reponse intervalle distant"); return false; }
  if (newIntervalMs != sendIntervalMs) {
    const uint32_t oldIntervalMs = sendIntervalMs;
    sendIntervalMs = newIntervalMs;
    Serial.print("[TRAK-CONNECT] Nouvel intervalle : "); Serial.print(sendIntervalMs / 1000UL); Serial.println(" s");
    devLog(String("Nouvel intervalle ") + String(oldIntervalMs / 1000UL) + "s -> " + String(sendIntervalMs / 1000UL) + "s");
  }
  return true;
}

HttpPostResult httpPostJson(const String& json) {
  if (!modemReady || !cellularReady) return HttpPostResult::TransportError;
  String url;
  url.reserve(strlen(TRAK_CONNECT_URL) + 1);
  url += TRAK_CONNECT_URL;
  Serial.println("[TRAK-CONNECT] Envoi REST JSON..."); Serial.println(json);
  at("AT+HTTPTERM", 1000);
  if (at("AT+HTTPINIT", 3000).indexOf("OK") < 0) return HttpPostResult::TransportError;
  if (at(String("AT+HTTPPARA=\"URL\",\"") + url + "\"", 3000).indexOf("OK") < 0) { at("AT+HTTPTERM", 1000); return HttpPostResult::TransportError; }
  if (at("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 2000).indexOf("OK") < 0) { at("AT+HTTPTERM", 1000); return HttpPostResult::TransportError; }
  String command = "AT+HTTPDATA="; command += json.length(); command += ",10000";
  while (modem.available()) modem.read();
  modem.print(command); modem.print("\r\n");
  if (waitForResponse("DOWNLOAD", 5000).indexOf("DOWNLOAD") < 0) { at("AT+HTTPTERM", 1000); return HttpPostResult::TransportError; }
  modem.print(json);
  if (waitForResponse("OK", 5000).indexOf("OK") < 0) { at("AT+HTTPTERM", 1000); return HttpPostResult::TransportError; }
  while (modem.available()) modem.read();
  modem.print("AT+HTTPACTION=1\r\n");
  const String action = waitForHttpAction(1, HTTP_TIMEOUT_MS);
  const int marker = action.indexOf("+HTTPACTION:");
  if (marker < 0) { at("AT+HTTPTERM", 1000); return HttpPostResult::TransportError; }
  String result = action.substring(marker + 12); result.trim();
  const int c1 = result.indexOf(',');
  const int c2 = result.indexOf(',', c1 + 1);
  const int status = c1 > 0 ? result.substring(c1 + 1, c2 > 0 ? c2 : result.length()).toInt() : 0;
  Serial.print("[HTTP] Statut serveur : "); Serial.println(status);
  at("AT+HTTPTERM", 1000);
  if (status >= 200 && status < 300) { devLog(String("Position HTTP ") + String(status)); return HttpPostResult::Ok; }
  devLog(String("Position HTTP erreur ") + String(status));
  return HttpPostResult::ServerError;
}

void updateLeds() {
  static uint32_t lastFrame = 0;
  static uint32_t lastRing = 0;
  const uint32_t now = millis();
  if (now - lastFrame < LED_FRAME_MS) return;
  lastFrame = now;
  if (!gnssFix && now - lastRing >= RING_STEP_MS) {
    lastRing = now;
    ++ringIndex;
    if (ringIndex >= WS2812_RING_COUNT) ringIndex = 0;
    for (uint16_t i = 0; i < WS2812_RING_COUNT; ++i) {
      const uint16_t previous = (ringIndex + WS2812_RING_COUNT - 1) % WS2812_RING_COUNT;
      const uint8_t level = i == ringIndex ? 255 : (i == previous ? 90 : 10);
      leds.setPixelColor(RING_FIRST + i, leds.Color(0, 0, level));
    }
  } else if (gnssFix) {
    for (uint16_t i = 0; i < WS2812_RING_COUNT; ++i) leds.setPixelColor(RING_FIRST + i, leds.Color(0, 0, 80));
  }
  const bool blink = centerBlinkUntil > now && ((now / BLINK_MS) & 1U);
  leds.setPixelColor(CENTER_LED, cellularReady ? leds.Color(blink ? 255 : 20, 0, blink ? 255 : 20) : 0);
  leds.show();
}

void trakRuntimeInit() {
  Serial.begin(DEBUG_BAUD);
  vTaskDelay(pdMS_TO_TICKS(1000));
  leds.begin(); leds.setBrightness(WS2812_BRIGHTNESS); leds.clear(); leds.show();
  trakId = trackerSerialNumber();
  initDevLog(); devLog(String("Firmware ") + TRAK_VERSION);
  modem.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
  vTaskDelay(pdMS_TO_TICKS(1000));
  if (!powerOnModem()) return;
  detectApn(); attachCellular(); configureGnss();
}

void trakCommunicationTask(void*) {
  GnssPosition position;
  uint32_t lastGnssPoll = millis() - GNSS_POLL_MS;
  uint32_t lastSend = millis() - SEND_INTERVAL_MS;
  uint32_t lastLog = 0;
  uint32_t lastRecovery = millis();
  uint32_t lastRemoteIntervalPoll = millis() - REMOTE_INTERVAL_POLL_MS;
  for (;;) {
    const uint32_t now = millis();
    motionUpdate(now);
    sendIntervalMs = currentSendIntervalMs();
    if (!modemReady && now - lastRecovery >= CELLULAR_RETRY_MS) {
      lastRecovery = now;
      if (powerOnModem()) { detectApn(); attachCellular(); configureGnss(); }
    } else if (modemReady && !cellularReady && now - lastRecovery >= CELLULAR_RETRY_MS) {
      lastRecovery = now; attachCellular();
    }
    if (cellularReady && now - lastRemoteIntervalPoll >= REMOTE_INTERVAL_POLL_MS) {
      lastRemoteIntervalPoll = now; fetchRemoteInterval();
    }
    if (now - lastGnssPoll >= GNSS_POLL_MS) {
      lastGnssPoll = now;
      GnssPosition next;
      gnssFix = readGnss(next);
      if (gnssFix) {
        position = next;
        if (now - lastLog >= GNSS_LOG_MS) {
          lastLog = now;
          Serial.printf("[GNSS] Fix OK lat=%.6f lon=%.6f alt=%.1f m\n", position.latitude, position.longitude, position.altitude);
        }
      } else if (now - lastLog >= GNSS_LOG_MS) {
        lastLog = now; Serial.println("[GNSS] Recherche du fix...");
      }
    }
    if (gnssFix && cellularReady && now - lastSend >= sendIntervalMs) {
      lastSend = now; centerBlinkUntil = now + 900;
      const HttpPostResult result = httpPostJson(buildJson(position));
      if (result == HttpPostResult::Ok) Serial.println("[TRAK-CONNECT] Position envoyee.");
      else if (result == HttpPostResult::ServerError) Serial.println("[TRAK-CONNECT] Serveur HTTP a refuse la position; 4G conservee.");
      else { cellularReady = false; Serial.println("[TRAK-CONNECT] Echec transport HTTP -> reconnexion 4G programmee."); devLog("Echec transport HTTP -> reconnexion 4G"); }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void trakLedTask(void*) {
  for (;;) { updateLeds(); vTaskDelay(pdMS_TO_TICKS(1)); }
}
