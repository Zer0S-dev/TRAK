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

  // A7670 can return unsolicited STK lines or an error around AT+CIMI.
  // Accept only a standalone 15-digit IMSI; ignore +MSTK, +CME ERROR, etc.
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

  Serial.print("[AUTO-APN] IMSI : ");
  Serial.println(imsi.length() ? imsi : "inconnu");
  detectedApn = DEFAULT_APN;

  if (imsi.length() < 5) {
    Serial.print("[AUTO-APN] MCC/MNC introuvable -> APN defaut : ");
    Serial.println(detectedApn);
    return;
  }

  const String mccmnc = imsi.substring(0, 5);
  Serial.print("[AUTO-APN] MCC/MNC : ");
  Serial.println(mccmnc);
  for (size_t i = 0; i < apnDatabaseSize; ++i) {
    if (mccmnc.equals(apnDatabase[i].mccmnc)) {
      detectedApn = apnDatabase[i].apn;
      break;
    }
  }
  Serial.print("[AUTO-APN] APN : ");
  Serial.println(detectedApn);
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
