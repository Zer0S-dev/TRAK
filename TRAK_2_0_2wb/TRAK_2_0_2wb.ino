// TRAK v2.0.2wb
// ============================================================
//
// ├── tracker_v12.ino          <-- Fichier principal
// ├── Config.h                 <-- Pins, constantes, paramètres
// ├── RuntimeConfig.h          <-- Configuration réseau / NVS
// ├── RuntimeConfig.cpp        <-- Profils Wi-Fi + Trackserver + compteur data
// ├── DisplayManager.h         <-- Interface écran
// ├── DisplayManager.cpp       <-- OLED / U8g2
// ├── ModemManager.h            <-- Interface modem / GNSS
// ├── ModemManager.cpp          <-- AT, GNSS, Auto-APN, HTTP
// ├── MotionManager.h           <-- Détection mouvement
// ├── MotionManager.cpp         <-- ADXL337 / MOBILE / STATIONNAIRE
// ├── WebInterface.h            <-- Interface serveur Web
// ├── WebInterface.cpp          <-- Serveur Web + API REST
// ├── data/
// │   └── index.html            <-- Dashboard LittleFS
// ├── Secrets.h                 <-- Identifiants Wi-Fi (privé)
// └── README.md                 <-- Documentation du projet
//
// ============================================================
// TODO
//
// 3. Sentinelle + alerte SMS
// 4. Communicate using SMS -> TRAK box = answer using text messages
// 5. Display TRAK Box SIM Number on dashboard (secured)
// 
//
// ============================================================
// ID & PASSWORD
//
// --- Wi-Fi Direct ---
// Fichier : WebInterface.cpp
// URL    : http://192.168.4.1
//
// const char* AP_SSID     = "trak";
// const char* AP_PASSWORD = "trak@dmin";
//
// --- Wi-Fi Local ---
// Fichier : Config.h
// URL    : http://trak.local/
//
// #define WEB_USER     "admin"
// #define WEB_PASSWORD "trak@dmin"
//
// ============================================================
// FIRMWARE VERSION
//
// 1.0  - Start
//
// 1.1  - Ajout détection de mouvement
//        Gestion MOBILE / STATIONNAIRE / veille
//
// 1.2  - Optimisation du code principal
//
// 1.3w - Ajout de l'interface Web
//
// 1.5w - Gestion de 3 réseaux Wi-Fi
//        Ajout de l'adresse locale : http://trak.local/
//        Ajout du Wi-Fi Direct : http://192.168.4.1
//
// 1.6w - v12 : Trackserver configurable depuis le Dashboard
//
// 1.7w - v13 : Compteur mensuel des données Wi-Fi / 4G
//        Estimation de la consommation selon le coefficient opérateur
//
// 1.8w - v14 : Authentification Web TRAK
//        Ajout de login.html
//        Gestion de session par cookie
//
// 1.9w - v19 : Application Android
//        Partage de session en réseau local / Wi-Fi Direct
//        Auto-discovery du tracker
//
// 2.0wb - v20 : Buffer FIFO des positions sur SD
//        Persistance après redémarrage
//        Rattrapage automatique Wi-Fi / 4G
//
// 2.0.2wb - v21 : FIX 4G send data
// ============================================================

#include <Arduino.h>
#include <WiFi.h>

#include "Config.h"
#include "DisplayManager.h"
#include "ModemManager.h"
#include "MotionManager.h"
#include "RuntimeConfig.h"
#include "WebInterface.h"
#include "PositionBuffer.h"

struct SharedState {
  GnssData gps;
  int signalPercent = 0;
  bool httpSuccess = false;
  bool wifiConnected = false;
  bool communicationReady = false;
  bool activeWiFi = false;
  int8_t activeWiFiSlot = -1;
  bool internetAvailable = false;
  uint32_t txCount = 0;
  uint32_t lastTxAt = 0;
  uint32_t bufferCount = 0;
  MotionState motion;
};

static SharedState state;
static SemaphoreHandle_t stateMutex = nullptr;

static bool copyState(SharedState& destination)
{
  if (stateMutex == nullptr) {
    return false;
  }

  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    return false;
  }

  destination = state;

  xSemaphoreGive(stateMutex);
  return true;
}

static void updateState(
    const GnssData* gps,
    int signalPercent,
    bool httpSuccess,
    bool wifiConnected,
    bool communicationReady,
    uint32_t txCount,
    uint32_t lastTxAt)
{
  if (stateMutex == nullptr) {
    return;
  }

  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    return;
  }

  if (gps != nullptr) {
    state.gps = *gps;
  }

  state.signalPercent = signalPercent;
  state.httpSuccess = httpSuccess;
  state.wifiConnected = wifiConnected;
  state.communicationReady = communicationReady;
  state.txCount = txCount;
  state.lastTxAt = lastTxAt;

  xSemaphoreGive(stateMutex);
}

static bool getWebTrackerData(WebTrackerData& out)
{
  SharedState snapshot;
  if (!copyState(snapshot)) {
    return false;
  }

  out.hasFix = snapshot.gps.hasFix;
  out.satellites = snapshot.gps.satellites;
  out.gpsSatellites = snapshot.gps.gpsSatellites;
  out.glonassSatellites = snapshot.gps.glonassSatellites;
  out.beidouSatellites = snapshot.gps.beidouSatellites;
  out.galileoSatellites = snapshot.gps.galileoSatellites;
  out.fixMode = snapshot.gps.fixMode;
  out.latitude = snapshot.gps.latitude;
  out.longitude = snapshot.gps.longitude;
  out.altitude = snapshot.gps.altitude;
  out.speedKmh = snapshot.gps.speedKmh;
  out.rawSpeedKmh = snapshot.gps.rawSpeedKmh;
  out.filteredSpeedKmh = snapshot.gps.filteredSpeedKmh;
  out.rawAltitude = snapshot.gps.rawAltitude;
  out.filteredAltitude = snapshot.gps.filteredAltitude;

  out.motionXG = snapshot.motion.xG;
  out.motionYG = snapshot.motion.yG;
  out.motionG = snapshot.motion.motionG;
  out.moving = snapshot.motion.moving;
  out.stationaryConfirmed = snapshot.motion.stationaryConfirmed;
  out.motionCalibrated = snapshot.motion.calibrated;

  out.signalPercent = snapshot.signalPercent;
  out.httpSuccess = snapshot.httpSuccess;
  out.wifiConnected = snapshot.wifiConnected;
  out.communicationReady = snapshot.communicationReady;
  out.activeWiFi = snapshot.activeWiFi;
  out.activeWiFiSlot = snapshot.activeWiFiSlot;
  out.internetAvailable = snapshot.internetAvailable;
  out.activeNetworkName = snapshot.activeWiFi ? "Wi-Fi" : (snapshot.communicationReady ? "4G" : "Aucun");
  out.txCount = snapshot.txCount;
  out.lastTxAt = snapshot.lastTxAt;
  out.bufferCount = snapshot.bufferCount;
  out.bufferCapacity = positionBufferCapacity();
  return true;
}

static void updateMotionSnapshot(const MotionState& motion)
{
  if (stateMutex == nullptr) return;
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
  state.motion = motion;
  xSemaphoreGive(stateMutex);
}

static void updateBufferSnapshot()
{
  if (stateMutex == nullptr) return;
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
  state.bufferCount = positionBufferCount();
  xSemaphoreGive(stateMutex);
}

static bool checkWiFiInternet();

// Profil Wi-Fi NVS actuellement utilisé (0..2), -1 si aucun.
static int8_t activeWiFiSlot = -1;

static const char* wifiStatusName(wl_status_t status)
{
  switch (status) {
    case WL_NO_SHIELD:       return "NO_SHIELD";
    case WL_IDLE_STATUS:    return "IDLE";
    case WL_NO_SSID_AVAIL:  return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
    case WL_CONNECTED:      return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST:return "CONNECTION_LOST";
    case WL_DISCONNECTED:   return "DISCONNECTED";
    default:                return "UNKNOWN";
  }
}

static bool connectWiFiProfile(uint8_t slot, uint32_t timeoutMs)
{
  WiFiProfile profile;
  if (!getWiFiProfile(slot, profile)) return false;

  Serial.print("[WIFI] Profil #");
  Serial.print(slot + 1);
  Serial.print(" : ");
  Serial.println(profile.ssid);

  WiFi.disconnect(false);
  delay(100);
  WiFi.begin(profile.ssid, profile.password);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("[WIFI] Echec profil #");
    Serial.print(slot + 1);
    Serial.print(" : ");
    Serial.println(wifiStatusName(WiFi.status()));
    return false;
  }

  Serial.print("[WIFI] Connecte a ");
  Serial.print(profile.ssid);
  Serial.print(" | IP : ");
  Serial.println(WiFi.localIP());

  if (!checkWiFiInternet()) {
    Serial.print("[WIFI] Internet indisponible sur ");
    Serial.println(profile.ssid);
    WiFi.disconnect(false);
    return false;
  }

  Serial.print("[WIFI] Internet OK sur ");
  Serial.println(profile.ssid);
  activeWiFiSlot = (int8_t)slot;
  return true;
}

static bool connectWiFi()
{
  activeWiFiSlot = -1;
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);

  Serial.println("[MODE] Wi-Fi");
  Serial.println("[WIFI] Recherche des 3 profils enregistres...");

  bool foundProfile = false;
  for (uint8_t slot = 0; slot < MAX_WIFI_PROFILES; ++slot) {
    WiFiProfile profile;
    if (!getWiFiProfile(slot, profile)) continue;
    foundProfile = true;
    if (connectWiFiProfile(slot, WIFI_CONNECT_TIMEOUT_MS)) {
      return true;
    }
  }

  activeWiFiSlot = -1;
  if (!foundProfile) {
    Serial.println("[WIFI] Aucun profil Wi-Fi enregistré.");
    return false;
  }

  Serial.println("[WIFI] Aucun profil avec Internet disponible.");
  return false;
}

static void applyNetworkPowerMode(bool stationary)
{
  // Cette fonction est appelée très fréquemment par la tâche réseau.
  // Ne reconfigure pas le Wi-Fi et ne réécris pas le log à chaque passage.
  static bool initialized = false;
  static bool lastStationary = false;

  if (initialized && stationary == lastStationary) {
    return;
  }

  WiFi.setSleep(stationary);
  initialized = true;
  lastStationary = stationary;

  Serial.print("[WIFI] Mode economie : ");
  Serial.println(stationary ? "ON" : "OFF");
}

enum class ActiveNetwork : uint8_t { NONE, WIFI, CELLULAR };

static ActiveNetwork activeNetwork = ActiveNetwork::NONE;

static const char* activeNetworkName()
{
  switch (activeNetwork) {
    case ActiveNetwork::WIFI: return "Wi-Fi";
    case ActiveNetwork::CELLULAR: return "4G";
    default: return "Aucun";
  }
}

static bool checkWiFiInternet()
{
  if (WiFi.status() != WL_CONNECTED) return false;
  TrackserverConfig ts;
  if (!getTrackserverConfig(ts)) return false;

  WiFiClient client;
  client.setTimeout(2500);
  const bool ok = client.connect(ts.host, 80);
  client.stop();
  return ok;
}

static bool activateWiFi()
{
  return connectWiFi();
}

static bool activateCellular()
{
  if (!connectCellularNetwork()) return false;
  return checkCellularInternet();
}

static bool tryPreferredNetwork()
{
  // Aucun profil Wi-Fi configuré : passage direct en 4G, sans scan/timeout Wi-Fi.
  if (wifiProfileCount() == 0) {
    Serial.println("[NET] Aucun profil Wi-Fi : passage direct en 4G.");
    if (activateCellular()) {
      activeNetwork = ActiveNetwork::CELLULAR;
      return true;
    }
    activeNetwork = ActiveNetwork::NONE;
    return false;
  }

  if (getNetworkMode() == NetworkMode::WIFI) {
    if (activateWiFi()) { activeNetwork = ActiveNetwork::WIFI; return true; }
    if (activateCellular()) { activeNetwork = ActiveNetwork::CELLULAR; return true; }
  } else {
    if (activateCellular()) { activeNetwork = ActiveNetwork::CELLULAR; return true; }
    if (activateWiFi()) { activeNetwork = ActiveNetwork::WIFI; return true; }
  }
  activeNetwork = ActiveNetwork::NONE;
  return false;
}

static bool tryFallbackNetwork()
{
  // Toujours respecter la préférence : elle détermine l'ordre de recherche.
  // En Wi-Fi, activateWiFi() essaie successivement les 3 profils NVS.
  return tryPreferredNetwork();
}

static void publishNetworkState(
    const GnssData* gps,
    int signalPercent,
    bool httpSuccess,
    uint32_t txCount,
    uint32_t lastTxAt)
{
  updateState(
      gps,
      signalPercent,
      httpSuccess,
      WiFi.status() == WL_CONNECTED,
      activeNetwork != ActiveNetwork::NONE,
      txCount,
      lastTxAt);

  if (stateMutex != nullptr && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    state.activeWiFi = activeNetwork == ActiveNetwork::WIFI;
    state.activeWiFiSlot = (activeNetwork == ActiveNetwork::WIFI) ? activeWiFiSlot : -1;
    state.internetAvailable = activeNetwork != ActiveNetwork::NONE;
    xSemaphoreGive(stateMutex);
  }
}

static BufferedPosition makeBufferedPosition(const GnssData& gps)
{
  BufferedPosition p;
  p.latitude = gps.latitude;
  p.longitude = gps.longitude;
  p.altitude = gps.altitude;
  p.speedKmh = gps.speedKmh;
  p.year = gps.year;
  p.month = gps.month;
  p.day = gps.day;
  p.capturedAtMs = millis();
  return p;
}

static bool bufferCurrentPosition(const GnssData& gps)
{
  if (!positionBufferIsReady()) {
    Serial.println("[BUFFER] SD indisponible : position non mémorisée.");
    return false;
  }

  const bool ok = positionBufferPush(makeBufferedPosition(gps));
  if (ok) {
    Serial.print("[BUFFER] Position ajoutée. En attente : ");
    Serial.println(positionBufferCount());
    updateBufferSnapshot();
  }
  return ok;
}

static bool flushOneBufferedPosition(
    const GnssData& currentGps,
    uint32_t& txCount,
    uint32_t& lastTxAt,
    bool& httpSuccess)
{
  if (!positionBufferIsReady() || positionBufferCount() == 0 ||
      activeNetwork == ActiveNetwork::NONE) {
    return false;
  }

  BufferedPosition buffered;
  if (!positionBufferPeek(buffered)) {
    updateBufferSnapshot();
    return false;
  }

  uint32_t dataBytesSent = 0;
  const bool cellular = activeNetwork == ActiveNetwork::CELLULAR;
  httpSuccess = cellular
      ? sendToTrackserverCellular(buffered.latitude, buffered.longitude, buffered.altitude, buffered.speedKmh, dataBytesSent)
      : sendToTrackserverWiFi(buffered.latitude, buffered.longitude, buffered.altitude, buffered.speedKmh, dataBytesSent);

  // Le trafic réseau est comptabilisé au mois de l'envoi, pas au mois
  // où la position a été capturée. C'est la consommation réelle actuelle.
  const uint16_t usageYear = currentGps.year >= 2000 ? currentGps.year : buffered.year;
  const uint8_t usageMonth = (currentGps.year >= 2000 && currentGps.month >= 1 && currentGps.month <= 12)
      ? currentGps.month : buffered.month;
  if (dataBytesSent > 0 && usageYear >= 2000 && usageMonth >= 1 && usageMonth <= 12) {
    recordDataUsage(cellular, dataBytesSent, usageYear, usageMonth);
  }

  if (!httpSuccess) {
    Serial.println("[BUFFER] Envoi de la position en attente échoué : elle reste dans le buffer.");
    activeNetwork = ActiveNetwork::NONE;
    return false;
  }

  if (!positionBufferPop()) {
    Serial.println("[BUFFER] ERREUR : position envoyée mais impossible de la retirer du buffer.");
    return false;
  }

  ++txCount;
  lastTxAt = millis();
  updateBufferSnapshot();

  Serial.print("[BUFFER] Position envoyée. Restant : ");
  Serial.println(positionBufferCount());
  return true;
}

static void CommunicationTask(void* parameter)
{
  (void)parameter;

  powerOnModem();
  modemSerial.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  vTaskDelay(pdMS_TO_TICKS(1000));

  Serial.println("[MODEM] Attente réponse AT...");
  uint32_t atStart = millis();
  bool modemReady = false;
  while (millis() - atStart < 15000) {
    if (sendATCommand("AT", 1000).indexOf("OK") >= 0) { modemReady = true; break; }
    Serial.println("[MODEM] Pas de réponse, nouvelle tentative...");
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  if (!modemReady) {
    Serial.println("[MODEM] ERREUR : A7670 non joignable.");
    updateState(nullptr, 0, false, false, false, 0, 0);
    while (true) vTaskDelay(pdMS_TO_TICKS(5000));
  }

  sendATCommand("ATE0");
  configureMultiGNSS();
  motionBegin();

  uint32_t txCount = 0;
  uint32_t lastTxAt = 0;
  uint32_t lastGnss = millis() - GNSS_INTERVAL_MS;
  uint32_t lastSend = millis() - SEND_INTERVAL_MS;
  uint32_t lastSignal = millis() - SIGNAL_INTERVAL_MS;
  uint32_t lastNetworkCheck = 0;
  uint32_t lastBufferFlush = 0;
  uint32_t lastConstellationDebug = 0;
  uint32_t lastMotionDebug = 0;
  bool previousMoving = false;
  uint16_t usageYear = 0;
  uint8_t usageMonth = 0;

  GnssData gps;
  bool httpSuccess = false;

  Serial.print("[NET] Préférence : ");
  Serial.println(networkModeName());
  if (!tryPreferredNetwork()) {
    Serial.println("[NET] Aucun accès Internet disponible. Fallback actif.");
  } else {
    Serial.print("[NET] Réseau actif : ");
    Serial.println(activeNetworkName());
  }
  publishNetworkState(&gps, 0, httpSuccess, txCount, lastTxAt);

  while (true) {
    const uint32_t now = millis();

    motionUpdate(now);
    const MotionState& motion = motionState();
    updateMotionSnapshot(motion);

    if (motion.moving && !previousMoving) {
      lastSend = now - (SEND_INTERVAL_MOVING_SEC * 1000UL);
      lastGnss = now - GNSS_INTERVAL_MS;
      Serial.println("[GNSS] Réveil logique : lecture immédiate après mouvement");
    }
    previousMoving = motion.moving;

    // -----------------------------
    // Fallback réseau automatique
    // -----------------------------
    if (now - lastNetworkCheck >= 20000UL || activeNetwork == ActiveNetwork::NONE) {
      lastNetworkCheck = now;
      bool internetOK = false;

      if (activeNetwork == ActiveNetwork::WIFI) {
        internetOK = checkWiFiInternet();
      } else if (activeNetwork == ActiveNetwork::CELLULAR) {
        internetOK = checkCellularInternet();
      }

      if (!internetOK) {
        Serial.print("[NET] Internet indisponible sur ");
        Serial.println(activeNetworkName());
        if (tryFallbackNetwork()) {
          Serial.print("[NET] Fallback réussi -> ");
          Serial.println(activeNetworkName());
          lastSend = now - (SEND_INTERVAL_MOVING_SEC * 1000UL);
        } else {
          Serial.println("[NET] Wi-Fi et 4G indisponibles. Nouvelle tentative plus tard.");
          activeNetwork = ActiveNetwork::NONE;
        }
      }
    }

    // ------------------------------------------------------------
    // Vidage FIFO du buffer SD : une position à la fois.
    // On privilégie le backlog avant d'envoyer une nouvelle position.
    // ------------------------------------------------------------
    if (activeNetwork != ActiveNetwork::NONE &&
        positionBufferCount() > 0 &&
        now - lastBufferFlush >= BUFFER_FLUSH_INTERVAL_MS) {
      lastBufferFlush = now;
      flushOneBufferedPosition(gps, txCount, lastTxAt, httpSuccess);
    }

    if (activeNetwork == ActiveNetwork::WIFI) {
      applyNetworkPowerMode(motion.stationaryConfirmed);
    }

    // GNSS adaptatif : le récepteur reste alimenté.
    const uint32_t gnssIntervalMs = motion.stationaryConfirmed
        ? GNSS_STATIONARY_INTERVAL_MS
        : GNSS_INTERVAL_MS;

    if (now - lastGnss >= gnssIntervalMs) {
      lastGnss = now;
      GnssData newGps = gps;
      if (readGNSS(newGps)) {
        gps = newGps;
        if (gps.year >= 2000 && gps.month >= 1 && gps.month <= 12 &&
            (gps.year != usageYear || gps.month != usageMonth)) {
          recordDataUsage(false, 0, gps.year, gps.month);
          usageYear = gps.year;
          usageMonth = gps.month;
        }
        digitalWrite(BOARD_LED, gps.hasFix ? HIGH : LOW);
        if (gps.hasFix) {
          Serial.print("Fix OK - Lat: "); Serial.print(gps.latitude, 6);
          Serial.print(" | Lon: "); Serial.print(gps.longitude, 6);
          Serial.print(" | Alt: "); Serial.print(gps.altitude, 1);
          Serial.print("m | Vit: "); Serial.print(gps.speedKmh, 1);
          Serial.println(" km/h");
        } else {
          Serial.print("Recherche GNSS (Mode: "); Serial.print(gps.fixMode);
          Serial.print(") - Sats: "); Serial.println(gps.satellites);
        }
        int sig = activeNetwork == ActiveNetwork::WIFI ? getWiFiSignalPercent() :
                  (activeNetwork == ActiveNetwork::CELLULAR ? getCellularSignalQuality() : 0);
        publishNetworkState(&gps, sig, httpSuccess, txCount, lastTxAt);
      }
    }

    if (now - lastConstellationDebug >= CONSTELLATION_DEBUG_INTERVAL_MS) {
      lastConstellationDebug = now;
      Serial.printf("[GNSS] GPS=%d GLO=%d BDS=%d GAL=%d TOTAL=%d | Vraw=%.1f Vfil=%.1f | Araw=%.1f Afil=%.1f\n",
                    gps.gpsSatellites, gps.glonassSatellites, gps.beidouSatellites,
                    gps.galileoSatellites, gps.satellites, gps.rawSpeedKmh,
                    gps.filteredSpeedKmh, gps.rawAltitude, gps.filteredAltitude);
    }

    if (now - lastMotionDebug >= CONSTELLATION_DEBUG_INTERVAL_MS) {
      lastMotionDebug = now;
      Serial.print("[ACC] X="); Serial.print(motion.xG, 3);
      Serial.print("g Y="); Serial.print(motion.yG, 3);
      Serial.print("g M="); Serial.print(motion.motionG, 3);
      Serial.print("g MODE="); Serial.print(motionModeName());
      Serial.print(" SEND=");
      const uint32_t intervalMs = currentSendIntervalMs();
      if (intervalMs == 0) Serial.println("OFF");
      else { Serial.print(intervalMs / 1000UL); Serial.println("s"); }
    }

    const uint32_t signalIntervalMs = motion.stationaryConfirmed
        ? SIGNAL_STATIONARY_INTERVAL_MS : SIGNAL_INTERVAL_MS;
    if (now - lastSignal >= signalIntervalMs) {
      lastSignal = now;
      int signalPercent = activeNetwork == ActiveNetwork::WIFI ? getWiFiSignalPercent() :
                          (activeNetwork == ActiveNetwork::CELLULAR ? getCellularSignalQuality() : 0);
      publishNetworkState(&gps, signalPercent, httpSuccess, txCount, lastTxAt);
    }

    const uint32_t sendIntervalMs = currentSendIntervalMs();
    if (sendIntervalMs > 0 && now - lastSend >= sendIntervalMs) {
      lastSend = now;
      if (!gps.hasFix) {
        Serial.println("[TRACK] Pas de fix GNSS : aucun envoi.");
      } else if (activeNetwork == ActiveNetwork::NONE) {
        Serial.println("[TRACK] Aucun réseau Internet : position mise en buffer.");
        bufferCurrentPosition(gps);
      } else if (positionBufferCount() > 0) {
        Serial.println("[TRACK] Buffer non vide : priorité au rattrapage FIFO.");
        flushOneBufferedPosition(gps, txCount, lastTxAt, httpSuccess);
      } else {
        uint32_t dataBytesSent = 0;
        const bool cellular = activeNetwork == ActiveNetwork::CELLULAR;
        httpSuccess = cellular
            ? sendToTrackserverCellular(gps.latitude, gps.longitude, gps.altitude, gps.speedKmh, dataBytesSent)
            : sendToTrackserverWiFi(gps.latitude, gps.longitude, gps.altitude, gps.speedKmh, dataBytesSent);

        if (dataBytesSent > 0 && gps.year >= 2000 && gps.month >= 1 && gps.month <= 12) {
          recordDataUsage(cellular, dataBytesSent, gps.year, gps.month);
        }

        if (httpSuccess) {
          ++txCount;
          lastTxAt = millis();
        } else {
          Serial.println("[NET] Envoi échoué : position ajoutée au buffer.");
          bufferCurrentPosition(gps);
          activeNetwork = ActiveNetwork::NONE;
        }
        int sig = activeNetwork == ActiveNetwork::WIFI ? getWiFiSignalPercent() : getCellularSignalQuality();
        publishNetworkState(&gps, sig, httpSuccess, txCount, lastTxAt);
      }
    }

    updateBufferSnapshot();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void UserInterfaceTask(void* parameter)
{
  bool constellationPage = false;
  uint32_t lastConstellationPage = 0;

  (void)parameter;

  // L'OLED appartient exclusivement au Core 1.
  initDisplay();

  uint32_t lastDisplay = 0;

  while (true) {
    const uint32_t now = millis();

    if (now - lastConstellationPage >= 3000) {
      lastConstellationPage = now;
      constellationPage = !constellationPage;
    }

    if (now - lastDisplay >= DISPLAY_INTERVAL_MS) {
      lastDisplay = now;

      SharedState snapshot;

      if (copyState(snapshot)) {
        const uint32_t lastTxAge =
            snapshot.lastTxAt == 0
                ? UINT32_MAX
                : millis() - snapshot.lastTxAt;

        updateOLED(
            snapshot.gps,
            snapshot.signalPercent,
            snapshot.httpSuccess,
            snapshot.txCount,
            lastTxAge,
            constellationPage,
            snapshot.activeWiFi,
            snapshot.activeWiFiSlot);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  runtimeConfigBegin();

  // SD : stockage persistant du buffer de positions.
  if (positionBufferBegin()) {
    updateBufferSnapshot();
  } else {
    Serial.println("[BUFFER] Buffer SD indisponible : le tracking continue sans stockage offline.");
  }

  Serial.print("[MODE] Configuration persistante : ");
  Serial.println(networkModeName());

  stateMutex = xSemaphoreCreateMutex();

  if (stateMutex == nullptr) {
    Serial.println("[FATAL] Impossible de créer le mutex.");
    while (true) {
      delay(1000);
    }
  }

  Serial.println();
  Serial.println("======================================");
  Serial.println(" TRAK - Dual Core v14");
  Serial.println(" Core 0 : A7670 / GNSS / réseau");
  Serial.println(" Core 1 : OLED");
  Serial.println("======================================");

  // Interface Web : Core 1. L'AP local est disponible dans les deux modes.
  webBegin(getWebTrackerData);
  xTaskCreatePinnedToCore(
      webTask,
      "WebInterface",
      8192,
      nullptr,
      1,
      nullptr,
      1);

  // Communication : Core 0.
  xTaskCreatePinnedToCore(
      CommunicationTask,
      "Communication",
      8192,
      nullptr,
      2,
      nullptr,
      0);

  // Interface : Core 1.
  xTaskCreatePinnedToCore(
      UserInterfaceTask,
      "UserInterface",
      4096,
      nullptr,
      1,
      nullptr,
      1);
}

void loop()
{
  // Le travail est effectué par les deux tâches FreeRTOS.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
