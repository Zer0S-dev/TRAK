#include "CommunicationManager.h"
#include <WiFi.h>
#include "Config.h"
#include "DevLog.h"
#include "StatusLedManager.h"
#include "ModemManager.h"
#include "MotionManager.h"
#include "PositionBuffer.h"
#include "RuntimeConfig.h"
#include "SentinelManager.h"
#include "NetworkManager.h"
#include "TrackingEngine.h"
#include "TrackerState.h"

namespace {

static String normalizeSmsPhone(const String& value)
{
  String out;
  out.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if ((c >= '0' && c <= '9') || c == '+') {
      out += c;
    }
  }

  // Le numero peut etre en format national dans la NVS et en +33 dans le SMS.
  if (out.length() == 10 && out[0] == '0') {
    out = "+33" + out.substring(1);
  }

  return out;
}

static bool sameSmsPhone(const String& a, const String& b)
{
  return normalizeSmsPhone(a) == normalizeSmsPhone(b);
}

static void handleIncomingSms(const GnssData& gps)
{
  String sender;
  String message;

  if (!readIncomingSMS(sender, message)) return;

  message.trim();
  message.toUpperCase();

  // Phase 8 : une seule commande est active pour le moment.
  if (message != "POSITION") {
    DevSerial.println("[SMS] Commande ignoree.");
    return;
  }

  char storedPhone[SENTINEL_PHONE_MAX_LEN + 1] = {};
  if (!sentinelGetUserPhone(storedPhone, sizeof(storedPhone))) {
    DevSerial.println("[SMS] POSITION refusee : numero utilisateur absent.");
    return;
  }

  if (!sameSmsPhone(sender, String(storedPhone))) {
    DevSerial.println("[SMS] POSITION refusee : expediteur non autorise.");
    return;
  }

  DevSerial.println("[SMS] POSITION autorisee.");
  if (sentinelSendPositionSms(gps)) {
    DevSerial.println("[SMS] Reponse POSITION envoyee.");
  } else {
    DevSerial.println("[SMS] Echec reponse POSITION.");
  }
}

}

void communicationTask(void* parameter)
{
  (void)parameter;

  powerOnModem();
  beginModemSerial();
  vTaskDelay(pdMS_TO_TICKS(1000));

  DevSerial.println("[MODEM] Attente réponse AT...");
  uint32_t atStart = millis();
  bool modemReady = false;
  while (millis() - atStart < 15000) {
    if (sendATCommand("AT", 1000).indexOf("OK") >= 0) { modemReady = true; break; }
    DevSerial.println("[MODEM] Pas de réponse, nouvelle tentative...");
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  if (!modemReady) {
    DevSerial.println("[MODEM] ERREUR : A7670 non joignable.");
    updateState(nullptr, 0, false, false, false, 0, 0);
    while (true) vTaskDelay(pdMS_TO_TICKS(5000));
  }

  sendATCommand("ATE0");
  configureMultiGNSS();
  motionBegin();
  trackingEngineBegin();

  uint32_t lastGnss = millis() - GNSS_INTERVAL_MS;
  uint32_t lastSignal = millis() - SIGNAL_INTERVAL_MS;

  uint32_t lastConstellationDebug = 0;
  uint32_t lastMotionDebug = 0;
  uint32_t lastSmsPoll = 0;
  uint16_t usageYear = 0;
  uint8_t usageMonth = 0;

 GnssData gps{};

  DevSerial.println("[NET] Communication : NetworkManager Core 1 choisit le transport.");
  publishNetworkState(&gps, 0, false, trackingEngineTxCount(), trackingEngineLastTxAt());

  while (true) {
    const uint32_t now = millis();
    statusLedService();

    motionUpdate(now);
    const MotionState& motion = motionState();
    updateMotionSnapshot(motion);

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
        statusLedSetGpsFix(gps.hasFix);
        if (gps.hasFix) {
          DevSerial.print("Fix OK - Lat: "); DevSerial.print(gps.latitude, 6);
          DevSerial.print(" | Lon: "); DevSerial.print(gps.longitude, 6);
          DevSerial.print(" | Alt: "); DevSerial.print(gps.altitude, 1);
          DevSerial.print("m | Vit: "); DevSerial.print(gps.speedKmh, 1);
          DevSerial.println(" km/h");
        } else {
          DevSerial.print("Recherche GNSS (Mode: "); DevSerial.print(gps.fixMode);
          DevSerial.print(") - Sats: "); DevSerial.println(gps.satellites);
        }
        int sig = 0;

if (getActiveNetwork() == ActiveNetwork::WIFI) {
  sig = getWiFiSignalPercent();
}
else if (getActiveNetwork() == ActiveNetwork::CELLULAR) {
  sig = getCellularSignalQuality();
}
        publishNetworkState(&gps, sig, false, trackingEngineTxCount(), trackingEngineLastTxAt());

        // TrackingEngine v2 : le point vient d'être actualisé par le GNSS.
        // Il est persisté indépendamment de l'état du réseau.
        trackingEngineUpdate(now, gps, motion);
      }
    }

    // Sentinel surveille la transition IMMOBILE -> MOBILE avec le LSM6DS3.
    // La position GPS la plus recente est incluse dans l'alerte SMS.
    sentinelUpdate(now, gps, motion);

    // Phase 8 : polling des SMS entrants. Le modem est protege par son mutex.
    if (now - lastSmsPoll >= 2000) {
      lastSmsPoll = now;
      handleIncomingSms(gps);
    }

    if (now - lastConstellationDebug >= CONSTELLATION_DEBUG_INTERVAL_MS) {
      lastConstellationDebug = now;
      DevSerial.printf("[GNSS] GPS=%d GLO=%d BDS=%d GAL=%d TOTAL=%d | Vraw=%.1f Vfil=%.1f | Araw=%.1f Afil=%.1f\n",
                    gps.gpsSatellites, gps.glonassSatellites, gps.beidouSatellites,
                    gps.galileoSatellites, gps.satellites, gps.rawSpeedKmh,
                    gps.filteredSpeedKmh, gps.rawAltitude, gps.filteredAltitude);
    }

    if (now - lastMotionDebug >= CONSTELLATION_DEBUG_INTERVAL_MS) {
      lastMotionDebug = now;
      DevSerial.print("[GYRO] X="); DevSerial.print(motion.xDps, 3);
      DevSerial.print(" dps Y="); DevSerial.print(motion.yDps, 3);
      DevSerial.print(" dps Z="); DevSerial.print(motion.zDps, 3);
      DevSerial.print(" dps M="); DevSerial.print(motion.motionDps, 3);
      DevSerial.print(" dps MODE="); DevSerial.print(motionModeName());
      DevSerial.print(" SEND=");
      const uint32_t intervalMs = currentSendIntervalMs();
      if (intervalMs == 0) DevSerial.println("OFF");
      else { DevSerial.print(intervalMs / 1000UL); DevSerial.println("s"); }
    }

    const uint32_t signalIntervalMs = motion.stationaryConfirmed
        ? SIGNAL_STATIONARY_INTERVAL_MS : SIGNAL_INTERVAL_MS;
    if (now - lastSignal >= signalIntervalMs) {
      lastSignal = now;
      int signalPercent = getActiveNetwork() == ActiveNetwork::WIFI ? getWiFiSignalPercent() :
                          (getActiveNetwork() == ActiveNetwork::CELLULAR ? getCellularSignalQuality() : 0);
      publishNetworkState(&gps, signalPercent, false, trackingEngineTxCount(), trackingEngineLastTxAt());
    }


    updateBufferSnapshot();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
