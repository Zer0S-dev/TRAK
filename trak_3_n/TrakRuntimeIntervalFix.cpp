#include <Arduino.h>
#include <HardwareSerial.h>
#include "Config.h"
#include "TrakRuntime.h"
#include "PositionBuffer.h"
#include "MotionManager.h"
#include "WiFiManager.h"
#include "SmsConfigManager.h"
#include "TrackserverClient.h"
#include "DashboardClient.h"

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
extern void devLog(const String& message);
extern String at(const String& command, uint32_t timeoutMs);

enum class NetworkPath : uint8_t { None, WiFi, Cellular };
static PositionBuffer positionBuffer;
static PositionBuffer dashboardPositionBuffer("/buffer/dashboard_positions.dat");
static bool bufferReady = false;
static bool dashboardBufferReady = false;
static volatile NetworkPath activeNetwork = NetworkPath::None;

constexpr uint32_t GNSS_LOG_MS = 5000;
constexpr uint32_t CELLULAR_RETRY_MS = 30000;
constexpr uint32_t BUFFER_RETRY_MS = 200;
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
  line += " | fifo_dash="; line += String((unsigned)dashboardPositionBuffer.size());
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



bool trakPositionBufferInit() {
  if (bufferReady) return true;
  bufferReady = positionBuffer.begin();
  if (bufferReady) {
    Serial.printf("[BUFFER] FIFO SD actif: %u position(s) restauree(s)\n", (unsigned)positionBuffer.size());
    devLog(String("Buffer SD ready: count=") + String((unsigned)positionBuffer.size()));
  }
  return bufferReady;
}






void trakCommunicationTaskFixed(void*) {
  GnssPosition position; uint32_t lastGnssPoll = millis() - GNSS_POLL_MS, lastRecord = millis() - SEND_INTERVAL_MS, lastLog = 0, lastRecovery = millis();
  uint32_t lastBufferRetry = 0, lastBufferInitRetry = millis(), lastDashboardBufferInitRetry = millis() - 30000, previousMotionReturnMs = 0, lastTerrainLog = 0;
  bool bufferFlushActive = false, bufferWasFull = false;

  wifiManagerBegin();
  if (wifiConnectBestSaved()) activeNetwork = NetworkPath::WiFi;
  else if (cellularReady) activeNetwork = NetworkPath::Cellular;
  else activeNetwork = NetworkPath::None;
  trackserverBegin();
  dashboardBegin();
  devLog(String("START | network=") + terrainNetworkName(activeNetwork) +
         " | fifo=" + String((unsigned)positionBuffer.size()));

  for (;;) {
    const uint32_t now = millis();
    smsConfigTick();
    wifiNetworkTick(true);
    const NetworkPath previousNetwork = activeNetwork;
    if (wifiIsActive()) activeNetwork = NetworkPath::WiFi;
    else if (cellularReady) activeNetwork = NetworkPath::Cellular;
    else activeNetwork = NetworkPath::None;
    if (activeNetwork != previousNetwork) {
      devLog(String("NETWORK | ") + terrainNetworkName(previousNetwork) + " -> " + terrainNetworkName(activeNetwork));
    }

    if (!bufferReady && now - lastBufferInitRetry >= CELLULAR_RETRY_MS) { lastBufferInitRetry = now; trakPositionBufferInit(); }
    if (!dashboardBufferReady && !smsConfigDashboardUrl().isEmpty() && now - lastDashboardBufferInitRetry >= CELLULAR_RETRY_MS) { lastDashboardBufferInitRetry = now; trakDashboardPositionBufferInit(); }
    if (!modemReady && now - lastRecovery >= CELLULAR_RETRY_MS) { lastRecovery = now; if (powerOnModem()) { detectApn(); attachCellular(); configureGnss(); } }
    else if (modemReady && !cellularReady && now - lastRecovery >= CELLULAR_RETRY_MS) { lastRecovery = now; attachCellular(); }


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
      if (dashboardBufferReady) {
        const size_t dashboardBefore = dashboardPositionBuffer.size();
        if (dashboardPositionBuffer.push(position) && dashboardBefore == 0) {
          devLog("FIFO_DASHBOARD | DATA_PENDING | first position queued");
        }
      }
    }
    if (bufferReady && gnssFix && now - lastRecord >= sendIntervalMs) {
      lastRecord = now;
      const size_t before = positionBuffer.size();
      if (positionBuffer.push(position)) {
        centerBlinkUntil = now + 900;
        if (before == 0) devLog("FIFO | DATA_PENDING | first position queued");
      }
      if (dashboardBufferReady) {
        const size_t dashboardBefore = dashboardPositionBuffer.size();
        if (dashboardPositionBuffer.push(position) && dashboardBefore == 0) {
          devLog("FIFO_DASHBOARD | DATA_PENDING | first position queued");
        }
      }
    }

    if (dashboardBufferReady && activeNetwork != NetworkPath::None && !dashboardPositionBuffer.empty() && now - lastBufferRetry >= BUFFER_RETRY_MS) {
      GnssPosition buffered;
      if (dashboardPositionBuffer.peek(buffered)) {
        const DashboardResult result = dashboardSendPosition(buffered);
        if (result == DashboardResult::Success) {
          dashboardPositionBuffer.pop();
          centerBlinkUntil = now + 500;
          if (dashboardPositionBuffer.empty()) devLog("FIFO_DASHBOARD | FLUSH_COMPLETE");
        }
      }
    }

    if (bufferReady && activeNetwork != NetworkPath::None && !positionBuffer.empty() && now - lastBufferRetry >= BUFFER_RETRY_MS) {
      lastBufferRetry = now; const size_t backlog = positionBuffer.size(); const uint8_t budget = backlog > 10 ? 3 : 1;
      if (backlog > 1 && !bufferFlushActive) { bufferFlushActive = true; devLog(String("Buffer flush started: ") + String((unsigned)backlog)); }
      for (uint8_t n = 0; n < budget && !positionBuffer.empty(); ++n) {
        GnssPosition buffered;
        if (!positionBuffer.peek(buffered)) break;

        const TrackserverResult result = trackserverSend(buffered);
        if (result == TrackserverResult::Success) {
          positionBuffer.pop();
          centerBlinkUntil = now + 500;
          continue;
        }

        // NotReady: network/configuration is not usable right now.
        // Failed: keep the record on SD and retry on the next pass.
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
