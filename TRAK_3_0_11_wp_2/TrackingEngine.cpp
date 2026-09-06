#include "TrackingEngine.h"

#include "Config.h"
#include "DevLog.h"
#include "ModemManager.h"
#include "ModemArbiter.h"
#include "NetworkManager.h"
#include "PositionBuffer.h"
#include "RuntimeConfig.h"
#include "StatusLedManager.h"
#include "TrackerState.h"

namespace {

TaskHandle_t senderTaskHandle = nullptr;
volatile uint32_t txCount = 0;
volatile uint32_t lastTxAt = 0;
volatile bool lastHttpSuccess = false;

uint32_t nextCaptureAt = 0;
bool initialized = false;
bool previousMoving = false;

static constexpr uint32_t SENDER_IDLE_MS = 250;
static constexpr uint32_t SENDER_RETRY_MS = 1500;
static constexpr uint32_t MODEM_ARB_TIMEOUT_MS = 250;

BufferedPosition makeBufferedPosition(const GnssData& gps)
{
  BufferedPosition p{};
  p.latitude = gps.latitude;
  p.longitude = gps.longitude;
  p.altitude = gps.altitude;
  p.speedKmh = gps.speedKmh;
  p.year = gps.year;
  p.month = gps.month;
  p.day = gps.day;
  p.hour = gps.hour;
  p.minute = gps.minute;
  p.second = gps.second;
  p.millisecond = gps.millisecond;
  return p;
}

bool queuePosition(const GnssData& gps)
{
  if (!positionBufferIsReady()) {
    DevSerial.println("[TRACK2] SD indisponible : position non mémorisée.");
    return false;
  }

  if (!positionBufferPush(makeBufferedPosition(gps))) {
    DevSerial.println("[TRACK2] Impossible de mettre la position dans le FIFO.");
    return false;
  }

  DevSerial.print("[TRACK2] Position enregistrée FIFO. En attente : ");
  DevSerial.println(positionBufferCount());
  updateBufferSnapshot();

  if (senderTaskHandle != nullptr) {
    xTaskNotifyGive(senderTaskHandle);
  }
  return true;
}

void publishEngineState()
{
  SharedState snapshot;
  if (!copyState(snapshot)) return;

  const ActiveNetwork network = getActiveNetwork();
  const int signalPercent =
      network == ActiveNetwork::WIFI ? getWiFiSignalPercent() :
      (network == ActiveNetwork::CELLULAR ? getCellularSignalQuality() : 0);

  publishNetworkState(
      &snapshot.gps,
      signalPercent,
      lastHttpSuccess,
      txCount,
      lastTxAt);
}

bool sendOneBufferedPosition()
{
  if (!positionBufferIsReady() || positionBufferCount() == 0) return false;

  const ActiveNetwork network = getActiveNetwork();
  if (network == ActiveNetwork::NONE) return false;

  BufferedPosition buffered;
  if (!positionBufferPeek(buffered)) {
    updateBufferSnapshot();
    return false;
  }

  uint32_t dataBytesSent = 0;
  const bool cellular = network == ActiveNetwork::CELLULAR;

  if (cellular) statusLedHttpStartCellular();
  else statusLedHttpStartWiFi();

  bool arbiterAcquired = false;
  if (cellular) {
    arbiterAcquired = modemArbiterAcquire(
        ModemArbiterClient::TRACKSERVER, MODEM_ARB_TIMEOUT_MS);

    if (!arbiterAcquired) {
      statusLedHttpStop();
      // The FIFO is deliberately untouched. This is a local modem
      // contention, not a failed Trackserver transmission.
      DevSerial.println("[TRACK2] A7670 occupe : envoi Trackserver reporte.");
      lastHttpSuccess = false;
      publishEngineState();
      return false;
    }
  }

  const bool ok = cellular
      ? sendToTrackserverCellular(
            buffered.latitude, buffered.longitude, buffered.altitude, buffered.speedKmh,
            buffered.year, buffered.month, buffered.day,
            buffered.hour, buffered.minute, buffered.second, buffered.millisecond,
            dataBytesSent)
      : sendToTrackserverWiFi(
            buffered.latitude, buffered.longitude, buffered.altitude, buffered.speedKmh,
            buffered.year, buffered.month, buffered.day,
            buffered.hour, buffered.minute, buffered.second, buffered.millisecond,
            dataBytesSent);

  if (arbiterAcquired) {
    modemArbiterRelease(ModemArbiterClient::TRACKSERVER);
  }

  statusLedHttpStop();

  if (dataBytesSent > 0 && buffered.year >= 2000 &&
      buffered.month >= 1 && buffered.month <= 12) {
    recordDataUsage(cellular, dataBytesSent, buffered.year, buffered.month);
  }

  lastHttpSuccess = ok;

  if (!ok) {
    DevSerial.println("[TRACK2] Envoi échoué : position conservée dans le FIFO.");
    networkReportTrackserverResult(false);
    publishEngineState();
    return false;
  }

  networkReportTrackserverResult(true);

  if (!positionBufferPop()) {
    DevSerial.println("[TRACK2] ERREUR : envoi OK mais retrait FIFO impossible.");
    publishEngineState();
    return false;
  }

  ++txCount;
  lastTxAt = millis();
  updateBufferSnapshot();

  DevSerial.print("[TRACK2] Position envoyée. Restant : ");
  DevSerial.println(positionBufferCount());
  publishEngineState();
  return true;
}

void trackingSenderTask(void* parameter)
{
  (void)parameter;
  DevSerial.println("[TRACK2] Sender démarré : FIFO -> Trackserver");

  for (;;) {
    if (positionBufferCount() == 0 || getActiveNetwork() == ActiveNetwork::NONE) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SENDER_IDLE_MS));
      continue;
    }

    // On vide progressivement le backlog. Une erreur arrête immédiatement
    // le cycle afin de laisser le NetworkManager changer de transport.
    if (!sendOneBufferedPosition()) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SENDER_RETRY_MS));
    } else {
      // Pas de temporisation fixe en cas de succès : le FIFO est drainé
      // aussi vite que le transport le permet.
      taskYIELD();
    }
  }
}

} // namespace

void trackingEngineBegin()
{
  if (senderTaskHandle != nullptr) return;

  xTaskCreatePinnedToCore(
      trackingSenderTask,
      "TrackingSender",
      8192,
      nullptr,
      2,
      &senderTaskHandle,
      0);

  if (senderTaskHandle == nullptr) {
    DevSerial.println("[TRACK2] ERREUR : task Sender impossible.");
    return;
  }

  DevSerial.println("[TRACK2] TrackingEngine v2 actif.");
  DevSerial.println("[TRACK2] GPS -> FIFO SD -> Sender -> Trackserver");
}

void trackingEngineUpdate(uint32_t now, const GnssData& gps, const MotionState& motion)
{
  if (!initialized) {
    initialized = true;
    previousMoving = motion.moving;
    nextCaptureAt = now;
    return;
  }

  // Le passage à MOBILE force une acquisition immédiate dès qu'un fix GNSS
  // est disponible, comme dans le comportement historique de TRAK.
  if (motion.moving && !previousMoving) {
    nextCaptureAt = now;
  }
  previousMoving = motion.moving;

  const uint32_t intervalMs = currentSendIntervalMs();
  if (intervalMs == 0) return;

  if (static_cast<int32_t>(now - nextCaptureAt) < 0) return;

  // Le point est persisté AVANT toute tentative réseau. C'est le principe
  // central du moteur v2 : le réseau ne peut plus faire perdre un point.
  if (gps.hasFix) {
    queuePosition(gps);
  } else {
    DevSerial.println("[TRACK2] Échéance tracking sans fix GNSS : aucun point.");
  }

  // Échéancier indépendant du temps passé par le transport. On ne décale
  // donc plus le prochain point parce qu'une requête HTTP a pris 3 ou 8 s.
  do {
    nextCaptureAt += intervalMs;
  } while (static_cast<int32_t>(now - nextCaptureAt) >= 0);
}

uint32_t trackingEngineTxCount()
{
  return txCount;
}

uint32_t trackingEngineLastTxAt()
{
  return lastTxAt;
}
