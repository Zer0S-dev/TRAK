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

// On ne change pas de protocole HTTP à chaque position lorsque plusieurs
// positions Trackserver sont déjà en attente. Le changement HTTP <-> HTTPS
// force une reconfiguration de la session A7670 et doit donc rester rare.
static constexpr uint8_t TRAK_CONNECT_BATCH_MAX = 2;
static constexpr uint8_t TRACKSERVER_BATCH_MAX = 3;
static constexpr uint32_t HTTP_MODE_SWITCH_GUARD_MS = 250;
static constexpr uint32_t RETRY_AFTER_HTTP_FAILURE_MS = 500;

SemaphoreHandle_t queueMutex = nullptr;
TrakConnectSample queueItems[TRAK_CONNECT_QUEUE_DEPTH];
size_t queueHead = 0;
size_t queueCount = 0;

volatile bool trakConnectLastOk = false;
TaskHandle_t uplinkTaskHandle = nullptr;

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
      continue;
    }

    bool didWork = false;

    // ---------------------------------------------------------------------
    // 1) TRAK Connect : priorité live, mais avec une limite de lot.
    // ---------------------------------------------------------------------
    // Plusieurs échantillons TRAK Connect peuvent avoir été produits pendant
    // une transaction Trackserver. Ils utilisent tous HTTPS : les traiter
    // dans le même lot évite de refaire HTTPINIT/SSL à chaque point.
    for (uint8_t i = 0; i < TRAK_CONNECT_BATCH_MAX; ++i) {
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
    // 2) Trackserver : on profite de la session HTTP tant qu'aucun point
    //    TRAK Connect n'est en attente.
    // ---------------------------------------------------------------------
    // Contrairement à l'ancien "1 + 1" strict, on peut envoyer plusieurs
    // positions Trackserver consécutives. Cela amortit le coût HTTPINIT /
    // HTTPTERM lors du changement HTTPS <-> HTTP, tout en conservant une
    // latence bornée pour le canal live : dès qu'un nouveau point TRAK
    // Connect est en file, le lot Trackserver s'arrête après le point courant.
    for (uint8_t i = 0; i < TRACKSERVER_BATCH_MAX; ++i) {
      if (hasTrakConnectPending()) break;
      if (!positionBufferIsReady() || positionBufferCount() == 0) break;

      guardTransportSwitch(LastTransport::TRACKSERVER);

      const bool ok = trackingEngineSendOneCellular();
      didWork = true;

      if (!ok) {
        // Ne pas marteler HTTPINIT en boucle. Le point reste dans le FIFO et
        // sera repris lors du prochain passage.
        vTaskDelay(pdMS_TO_TICKS(RETRY_AFTER_HTTP_FAILURE_MS));
        break;
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

  DevSerial.println("[UPLINK] Ordonnanceur modem 4G prêt : lots TC/Trackserver + bascule amortie.");
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
