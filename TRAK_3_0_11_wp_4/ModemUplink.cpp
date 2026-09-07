#include "ModemUplink.h"

#include "DevLog.h"
#include "TrackerState.h"
#include "TrackingEngine.h"
#include "TRAKConnect.h"
#include "PositionBuffer.h"

namespace {

// Tampon circulaire TRAK Connect : profondeur volontairement faible,
// seule la position la plus récente compte pour l'affichage live.
static constexpr size_t TRAK_CONNECT_QUEUE_DEPTH = 4;

SemaphoreHandle_t queueMutex = nullptr;
TrakConnectSample queueItems[TRAK_CONNECT_QUEUE_DEPTH];
size_t queueHead = 0;
size_t queueCount = 0;

volatile bool trakConnectLastOk = false;

TaskHandle_t uplinkTaskHandle = nullptr;

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

void modemUplinkTask(void* parameter)
{
  (void)parameter;
  DevSerial.println("[UPLINK] Tâche démarrée : seule propriétaire du modem 4G.");

  for (;;) {
    if (getActiveNetwork() != ActiveNetwork::CELLULAR) {
      // Rien à faire : en WiFi, TrackingEngine et TRAKConnect envoient
      // directement (pas de ressource matérielle partagée à protéger).
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
      continue;
    }

    bool didWork = false;

    // 1) Position "live" TRAK Connect : sensible à la latence (affichage
    //    carte en direct), donc toujours traitée en premier quand une
    //    est en attente. Coût désormais réduit (session TLS réutilisée,
    //    SSL configuré une seule fois — voir ModemManager.cpp).
    TrakConnectSample sample;
    if (popTrakConnectSample(sample)) {
      const bool ok = trakConnectSendOneCellular(
          sample.latitude, sample.longitude, sample.seq);
      trakConnectLastOk = ok;
      didWork = true;
    }

    // 2) Un point du backlog Trackserver (FIFO SD). Un seul par
    //    itération : avec deux tâches concurrentes supprimées, il n'y a
    //    plus besoin d'un arbitrage à courte échéance — la boucle
    //    elle-même garantit qu'un gros backlog Trackserver ne peut pas
    //    empêcher indéfiniment le prochain échantillon TRAK Connect de
    //    passer, puisqu'on revérifie la file TRAK Connect à chaque tour.
    if (trackingEngineSendOneCellular()) {
      didWork = true;
    }

    if (!didWork) {
      // Rien à envoyer pour l'instant : on attend soit une notification
      // (nouveau point FIFO ou nouvel échantillon TRAK Connect), soit un
      // court délai, pour ne pas boucler à vide sur le CPU.
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
    }
    // Si du travail a été fait, on reboucle immédiatement (pas de délai
    // artificiel) : c'est cette absence de "pause de politesse" a
    // posteriori — remplacée par le fait qu'un seul task existe — qui
    // élimine la famine mutuelle observée avec l'ancien ModemArbiter.
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

  DevSerial.println("[UPLINK] Ordonnanceur modem 4G pret.");
}

void modemUplinkEnqueueTrakConnect(const TrakConnectSample& sample)
{
  if (queueMutex == nullptr) return;
  if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

  if (queueCount == TRAK_CONNECT_QUEUE_DEPTH) {
    // File pleine : on écrase le plus ancien échantillon en attente.
    // Seule la fraîcheur compte pour TRAK Connect, pas l'historique.
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
