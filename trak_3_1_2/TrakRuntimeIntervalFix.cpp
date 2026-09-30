#include <Arduino.h>
#include <HardwareSerial.h>
#include "Config.h"
#include "TrakRuntime.h"
#include "PositionBuffer.h"
#include "MotionManager.h"
#include "WiFiManager.h"
#include "TrakConfig.h"
#include "WizardManager.h"

extern HardwareSerial modem;
extern volatile bool modemReady;
extern volatile bool cellularReady;
extern volatile bool gnssFix;
extern volatile uint32_t centerBlinkUntil;
extern volatile uint32_t sendIntervalMs;
extern bool powerOnModem();
extern void detectApn();
extern bool attachCellular();
extern bool configureGnss();
extern bool readGnss(GnssPosition& p);
extern String buildJson(const GnssPosition& p);
extern String at(const String& command, uint32_t timeoutMs);
extern String waitForHttpAction(uint8_t method, uint32_t timeoutMs);
enum class HttpPostResult : uint8_t;
extern HttpPostResult httpPostJson(const String& json);
extern void devLog(const String& message);

enum class NetworkPath : uint8_t { None, WiFi, Cellular };
static PositionBuffer positionBuffer;
static bool bufferReady = false;
static volatile NetworkPath activeNetwork = NetworkPath::None;

constexpr uint32_t GNSS_LOG_MS = 5000;
constexpr uint32_t CELLULAR_RETRY_MS = 30000;
constexpr uint32_t BUFFER_RETRY_MS = 200;
constexpr uint32_t WIFI_PROFILE_SYNC_MS = 30000;
constexpr uint32_t CELLULAR_SIGNAL_POLL_MS = 10000;
static int cachedCellularSignalPercent = -1;
static uint32_t lastCellularSignalPoll = 0;
static constexpr uint32_t TERRAIN_LOG_MS = 10000;

static const char* terrainNetworkName(NetworkPath path) {
  return path == NetworkPath::WiFi ? "WiFi" : path == NetworkPath::Cellular ? "4G" : "None";
}

static void terrainLogSnapshot(const GnssPosition& position, NetworkPath network, size_t fifoCount) {
  const MotionState& ms = motionState();
  String line;
  line.reserve(280);
  line += "STATUS | net="; line += terrainNetworkName(network);
  line += " | fix="; line += gnssFix ? "1" : "0";
  line += " | mode="; line += String(position.fixMode);
  line += " | sat="; line += String(position.totalSatellites);
  line += " used="; line += String(position.usedSatellites);
  line += " gps="; line += String(position.gpsSatellites);
  line += " glo="; line += String(position.glonassSatellites);
  line += " gal="; line += String(position.galileoSatellites);
  line += " bei="; line += String(position.beidouSatellites);
  if (position.valid) {
    line += " | lat="; line += String(position.latitude, 6);
    line += " lon="; line += String(position.longitude, 6);
    line += " alt="; line += String(position.altitude, 1);
    line += " | hdop="; line += String(position.hdop, 2);
    line += " pdop="; line += String(position.pdop, 2);
    line += " vdop="; line += String(position.vdop, 2);
    line += " | speed="; line += String(position.speedKnots, 2);
    line += "kn course="; line += String(position.courseDeg, 1);
  }
  line += " | motion="; line += motionIsMobile() ? "MOBILE" : "IMMOBILE";
  line += " | gyro="; line += String(ms.motionDps, 2);
  line += "dps | fifo="; line += String((unsigned)fifoCount);
  line += " | interval="; line += String((unsigned)(sendIntervalMs / 1000UL)); line += "s";
  if (network == NetworkPath::Cellular && cachedCellularSignalPercent >= 0) {
    line += " | csq="; line += String(cachedCellularSignalPercent); line += "%";
  }
  devLog(line);
}

uint8_t trakActiveNetworkCode() {
  switch (activeNetwork) {
    case NetworkPath::WiFi: return 1;
    case NetworkPath::Cellular: return 2;
    default: return 0;
  }
}

static int readCellularSignalPercent() {
  const uint32_t now = millis();
  if (cachedCellularSignalPercent >= 0 && now - lastCellularSignalPoll < CELLULAR_SIGNAL_POLL_MS) return cachedCellularSignalPercent;
  if (!modemReady || !cellularReady) return -1;
  lastCellularSignalPoll = now;
  const String response = at("AT+CSQ", 1500);
  const int marker = response.indexOf("+CSQ:");
  if (marker < 0) return cachedCellularSignalPercent;
  int cursor = marker + 5;
  while (cursor < (int)response.length() && (response[cursor] == ' ' || response[cursor] == '\t')) ++cursor;
  int end = cursor;
  while (end < (int)response.length() && response[end] >= '0' && response[end] <= '9') ++end;
  if (end == cursor) return cachedCellularSignalPercent;
  const int rssi = response.substring(cursor, end).toInt();
  if (rssi == 99 || rssi < 0 || rssi > 31) return -1;
  cachedCellularSignalPercent = (rssi * 100 + 15) / 31;
  Serial.printf("[4G] Signal CSQ=%d -> %d%%\n", rssi, cachedCellularSignalPercent);
  devLog(String("4G signal: CSQ=") + String(rssi) + " -> " + String(cachedCellularSignalPercent) + "%");
  return cachedCellularSignalPercent;
}

static String addTransportToJson(String json, NetworkPath path) {
  const int end = json.lastIndexOf('}');
  if (end < 0) return json;
  json.remove(end);
  json += ",\"network\":\"";
  json += path == NetworkPath::WiFi ? "WiFi" : "4G";
  json += "\"";
  if (path == NetworkPath::Cellular) {
    const int signalPercent = readCellularSignalPercent();
    if (signalPercent >= 0) { json += ",\"signal_percent\":"; json += String(signalPercent); }
  }
  json += "}";
  return json;
}

static int postBufferedPosition(const GnssPosition& position, int& httpStatus) {
  NetworkPath transport = NetworkPath::None;
  if (wifiIsActive()) { activeNetwork = NetworkPath::WiFi; transport = NetworkPath::WiFi; }
  else if (cellularReady) { activeNetwork = NetworkPath::Cellular; transport = NetworkPath::Cellular; }
  if (transport == NetworkPath::None) { httpStatus = 0; return 2; }
  String json = addTransportToJson(buildJson(position), transport);
  if (transport == NetworkPath::WiFi) {
    const bool ok = wifiPostJson(json, httpStatus);
    if (ok) return 0;
    if (httpStatus == 0) return 2;
    return 1;
  }
  httpStatus = 0;
  return static_cast<int>(httpPostJson(json));
}

bool trakPositionBufferInit() {
  if (bufferReady) return true;
  bufferReady = positionBuffer.begin();
  if (bufferReady) {
    Serial.printf("[BUFFER] FIFO SD actif: %u position(s) restauree(s)\n", (unsigned)positionBuffer.size());
    devLog(String("Buffer SD ready: count=") + String((unsigned)positionBuffer.size()));
  }
  return bufferReady;
}

static String readHttpBody(uint32_t timeoutMs) {
  while (modem.available()) modem.read();
  modem.print("AT+HTTPREAD=0,2048\r\n");
  String response; response.reserve(2200); const uint32_t start = millis(); bool started = false;
  while (millis() - start < timeoutMs) {
    while (modem.available()) {
      response += static_cast<char>(modem.read());
      if (response.indexOf("+HTTPREAD:") >= 0) started = true;
      if (started && response.indexOf("+HTTPREAD: 0") >= 0) return response;
      if (response.indexOf("\r\nERROR\r\n") >= 0) return response;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  return response;
}

static bool parseUnsignedField(const String& response, const char* key, uint32_t& value) {
  const int marker = response.indexOf(key); if (marker < 0) return false;
  const int colon = response.indexOf(':', marker); if (colon < 0) return false;
  int cursor = colon + 1; while (cursor < (int)response.length() && (response[cursor] == ' ' || response[cursor] == '\t')) ++cursor;
  int end = cursor; while (end < (int)response.length() && response[end] >= '0' && response[end] <= '9') ++end;
  if (end == cursor) return false; value = (uint32_t)response.substring(cursor, end).toInt(); return true;
}

static bool parseRemoteSettings(const String& response, uint32_t& activeSec, uint32_t& idleSec, uint8_t& sensitivity) {
  uint32_t active = 0, idle = 0, sens = 0;
  if (!parseUnsignedField(response, "\"active_interval_seconds\"", active)) return false;
  if (!parseUnsignedField(response, "\"idle_interval_seconds\"", idle)) return false;
  if (!parseUnsignedField(response, "\"sensitivity_level\"", sens)) return false;
  if (!(active == 5 || active == 10 || active == 15 || active == 20)) return false;
  if (!(idle == 30 || idle == 60 || idle == 900 || idle == 1800 || idle == 3600)) return false;
  if (sens < 1 || sens > 5) return false;
  activeSec = active; idleSec = idle; sensitivity = (uint8_t)sens; return true;
}

static bool fetchRemoteIntervalFixed() {
  if (!modemReady || !cellularReady) return false;
  String url = String(TRAK_INTERVAL_URL);
  modem.print("AT+HTTPTERM\r\n"); vTaskDelay(pdMS_TO_TICKS(100)); while (modem.available()) modem.read();
  modem.print("AT+HTTPINIT\r\n"); String init; const uint32_t initStart = millis();
  while (millis() - initStart < 3000) { while (modem.available()) init += (char)modem.read(); if (init.indexOf("OK") >= 0 || init.indexOf("ERROR") >= 0) break; vTaskDelay(pdMS_TO_TICKS(2)); }
  if (init.indexOf("OK") < 0) return false;
  modem.print(String("AT+HTTPPARA=\"URL\",\"") + url + "\"\r\n"); String para; const uint32_t paraStart = millis();
  while (millis() - paraStart < 3000) { while (modem.available()) para += (char)modem.read(); if (para.indexOf("OK") >= 0 || para.indexOf("ERROR") >= 0) break; vTaskDelay(pdMS_TO_TICKS(2)); }
  if (para.indexOf("OK") < 0) return false;
  while (modem.available()) modem.read(); modem.print("AT+HTTPACTION=0\r\n");
  const String action = waitForHttpAction(0, 10000); const int marker = action.indexOf("+HTTPACTION:"); if (marker < 0) return false;
  String result = action.substring(marker + 12); result.trim(); const int c1 = result.indexOf(','); const int c2 = result.indexOf(',', c1 + 1); const int status = c1 > 0 ? result.substring(c1 + 1, c2 > 0 ? c2 : result.length()).toInt() : 0;
  if (status < 200 || status >= 300) { modem.print("AT+HTTPTERM\r\n"); return false; }
  const String body = readHttpBody(5000); modem.print("AT+HTTPTERM\r\n");
  uint32_t activeSec = 0, idleSec = 0; uint8_t sensitivity = 0;
  if (!parseRemoteSettings(body, activeSec, idleSec, sensitivity)) return false;
  const uint32_t oldActive = motionActiveIntervalSec(), oldIdle = motionIdleIntervalSec(); const uint8_t oldSensitivity = motionSensitivityLevel();
  if ((activeSec != oldActive || idleSec != oldIdle) && setMotionIntervals(activeSec, idleSec)) devLog(String("Remote intervals: ") + String(oldActive) + "s/" + String(oldIdle) + "s -> " + String(activeSec) + "s/" + String(idleSec) + "s");
  if (sensitivity != oldSensitivity && setMotionSensitivityLevel(sensitivity)) devLog(String("Remote gyro sensitivity: ") + String(oldSensitivity) + " -> " + String(sensitivity));
  return true;
}

static bool syncWifiProfilesVia4G() {
  if (!modemReady || !cellularReady) return false;
  const String url = trakWebAppUrl() + "api/wifi/";
  modem.print("AT+HTTPTERM\r\n"); vTaskDelay(pdMS_TO_TICKS(50)); while (modem.available()) modem.read();
  modem.print("AT+HTTPINIT\r\n");
  String init = ""; const uint32_t a = millis(); while (millis() - a < 3000) { while (modem.available()) init += (char)modem.read(); if (init.indexOf("OK") >= 0 || init.indexOf("ERROR") >= 0) break; vTaskDelay(pdMS_TO_TICKS(2)); }
  if (init.indexOf("OK") < 0) return false;
  modem.print(String("AT+HTTPPARA=\"URL\",\"") + url + "\"\r\n"); String para = ""; const uint32_t b = millis(); while (millis() - b < 3000) { while (modem.available()) para += (char)modem.read(); if (para.indexOf("OK") >= 0 || para.indexOf("ERROR") >= 0) break; vTaskDelay(pdMS_TO_TICKS(2)); }
  if (para.indexOf("OK") < 0) return false;
  while (modem.available()) modem.read(); modem.print("AT+HTTPACTION=0\r\n"); const String action = waitForHttpAction(0, 10000); const int marker = action.indexOf("+HTTPACTION:"); if (marker < 0) return false;
  String result = action.substring(marker + 12); result.trim(); const int c1 = result.indexOf(','); const int c2 = result.indexOf(',', c1 + 1); const int status = c1 > 0 ? result.substring(c1 + 1, c2 > 0 ? c2 : result.length()).toInt() : 0;
  if (status < 200 || status >= 300) { modem.print("AT+HTTPTERM\r\n"); return false; }
  const String body = readHttpBody(5000); modem.print("AT+HTTPTERM\r\n");
  for (uint8_t slot = 0; slot < 3; ++slot) {
    const String key = String("\"slot\":") + slot; const int pos = body.indexOf(key); if (pos < 0) { wifiClearProfile(slot); continue; }
    const int ssidKey = body.indexOf("\"ssid\":\"", pos), pwdKey = body.indexOf("\"password\":\"", pos); if (ssidKey < 0 || pwdKey < 0) continue;
    const int ss = ssidKey + 8, se = body.indexOf('"', ss), ps = pwdKey + 12, pe = body.indexOf('"', ps); if (se < 0 || pe < 0) continue;
    const String ssid = body.substring(ss, se), password = body.substring(ps, pe); wifiSetProfile(slot, ssid.c_str(), password.c_str());
  }
  Serial.println("[WIFI] Profils dashboard synchronises via 4G."); return true;
}

void trakCommunicationTaskFixed(void*) {
  GnssPosition position; uint32_t lastGnssPoll = millis() - GNSS_POLL_MS, lastRecord = millis() - SEND_INTERVAL_MS, lastLog = 0, lastRecovery = millis();
  uint32_t lastRemoteIntervalPoll = millis() - REMOTE_INTERVAL_POLL_MS, lastBufferRetry = 0, lastBufferInitRetry = millis(), lastWifiSync = millis() - WIFI_PROFILE_SYNC_MS, previousMotionReturnMs = 0, lastTerrainLog = 0;
  bool bufferFlushActive = false, bufferWasFull = false;

  wifiManagerBegin();
  if (wifiConnectBestSaved()) activeNetwork = NetworkPath::WiFi;
  else if (cellularReady) activeNetwork = NetworkPath::Cellular;
  else activeNetwork = NetworkPath::None;
  devLog(String("START | network=") + terrainNetworkName(activeNetwork) +
         " | fifo=" + String((unsigned)positionBuffer.size()));

  for (;;) {
    trakWizardSmsTick();
    if (trakWizardActive()) {
      activeNetwork = NetworkPath::None;
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    const uint32_t now = millis();
    wifiNetworkTick(true);
    const NetworkPath previousNetwork = activeNetwork;
    if (wifiIsActive()) activeNetwork = NetworkPath::WiFi;
    else if (cellularReady) activeNetwork = NetworkPath::Cellular;
    else activeNetwork = NetworkPath::None;
    if (activeNetwork != previousNetwork) {
      devLog(String("NETWORK | ") + terrainNetworkName(previousNetwork) + " -> " + terrainNetworkName(activeNetwork));
    }

    if (!bufferReady && now - lastBufferInitRetry >= CELLULAR_RETRY_MS) { lastBufferInitRetry = now; trakPositionBufferInit(); }
    if (!modemReady && now - lastRecovery >= CELLULAR_RETRY_MS) { lastRecovery = now; if (powerOnModem()) { detectApn(); attachCellular(); configureGnss(); } }
    else if (modemReady && !cellularReady && now - lastRecovery >= CELLULAR_RETRY_MS) { lastRecovery = now; attachCellular(); }

    if (now - lastWifiSync >= WIFI_PROFILE_SYNC_MS) {
      lastWifiSync = now;
      if (wifiIsActive()) wifiSyncProfilesFromServer();
      else if (cellularReady) syncWifiProfilesVia4G();
    }
    if (cellularReady && now - lastRemoteIntervalPoll >= REMOTE_INTERVAL_POLL_MS) { lastRemoteIntervalPoll = now; fetchRemoteIntervalFixed(); }

    motionUpdate(now); sendIntervalMs = currentSendIntervalMs();
    const uint32_t motionReturnMs = motionStationaryConfirmationRemainingMs(); const bool motionReturnStarted = (previousMotionReturnMs == 0 && motionReturnMs > 0); previousMotionReturnMs = motionReturnMs;

    if (now - lastGnssPoll >= GNSS_POLL_MS) {
      lastGnssPoll = now;
      const bool previousFix = gnssFix;
      GnssPosition next;
      const bool gnssResponse = readGnss(next);
      gnssFix = gnssResponse && next.valid;
      if (gnssFix) {
        position = next;
        if (!previousFix) {
          devLog(String("GNSS | FIX_ACQUIRED | mode=") + String(position.fixMode) +
                 " sat=" + String(position.totalSatellites) +
                 " used=" + String(position.usedSatellites) +
                 " GPS=" + String(position.gpsSatellites) +
                 " GLO=" + String(position.glonassSatellites) +
                 " GAL=" + String(position.galileoSatellites) +
                 " BEI=" + String(position.beidouSatellites) +
                 " hdop=" + String(position.hdop, 2) +
                 " speed=" + String(position.speedKnots, 2) + "kn");
        }
        if (now - lastLog >= GNSS_LOG_MS) {
          lastLog = now;
          Serial.printf("[GNSS] FIX | SAT=%u USED=%u GPS=%u GLO=%u GAL=%u BEI=%u | lat=%.6f lon=%.6f alt=%.1f m\n",
                        position.totalSatellites, position.usedSatellites,
                        position.gpsSatellites, position.glonassSatellites,
                        position.galileoSatellites, position.beidouSatellites,
                        position.latitude, position.longitude, position.altitude);
        }
      } else {
        if (previousFix) devLog("GNSS | FIX_LOST");
        if (now - lastLog >= GNSS_LOG_MS) {
          lastLog = now;
          Serial.println("[GNSS] RECHERCHE FIX");
        }
      }
    }

    if (motionReturnStarted && bufferReady && gnssFix) {
      lastRecord = now;
      const size_t before = positionBuffer.size();
      if (positionBuffer.push(position)) {
        centerBlinkUntil = now + 900;
        if (before == 0) devLog("FIFO | DATA_PENDING | first position queued");
      }
    }
    if (bufferReady && gnssFix && now - lastRecord >= sendIntervalMs) {
      lastRecord = now;
      const size_t before = positionBuffer.size();
      if (positionBuffer.push(position)) {
        centerBlinkUntil = now + 900;
        if (before == 0) devLog("FIFO | DATA_PENDING | first position queued");
      }
    }

    if (bufferReady && activeNetwork != NetworkPath::None && !positionBuffer.empty() && now - lastBufferRetry >= BUFFER_RETRY_MS) {
      lastBufferRetry = now; const size_t backlog = positionBuffer.size(); const uint8_t budget = backlog > 10 ? 3 : 1;
      if (backlog > 1 && !bufferFlushActive) { bufferFlushActive = true; devLog(String("Buffer flush started: ") + String((unsigned)backlog)); }
      for (uint8_t n = 0; n < budget && !positionBuffer.empty(); ++n) {
        GnssPosition buffered; if (!positionBuffer.peek(buffered)) break;
        int httpStatus = 0; const int result = postBufferedPosition(buffered, httpStatus);
        if (result == 0) {
          positionBuffer.pop();
          Serial.printf("[BUFFER] Sent OK via %s, remaining=%u\n", activeNetwork == NetworkPath::WiFi ? "WiFi" : "4G", (unsigned)positionBuffer.size());
          devLog(String("FIFO | SENT_OK | via=") + terrainNetworkName(activeNetwork) + " | remaining=" + String((unsigned)positionBuffer.size()));
          continue;
        }
        if (result == 2) { if (activeNetwork == NetworkPath::Cellular) cellularReady = false; activeNetwork = NetworkPath::None; Serial.println("[NET] Transport perdu, FIFO conservee."); devLog("Transport failure -> network recovery"); }
        else {
          Serial.printf("[TRAK-CONNECT] HTTP %d, position conservee.\n", httpStatus);
          devLog(String("FIFO | SEND_ERROR | HTTP=") + String(httpStatus) + " | retained=1");
        }
        break;
      }
      if (positionBuffer.empty() && bufferFlushActive) { bufferFlushActive = false; devLog("Buffer flush complete"); }
      if (positionBuffer.size() >= POSITION_BUFFER_CAPACITY && !bufferWasFull) { bufferWasFull = true; Serial.println("[BUFFER] FIFO pleine."); devLog("Buffer full"); }
      if (positionBuffer.size() < POSITION_BUFFER_CAPACITY) bufferWasFull = false;
    }

    if (now - lastLog >= GNSS_LOG_MS) {
      lastLog = now;
      Serial.printf("[TRAK] network=%s buffer=%u motion=%s\n", activeNetwork == NetworkPath::WiFi ? "WiFi" : activeNetwork == NetworkPath::Cellular ? "4G" : "None", (unsigned)positionBuffer.size(), motionIsMobile() ? "MOBILE" : "IMMOBILE");
    }

    // One compact SD snapshot every 10 s. This is intentionally independent
    // from the Serial GNSS log cadence and contains the complete field state.
    if (now - lastTerrainLog >= TERRAIN_LOG_MS) {
      lastTerrainLog = now;
      if (activeNetwork == NetworkPath::Cellular) readCellularSignalPercent();
      terrainLogSnapshot(position, activeNetwork, positionBuffer.size());
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
