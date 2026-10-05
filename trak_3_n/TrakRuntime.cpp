#include <Arduino.h>
#include <WiFi.h>
#include <HardwareSerial.h>
#include <Adafruit_NeoPixel.h>
#include <SPI.h>
#include <SD.h>
#include <math.h>
#include "Config.h"
#include "TrakRuntime.h"
#include "MotionManager.h"
#include "DevLog.h"
#include "RemoteConfigClient.h"

HardwareSerial modem(1);
Adafruit_NeoPixel leds(WS2812_RING_COUNT + WS2812_CENTER_COUNT, WS2812_PIN, NEO_GRB + NEO_KHZ800);

struct NetworkAPN { const char* mccmnc; const char* apn; };
static const NetworkAPN apnDatabase[] = {
  {"20810", "sl2sfr"}, {"20809", "sl2sfr"}, {"20815", "free"},
  {"20801", "orange"}, {"20802", "orange"}, {"20820", "ebouygtel.com"}
};
static constexpr size_t apnDatabaseSize = sizeof(apnDatabase) / sizeof(apnDatabase[0]);
String detectedApn = DEFAULT_APN;
volatile bool modemReady = false;
volatile bool cellularReady = false;
volatile bool gnssFix = false;
volatile uint32_t centerBlinkUntil = 0;
volatile bool centerCommunicationOff = false;
volatile uint32_t sendIntervalMs = SEND_INTERVAL_MS;
bool devLogReady = false;
constexpr uint16_t CENTER_LED = 0;
constexpr uint16_t RING_FIRST = 1;
constexpr uint32_t LED_FRAME_MS = 10;
constexpr uint32_t RING_STEP_MS = 100;
constexpr uint32_t COMMUNICATION_FLASH_MS = 2000;
constexpr uint32_t CELLULAR_RETRY_MS = 30000;
uint16_t ringIndex = 0;

void devLog(const String& message) {
  if (!DEV_LOG || !devLogReady) return;
  File file = SD.open("/dev.log", FILE_APPEND);
  if (!file) return;
  file.print(millis()); file.print(" "); file.println(message); file.close();
}
void initDevLog() {
  if (!DEV_LOG) return;
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, SPI, 10000000)) { Serial.println("[DEV-LOG] SD indisponible, dev.log non actif."); return; }
  devLogReady = true;
  devLog("=== TRAK " TRAK_VERSION " terrain.log ===");
  devLog("BOOT | SD log active | periodic=10s | events=immediate");
  Serial.println("[DEV-LOG] SD active: /dev.log");
}
String readModem(uint32_t timeoutMs) {
  String response; response.reserve(256); const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (modem.available()) { response += static_cast<char>(modem.read()); if (response.endsWith("\r\nOK\r\n") || response.endsWith("\nOK\n") || response.endsWith("\r\nERROR\r\n") || response.endsWith("\nERROR\n") || response.indexOf("+CME ERROR:") >= 0 || response.indexOf("+CMS ERROR:") >= 0) return response; }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return response;
}
String at(const String& command, uint32_t timeoutMs = MODEM_TIMEOUT_MS) {
  while (modem.available()) modem.read(); Serial.print("[AT] "); Serial.println(command); modem.print(command); modem.print("\r\n"); String response = readModem(timeoutMs); Serial.println(response); return response;
}
bool powerOnModem() {
  Serial.println("[4G] Demarrage du modem A7670...");
  if (at("AT", 1500).indexOf("OK") >= 0) { at("ATE0", 1500); modemReady = true; Serial.println("[4G] Modem deja actif."); devLog("Modem deja actif"); return true; }
  pinMode(MODEM_PWRKEY_PIN, OUTPUT); pinMode(MODEM_RESET_PIN, OUTPUT); digitalWrite(MODEM_RESET_PIN, HIGH); digitalWrite(MODEM_PWRKEY_PIN, HIGH); digitalWrite(MODEM_PWRKEY_PIN, LOW); vTaskDelay(pdMS_TO_TICKS(1000)); digitalWrite(MODEM_PWRKEY_PIN, HIGH); vTaskDelay(pdMS_TO_TICKS(5000));
  for (uint8_t i = 0; i < 5; ++i) { if (at("AT", 1500).indexOf("OK") >= 0) { at("ATE0", 1500); modemReady = true; Serial.println("[4G] Modem repond."); devLog("Modem repond apres demarrage"); return true; } vTaskDelay(pdMS_TO_TICKS(500)); }
  modemReady = false; cellularReady = false; Serial.println("[4G] Modem ne repond pas."); devLog("ERREUR modem ne repond pas"); return false;
}
void detectApn() {
  const String response = at("AT+CIMI", 3000); String imsi; int cursor = 0;
  while (cursor < (int)response.length()) { int lineEnd = response.indexOf('\n', cursor); if (lineEnd < 0) lineEnd = response.length(); String line = response.substring(cursor, lineEnd); line.trim(); if (line.length() == 15) { bool allDigits = true; for (size_t i = 0; i < line.length(); ++i) if (line[i] < '0' || line[i] > '9') { allDigits = false; break; } if (allDigits) { imsi = line; break; } } cursor = lineEnd + 1; }
  Serial.print("[AUTO-APN] IMSI : "); Serial.println(imsi.length() ? imsi : "inconnu"); detectedApn = DEFAULT_APN; if (imsi.length() < 5) { Serial.print("[AUTO-APN] MCC/MNC introuvable -> APN defaut : "); Serial.println(detectedApn); return; }
  const String mccmnc = imsi.substring(0, 5); Serial.print("[AUTO-APN] MCC/MNC : "); Serial.println(mccmnc); for (size_t i = 0; i < apnDatabaseSize; ++i) if (mccmnc.equals(apnDatabase[i].mccmnc)) { detectedApn = apnDatabase[i].apn; break; } Serial.print("[AUTO-APN] APN : "); Serial.println(detectedApn);
}
bool attachCellular() {
  if (!modemReady) return false; Serial.println("[4G] Activation connexion data..."); at("AT+CFUN=1", 5000); const String attach = at("AT+CGATT=1", 5000); if (attach.indexOf("ERROR") >= 0) { cellularReady = false; Serial.println("[4G] Echec attachement packet domain."); devLog("ERREUR attachement data"); return false; } at(String("AT+CGDCONT=1,\"IP\",\"") + detectedApn + "\"", 3000); const String response = at("AT+CGACT=1,1", 10000); cellularReady = response.indexOf("OK") >= 0; if (!cellularReady) { Serial.println("[4G] Echec activation PDP."); devLog("ERREUR activation PDP"); return false; } at("AT+CGPADDR=1", 3000); Serial.println("[4G] PDP actif."); devLog("PDP actif"); return true;
}
bool configureGnss() {
  Serial.println("[GNSS] Activation GNSS..."); at("AT+CGNSSPWR=0", 2000); const String mode = at("AT+CGNSSMODE=15", 2000); if (mode.indexOf("ERROR") >= 0) Serial.println("[GNSS] CGNSSMODE non supporte, on continue."); at("AT+CGNSSTST=0", 2000); const String response = at("AT+CGNSSPWR=1", 3000); if (response.indexOf("OK") < 0) { devLog("ERREUR activation GNSS"); return false; } Serial.println("[GNSS] GNSS actif."); devLog("GNSS actif"); return true;
}
bool digitsOnly(const String& value) { if (value.isEmpty()) return false; for (size_t i = 0; i < value.length(); ++i) if (value[i] < '0' || value[i] > '9') return false; return true; }
String isoTimestamp(const String& date, const String& utc) { if (date.length() < 6 || utc.length() < 6) return ""; if (!digitsOnly(date.substring(0, 6)) || !digitsOnly(utc.substring(0, 6))) return ""; const int day = date.substring(0, 2).toInt(), month = date.substring(2, 4).toInt(); const int hour = utc.substring(0, 2).toInt(), minute = utc.substring(2, 4).toInt(), second = utc.substring(4, 6).toInt(); if (day < 1 || day > 31 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 60) return ""; String out; out.reserve(21); out += "20"; out += date.substring(4, 6); out += '-'; out += date.substring(2, 4); out += '-'; out += date.substring(0, 2); out += 'T'; out += utc.substring(0, 2); out += ':'; out += utc.substring(2, 4); out += ':'; out += utc.substring(4, 6); out += 'Z'; return out; }
bool nextField(const String& line, int& cursor, String& value) { if (cursor > static_cast<int>(line.length())) return false; const int comma = line.indexOf(',', cursor); if (comma < 0) { value = line.substring(cursor); cursor = line.length() + 1; } else { value = line.substring(cursor, comma); cursor = comma + 1; } value.trim(); return true; }
double dmToDegrees(double value, bool latitude) { const double limit = latitude ? 90.0 : 180.0; if (fabs(value) <= limit) return value; const double degrees = floor(value / 100.0); const double minutes = value - degrees * 100.0; if (minutes < 0.0 || minutes >= 60.0 || degrees < 0.0 || degrees > limit) return NAN; return degrees + minutes / 60.0; }

bool readGnss(GnssPosition& p) {
  if (!modemReady) return false; p = GnssPosition{}; p.valid = false; const String response = at("AT+CGNSSINFO", 1500); const int marker = response.indexOf("+CGNSSINFO:"); if (marker < 0) { Serial.println("[GNSS] NO FIX | SAT visible=0 | USED=0 | GPS=0 GLO=0 GAL=0 BEI=0"); return false; } String line = response.substring(marker + 11); const int nl = line.indexOf('\n'); if (nl >= 0) line = line.substring(0, nl); line.trim(); if (line.isEmpty()) { Serial.println("[GNSS] NO FIX | SAT visible=0 | USED=0 | GPS=0 GLO=0 GAL=0 BEI=0"); return false; }
  String fields[20]; int cursor = 0; uint8_t count = 0; while (count < 20 && nextField(line, cursor, fields[count])) ++count; if (count < 5) { Serial.println("[GNSS] NO FIX | SAT visible=0 | USED=0 | GPS=0 GLO=0 GAL=0 BEI=0"); return false; }
  p.fixMode = (count > 0 && fields[0].length()) ? (uint8_t)constrain(fields[0].toInt(), 0, 255) : 0;
  p.gpsSatellites = (count > 1) ? (uint8_t)constrain(fields[1].toInt(), 0, 255) : 0; p.glonassSatellites = (count > 2 && fields[2].length()) ? (uint8_t)constrain(fields[2].toInt(), 0, 255) : 0; p.galileoSatellites = (count > 3 && fields[3].length()) ? (uint8_t)constrain(fields[3].toInt(), 0, 255) : 0; p.beidouSatellites = (count > 4 && fields[4].length()) ? (uint8_t)constrain(fields[4].toInt(), 0, 255) : 0; p.totalSatellites = p.gpsSatellites + p.glonassSatellites + p.galileoSatellites + p.beidouSatellites;
  if (count > 12 && fields[count - 1].length() && digitsOnly(fields[count - 1])) { const int used = fields[count - 1].toInt(); if (used >= 0 && used <= 255) p.usedSatellites = (uint8_t)used; }
  int ns = -1, ew = -1; for (uint8_t i = 1; i < count; ++i) { if (fields[i].equalsIgnoreCase("N") || fields[i].equalsIgnoreCase("S")) ns = i; if (fields[i].equalsIgnoreCase("E") || fields[i].equalsIgnoreCase("W")) ew = i; }
  if (ns < 1 || ew != ns + 2 || ew + 3 >= count || fields[0].toInt() <= 0) { Serial.println("[GNSS] NO FIX | SAT visible=0 | USED=0 | GPS=0 GLO=0 GAL=0 BEI=0"); return true; } const double rawLat = fields[ns - 1].toDouble(), rawLon = fields[ew - 1].toDouble(); double lat = dmToDegrees(rawLat, true), lon = dmToDegrees(rawLon, false); if (!isfinite(lat) || !isfinite(lon)) { Serial.println("[GNSS] NO FIX | SAT visible=0 | USED=0 | GPS=0 GLO=0 GAL=0 BEI=0"); return true; } if (fields[ns].equalsIgnoreCase("S")) lat = -lat; if (fields[ew].equalsIgnoreCase("W")) lon = -lon; if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) { Serial.println("[GNSS] NO FIX | SAT visible=0 | USED=0 | GPS=0 GLO=0 GAL=0 BEI=0"); return true; } const String date = fields[ew + 1], utc = fields[ew + 2]; const double altitude = fields[ew + 3].toDouble(); if (!isfinite(altitude)) { Serial.println("[GNSS] NO FIX | SAT visible=0 | USED=0 | GPS=0 GLO=0 GAL=0 BEI=0"); return true; }
  p.latitude = lat; p.longitude = lon; p.altitude = altitude; p.timestamp = isoTimestamp(date, utc);
  p.speedKnots = (ew + 4 < count && fields[ew + 4].length()) ? fields[ew + 4].toFloat() : 0.0f;
  p.courseDeg = (ew + 5 < count && fields[ew + 5].length()) ? fields[ew + 5].toFloat() : 0.0f;
  p.pdop = (ew + 6 < count && fields[ew + 6].length()) ? fields[ew + 6].toFloat() : 0.0f;
  p.hdop = (ew + 7 < count && fields[ew + 7].length()) ? fields[ew + 7].toFloat() : 0.0f;
  p.vdop = (ew + 8 < count && fields[ew + 8].length()) ? fields[ew + 8].toFloat() : 0.0f;
  p.valid = (p.fixMode == 2 || p.fixMode == 3);
  if (!p.valid) { Serial.println("[GNSS] NO FIX | mode invalide"); return false; }
  Serial.printf("[GNSS] FIX | mode=%u | SAT visible=%u | USED=%u | GPS=%u GLO=%u GAL=%u BEI=%u | hdop=%.2f pdop=%.2f speed=%.2fkn course=%.1f\n", p.fixMode, p.totalSatellites, p.usedSatellites, p.gpsSatellites, p.glonassSatellites, p.galileoSatellites, p.beidouSatellites, p.hdop, p.pdop, p.speedKnots, p.courseDeg); return true;
}
void signalCommunicationSend() {
  // Extinction physique immédiate. Le flag empêche updateLeds() de rallumer la LED.
  centerCommunicationOff = true;
  centerBlinkUntil = millis() + COMMUNICATION_FLASH_MS;
  leds.setPixelColor(CENTER_LED, 0);
  leds.show();

  Serial.printf("[LED] DATA SEND -> OFF %lu ms\n", (unsigned long)COMMUNICATION_FLASH_MS);
}

void updateLeds() { static uint32_t lastFrame = 0, lastRing = 0; const uint32_t now = millis(); if (now - lastFrame < LED_FRAME_MS) return; lastFrame = now; if (!gnssFix && now - lastRing >= RING_STEP_MS) { lastRing = now; ++ringIndex; if (ringIndex >= WS2812_RING_COUNT) ringIndex = 0; for (uint16_t i = 0; i < WS2812_RING_COUNT; ++i) { const uint16_t previous = (ringIndex + WS2812_RING_COUNT - 1) % WS2812_RING_COUNT; const uint8_t level = i == ringIndex ? 255 : (i == previous ? 90 : 10); leds.setPixelColor(RING_FIRST + i, leds.Color(0, 0, level)); } } else if (gnssFix) for (uint16_t i = 0; i < WS2812_RING_COUNT; ++i) leds.setPixelColor(RING_FIRST + i, leds.Color(0, 0, 80)); if (centerCommunicationOff) {
    if (now < centerBlinkUntil) {
      leds.setPixelColor(CENTER_LED, 0);
    } else {
      centerCommunicationOff = false;
      leds.setPixelColor(CENTER_LED, (cellularReady || WiFi.status() == WL_CONNECTED) ? leds.Color(20, 0, 20) : 0);
    }
  } else {
    leds.setPixelColor(CENTER_LED, (cellularReady || WiFi.status() == WL_CONNECTED) ? leds.Color(20, 0, 20) : 0);
  }
  leds.show();
}
void trakRuntimeInit() { Serial.begin(DEBUG_BAUD); vTaskDelay(pdMS_TO_TICKS(1000)); leds.begin(); leds.setBrightness(WS2812_BRIGHTNESS); leds.clear(); leds.show(); initDevLog(); devLog(String("Firmware ") + TRAK_VERSION); modem.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN); vTaskDelay(pdMS_TO_TICKS(1000)); if (!powerOnModem()) return; detectApn(); attachCellular(); configureGnss(); }
void trakCommunicationTask(void*) { for (;;) { vTaskDelay(pdMS_TO_TICKS(100)); } }
void trakLedTask(void*) { for (;;) { updateLeds(); vTaskDelay(pdMS_TO_TICKS(1)); } }
