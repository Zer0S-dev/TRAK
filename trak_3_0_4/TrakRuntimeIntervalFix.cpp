#include <Arduino.h>
#include <HardwareSerial.h>
#include "Config.h"
#include "TrakRuntime.h"
#include "PositionBuffer.h"
#include "MotionManager.h"

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
extern String waitForHttpAction(uint8_t method, uint32_t timeoutMs);
enum class HttpPostResult : uint8_t;
extern HttpPostResult httpPostJson(const String& json);
extern void devLog(const String& message);

static int postBufferedPosition(const GnssPosition& position) {
  return static_cast<int>(httpPostJson(buildJson(position)));
}

constexpr uint32_t GNSS_LOG_MS = 5000;
constexpr uint32_t CELLULAR_RETRY_MS = 30000;
constexpr uint32_t BUFFER_RETRY_MS = 1000;

static PositionBuffer positionBuffer;
static bool bufferReady = false;

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
  modem.print("AT+HTTPREAD=0,256\r\n");
  String response;
  response.reserve(384);
  const uint32_t start = millis();
  bool started = false;
  while (millis() - start < timeoutMs) {
    while (modem.available()) {
      response += static_cast<char>(modem.read());
      if (response.indexOf("+HTTPREAD:") >= 0) started = true;
      if (started && response.indexOf("+HTTPREAD: 0") >= 0) return response;
      if (response.indexOf("\r\nERROR\r\n") >= 0 || response.endsWith("\nERROR\n")) return response;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  return response;
}

static bool parseUnsignedField(const String& response, const char* key, uint32_t& value) {
  const int marker = response.indexOf(key);
  if (marker < 0) return false;
  const int colon = response.indexOf(':', marker);
  if (colon < 0) return false;
  int cursor = colon + 1;
  while (cursor < (int)response.length() && (response[cursor] == ' ' || response[cursor] == '\t')) ++cursor;
  int end = cursor;
  while (end < (int)response.length() && response[end] >= '0' && response[end] <= '9') ++end;
  if (end == cursor) return false;
  value = (uint32_t)response.substring(cursor, end).toInt();
  return true;
}

static bool parseRemoteSettings(const String& response, uint32_t& activeSec, uint32_t& idleSec, uint8_t& sensitivity) {
  uint32_t active = 0, idle = 0, sens = 0;
  if (!parseUnsignedField(response, "\"active_interval_seconds\"", active)) return false;
  if (!parseUnsignedField(response, "\"idle_interval_seconds\"", idle)) return false;
  if (!parseUnsignedField(response, "\"sensitivity_level\"", sens)) return false;
  if (!(active == 5 || active == 10 || active == 15 || active == 20)) return false;
  if (!(idle == 30 || idle == 60 || idle == 900 || idle == 1800 || idle == 3600)) return false;
  if (sens < 1 || sens > 5) return false;
  activeSec = active; idleSec = idle; sensitivity = (uint8_t)sens;
  return true;
}

static bool fetchRemoteIntervalFixed() {
  if (!modemReady || !cellularReady) return false;
  String url;
  url.reserve(strlen(TRAK_INTERVAL_URL) + strlen(TRAK_API_KEY) + 12);
  url += TRAK_INTERVAL_URL;
  url += "?api_key=";
  url += TRAK_API_KEY;

  Serial.println("[TRAK-CONNECT] Lecture reglages actifs/repos/gyro...");
  Serial.println(url);

  modem.print("AT+HTTPTERM\r\n");
  vTaskDelay(pdMS_TO_TICKS(100));
  while (modem.available()) modem.read();

  modem.print("AT+HTTPINIT\r\n");
  String init;
  const uint32_t initStart = millis();
  while (millis() - initStart < 3000) {
    while (modem.available()) init += (char)modem.read();
    if (init.indexOf("OK") >= 0 || init.indexOf("ERROR") >= 0) break;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  if (init.indexOf("OK") < 0) return false;

  modem.print(String("AT+HTTPPARA=\"URL\",\"") + url + "\"\r\n");
  String para;
  const uint32_t paraStart = millis();
  while (millis() - paraStart < 3000) {
    while (modem.available()) para += (char)modem.read();
    if (para.indexOf("OK") >= 0 || para.indexOf("ERROR") >= 0) break;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  if (para.indexOf("OK") < 0) return false;

  while (modem.available()) modem.read();
  modem.print("AT+HTTPACTION=0\r\n");
  const String action = waitForHttpAction(0, 10000);
  const int marker = action.indexOf("+HTTPACTION:");
  if (marker < 0) return false;

  String result = action.substring(marker + 12);
  result.trim();
  const int c1 = result.indexOf(',');
  const int c2 = result.indexOf(',', c1 + 1);
  const int status = c1 > 0 ? result.substring(c1 + 1, c2 > 0 ? c2 : result.length()).toInt() : 0;
  Serial.print("[HTTP] Reglages statut serveur : "); Serial.println(status);
  if (status < 200 || status >= 300) { modem.print("AT+HTTPTERM\r\n"); return false; }

  const String body = readHttpBody(5000);
  modem.print("AT+HTTPTERM\r\n");
  Serial.print("[HTTP] Corps reglages : "); Serial.println(body);

  uint32_t activeSec = 0, idleSec = 0; uint8_t sensitivity = 0;
  if (!parseRemoteSettings(body, activeSec, idleSec, sensitivity)) {
    Serial.println("[TRAK-CONNECT] Reglages actifs/repos/sensibilite invalides.");
    return false;
  }

  const uint32_t oldActive = motionActiveIntervalSec();
  const uint32_t oldIdle = motionIdleIntervalSec();
  const uint8_t oldSensitivity = motionSensitivityLevel();
  if (activeSec != oldActive || idleSec != oldIdle) {
    if (setMotionIntervals(activeSec, idleSec)) {
      Serial.print("[MODE] Actif/repos : "); Serial.print(activeSec); Serial.print("s / "); Serial.print(idleSec); Serial.println("s");
      devLog(String("Remote intervals: ") + String(oldActive) + "s/" + String(oldIdle) + "s -> " + String(activeSec) + "s/" + String(idleSec) + "s");
    }
  }
  if (sensitivity != oldSensitivity) {
    if (setMotionSensitivityLevel(sensitivity)) {
      Serial.print("[GYRO] Sensibilite distante : niveau "); Serial.println(sensitivity);
      devLog(String("Remote gyro sensitivity: ") + String(oldSensitivity) + " -> " + String(sensitivity));
    }
  }
  return true;
}

void trakCommunicationTaskFixed(void*) {
  GnssPosition position;
  uint32_t lastGnssPoll = millis() - GNSS_POLL_MS;
  uint32_t lastRecord = millis() - SEND_INTERVAL_MS;
  uint32_t lastLog = 0;
  uint32_t lastRecovery = millis();
  uint32_t lastRemoteIntervalPoll = millis() - REMOTE_INTERVAL_POLL_MS;
  uint32_t lastBufferRetry = 0;
  uint32_t lastBufferInitRetry = millis();
  uint32_t previousMotionReturnMs = 0;
  bool bufferFlushActive = false;
  bool bufferWasFull = false;

  for (;;) {
    const uint32_t now = millis();

    if (!bufferReady && now - lastBufferInitRetry >= CELLULAR_RETRY_MS) {
      lastBufferInitRetry = now;
      trakPositionBufferInit();
    }

    if (!modemReady && now - lastRecovery >= CELLULAR_RETRY_MS) {
      lastRecovery = now;
      if (powerOnModem()) { detectApn(); attachCellular(); configureGnss(); }
    } else if (modemReady && !cellularReady && now - lastRecovery >= CELLULAR_RETRY_MS) {
      lastRecovery = now;
      attachCellular();
    }

    if (cellularReady && now - lastRemoteIntervalPoll >= REMOTE_INTERVAL_POLL_MS) {
      lastRemoteIntervalPoll = now;
      fetchRemoteIntervalFixed();
    }

    // The gyro is sampled independently from the GPS recording cadence.
    motionUpdate(now);
    sendIntervalMs = currentSendIntervalMs();
    const uint32_t motionReturnMs = motionStationaryConfirmationRemainingMs();
    const bool motionReturnStarted = (previousMotionReturnMs == 0 && motionReturnMs > 0);
    previousMotionReturnMs = motionReturnMs;

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
        lastLog = now;
        Serial.println("[GNSS] Recherche du fix...");
      }
    }

    // Do not wait for the normal active interval when the 8 s return-to-idle
    // confirmation starts. Record the current GPS position immediately so the
    // dashboard receives motion_return_ms close to 8000 and can display 8 -> 0.
    if (motionReturnStarted && bufferReady && gnssFix) {
      lastRecord = now;
      const bool wasEmpty = positionBuffer.empty();
      if (positionBuffer.push(position)) {
        const size_t count = positionBuffer.size();
        Serial.printf("[BUFFER] Retour immobile: push immediat count=%u\n", (unsigned)count);
        if (wasEmpty) devLog(String("Buffer push retour immobile: count=") + String((unsigned)count));
        if (positionBuffer.full() && !bufferWasFull) {
          bufferWasFull = true;
          devLog(String("Buffer full: oldest overwritten, count=") + String((unsigned)count));
        }
        centerBlinkUntil = now + 900;
      }
    }

    if (bufferReady && gnssFix && now - lastRecord >= sendIntervalMs) {
      lastRecord = now;
      const bool wasEmpty = positionBuffer.empty();
      if (positionBuffer.push(position)) {
        const size_t count = positionBuffer.size();
        Serial.printf("[BUFFER] SD push count=%u\n", (unsigned)count);
        if (wasEmpty) devLog(String("Buffer push: count=") + String((unsigned)count));
        if (positionBuffer.full() && !bufferWasFull) {
          bufferWasFull = true;
          devLog(String("Buffer full: oldest overwritten, count=") + String((unsigned)count));
        }
        centerBlinkUntil = now + 900;
      }
    }

    if (bufferReady && cellularReady && !positionBuffer.empty() && now - lastBufferRetry >= BUFFER_RETRY_MS) {
      lastBufferRetry = now;
      const bool isBacklog = positionBuffer.size() > 1 || bufferFlushActive;
      if (isBacklog && !bufferFlushActive) {
        bufferFlushActive = true;
        devLog(String("Buffer flush started: ") + String((unsigned)positionBuffer.size()));
      }

      GnssPosition buffered;
      if (positionBuffer.peek(buffered)) {
        const int result = postBufferedPosition(buffered);
        if (result == 0) {
          positionBuffer.pop();
          Serial.printf("[BUFFER] Sent OK, remaining=%u\n", (unsigned)positionBuffer.size());
          if (positionBuffer.empty()) {
            if (bufferFlushActive) devLog("Buffer flush completed");
            bufferFlushActive = false;
            bufferWasFull = false;
          }
        } else if (result == 2) {
          cellularReady = false;
          Serial.println("[TRAK-CONNECT] Echec transport HTTP -> reconnexion 4G programmee.");
          devLog("REST transmission failed -> 4G recovery");
        } else {
          Serial.println("[TRAK-CONNECT] Serveur HTTP a refuse la position; position conservee dans le buffer SD.");
          devLog("REST server error -> point retained on SD");
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
