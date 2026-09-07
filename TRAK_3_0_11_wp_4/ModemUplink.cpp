#include "ModemUplink.h"

#include "DevLog.h"
#include "TrackerState.h"
#include "TrackingEngine.h"
#include "TRAKConnect.h"
#include "PositionBuffer.h"

namespace {

// TRAK Connect est un canal live : on conserve uniquement les échantillons
// les plus récents lorsque la 4G est temporairement occupée.
static constexpr size_t TRAK_CONNECT_QUEUE_DEPTH = 4;

// TRAK Connect reste prioritaire, mais Trackserver possède désormais un
// créneau garanti lorsqu'il y a du FIFO à vider. On évite ainsi qu'un flux
// live à 2 s ne puisse affamer complètement le tracking historique.
static constexpr uint8_t TRAK_CONNECT_BATCH_MAX = 2;
static constexpr uint8_t TRACKSERVER_BATCH_MAX = 3;
static constexpr uint32_t TRACKSERVER_SERVICE_INTERVAL_MS = 5000;
static constexpr uint32_t HTTP_MODE_SWITCH_GUARD_MS = 250;
static constexpr uint32_t RETRY_AFTER_HTTP_FAILURE_MS = 500;

SemaphoreHandle_t queueMutex = nullptr;
TrakConnectSample queueItems[TRAK_CONNECT_QUEUE_DEPTH];
size_t queueHead = 0;
size_t queueCount = 0;

volatile bool trakConnectLastOk = false;
TaskHandle_t uplinkTaskHandle = nullptr;

// Dernier créneau Trackserver effectivement tenté en 4G.
uint32_t lastTrackserverServiceMs = 0;

enum class LastTransport : uint8_t {
  NONE,
  TRAK_CONNECT,
  TRACKSERVER
};

LastTransport lastTransport = LastTransport::NONE;

bool hasTrakConnectPending()
{
  if (queueMutex == nullptr) return false;
  if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(20)) != pdTRUE) return false;
  const bool has = queueCount > 0;
  xSemaphoreGive(queueMutex);
  return has;
}

bool popTrakConnectSample(TrakConnectSample& out)
{
  if (queueMutex == nullptr) return false;
  if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;

  bool has = queueCount > 0;
  if (has) {
    out = queueItems[queueHead];
    queueHead = (queueHead + 1) % TRAK_CONNECT_QUEUE_DEPTH;
    --queueCount;
  }

  xSemaphoreGive(queueMutex);
  return has;
}

bool hasTrackserverPending()
{
  return positionBufferIsReady() && positionBufferCount() > 0;
}

bool trackserverServiceDue()
{
  if (!hasTrackserverPending()) return false;
  if (lastTrackserverServiceMs == 0) return true;
  return (millis() - lastTrackserverServiceMs) >= TRACKSERVER_SERVICE_INTERVAL_MS;
}

void guardTransportSwitch(LastTransport nextTransport)
{
  if (lastTransport != LastTransport::NONE &&
      lastTransport != nextTransport) {
    // Le A7670 a besoin d'un petit temps de stabilisation lorsqu'on passe
    // d'une session HTTP à une session HTTPS, ou inversement. Ce délai est
    // uniquement appliqué lors d'un vrai changement de transport.
    vTaskDelay(pdMS_TO_TICKS(HTTP_MODE_SWITCH_GUARD_MS));
  }

  lastTransport = nextTransport;
}

void modemUplinkTask(void* parameter)
{
  (void)parameter;
  DevSerial.println("[UPLINK] Tâche démarrée : seule propriétaire du modem 4G.");

  for (;;) {
    if (getActiveNetwork() != ActiveNetwork::CELLULAR) {
      // En WiFi, les deux services utilisent leur transport indépendant.
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
      lastTransport = LastTransport::NONE;
      lastTrackserverServiceMs = 0;
      continue;
    }

    bool didWork = false;

    // ---------------------------------------------------------------------
    // 1) TRAK Connect : priorité live.
    // ---------------------------------------------------------------------
    // Si Trackserver a atteint son créneau garanti, on limite ce lot à un
    // seul point TC afin de libérer immédiatement le modem pour Trackserver.
    const uint8_t tcBatchMax = trackserverServiceDue()
        ? 1
        : TRAK_CONNECT_BATCH_MAX;

    for (uint8_t i = 0; i < tcBatchMax; ++i) {
      TrakConnectSample sample;
      if (!popTrakConnectSample(sample)) break;

      guardTransportSwitch(LastTransport::TRAK_CONNECT);

      const bool ok = trakConnectSendOneCellular(
          sample.latitude, sample.longitude, sample.seq);
      trakConnectLastOk = ok;
      didWork = true;

      if (!ok) {
        // Une erreur HTTPS doit laisser une chance au Trackserver de passer
        // après la remise à zéro effectuée par ModemManager.
        vTaskDelay(pdMS_TO_TICKS(RETRY_AFTER_HTTP_FAILURE_MS));
        break;
      }
    }

    // ---------------------------------------------------------------------
    // 2) Trackserver : créneau garanti toutes les 5 s si le FIFO est rempli.
    // ---------------------------------------------------------------------
    // En mouvement, TRAK Connect produit un point toutes les 2 s : on force
    // malgré tout un passage Trackserver au plus tard toutes les 5 s.
    // Cela empêche le canal live de monopoliser complètement le modem.
    //
    // Si aucun point TC n'est en attente, on profite au contraire de la
    // session HTTP pour vider jusqu'à 3 positions consécutives.
    if (hasTrackserverPending()) {
      const bool tcPending = hasTrakConnectPending();
      const bool serviceDue = trackserverServiceDue();

      if (!tcPending || serviceDue) {
        const uint8_t batchMax = tcPending ? 1 : TRACKSERVER_BATCH_MAX;

        for (uint8_t i = 0; i < batchMax; ++i) {
          if (!hasTrackserverPending()) break;

          // Dès qu'un nouveau point TC arrive pendant un batch de rattrapage,
          // on arrête le lot après le point Trackserver courant.
          if (i > 0 && hasTrakConnectPending()) break;

          guardTransportSwitch(LastTransport::TRACKSERVER);

          // Le créneau est consommé dès la tentative : une erreur ne doit
          // pas provoquer une boucle Trackserver qui bloque le canal live.
          lastTrackserverServiceMs = millis();

          const bool ok = trackingEngineSendOneCellular();
          didWork = true;

          if (!ok) {
            // Le point reste dans le FIFO. On laisse ensuite TRAK Connect
            // reprendre la main et on réessaiera au prochain créneau.
            vTaskDelay(pdMS_TO_TICKS(RETRY_AFTER_HTTP_FAILURE_MS));
            break;
          }
        }
      }
    }

    if (!didWork) {
      // Attente courte : notification d'un nouveau point ou réveil périodique.
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
    }
  }
}

} // namespace

void modemUplinkBegin()
{
  if (uplinkTaskHandle != nullptr) return;

  if (queueMutex == nullptr) {
    queueMutex = xSemaphoreCreateMutex();
    if (queueMutex == nullptr) {
      DevSerial.println("[UPLINK] ERREUR : mutex file TRAK Connect impossible.");
      return;
    }
  }

  xTaskCreatePinnedToCore(
      modemUplinkTask,
      "ModemUplink",
      6144,
      nullptr,
      2,
      &uplinkTaskHandle,
      0);

  if (uplinkTaskHandle == nullptr) {
    DevSerial.println("[UPLINK] ERREUR : tâche impossible.");
    return;
  }

  DevSerial.println("[UPLINK] Ordonnanceur modem 4G prêt : TC prioritaire + créneau Trackserver garanti 5 s.");
}

void modemUplinkEnqueueTrakConnect(const TrakConnectSample& sample)
{
  if (queueMutex == nullptr) return;
  if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

  if (queueCount == TRAK_CONNECT_QUEUE_DEPTH) {
    // La fraîcheur est prioritaire pour le live : on remplace le plus ancien.
    queueHead = (queueHead + 1) % TRAK_CONNECT_QUEUE_DEPTH;
    --queueCount;
  }

  const size_t insertAt = (queueHead + queueCount) % TRAK_CONNECT_QUEUE_DEPTH;
  queueItems[insertAt] = sample;
  ++queueCount;

  xSemaphoreGive(queueMutex);

  if (uplinkTaskHandle != nullptr) {
    xTaskNotifyGive(uplinkTaskHandle);
  }
}

bool modemUplinkTrakConnectLastOk()
{
  return trakConnectLastOk;
}
