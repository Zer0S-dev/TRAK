#include "ModemUplink.h"

#include "DevLog.h"
#include "TrackerState.h"
#include "TrackingEngine.h"
#include "TRAKConnect.h"
#include "PositionBuffer.h"

namespace {

// TRAK Connect is live communication. Keep a very small queue and always
// favour freshness over historical backlog.
static constexpr size_t TRAK_CONNECT_QUEUE_DEPTH = 4;

// The A7670 exposes one AT+HTTP session. Switching HTTP <-> HTTPS is the
// expensive operation, so we deliberately keep each protocol in service for
// a while instead of alternating on every transaction.
static constexpr uint8_t TRAK_CONNECT_BATCH_MAX = 4;
static constexpr uint8_t TRACKSERVER_BATCH_MAX = 4;
static constexpr uint32_t TRACKSERVER_SERVICE_INTERVAL_MS = 15000UL;
static constexpr uint32_t TRANSPORT_SWITCH_SETTLE_MS = 300UL;
static constexpr uint32_t HTTP_FAILURE_BACKOFF_MS = 1000UL;

SemaphoreHandle_t queueMutex = nullptr;
TrakConnectSample queueItems[TRAK_CONNECT_QUEUE_DEPTH];
size_t queueHead = 0;
size_t queueCount = 0;

volatile bool trakConnectLastOk = false;
TaskHandle_t uplinkTaskHandle = nullptr;

uint32_t lastTrackserverServiceMs = 0;
uint32_t lastHttpFailureMs = 0;

enum class Transport : uint8_t {
  NONE,
  TRAK_CONNECT,
  TRACKSERVER
};

Transport currentTransport = Transport::NONE;

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

  const bool has = queueCount > 0;
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

bool trackserverDue()
{
  if (!hasTrackserverPending()) return false;
  if (lastTrackserverServiceMs == 0) return true;
  return millis() - lastTrackserverServiceMs >= TRACKSERVER_SERVICE_INTERVAL_MS;
}

void prepareTransport(Transport next)
{
  if (currentTransport != Transport::NONE && currentTransport != next) {
    // Give the A7670 a short settle time after HTTPTERM/HTTPINIT/SSL mode
    // changes. The actual session lifecycle is handled by ModemManager.
    vTaskDelay(pdMS_TO_TICKS(TRANSPORT_SWITCH_SETTLE_MS));
  }
  currentTransport = next;
}

void resetCellularSessionState()
{
  currentTransport = Transport::NONE;
  lastHttpFailureMs = 0;
}

void modemUplinkTask(void* parameter)
{
  (void)parameter;
  DevSerial.println("[UPLINK] Tache demarree : proprietaire unique du modem 4G.");

  for (;;) {
    if (getActiveNetwork() != ActiveNetwork::CELLULAR) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
      resetCellularSessionState();
      continue;
    }

    bool didWork = false;

    // ---------------------------------------------------------------
    // 1. TRAK Connect live channel
    // ---------------------------------------------------------------
    // Drain a short burst. This avoids leaving fresh communication waiting
    // behind a historical FIFO while still bounding modem occupation.
    uint8_t tcSent = 0;
    while (tcSent < TRAK_CONNECT_BATCH_MAX) {
      TrakConnectSample sample;
      if (!popTrakConnectSample(sample)) break;

      prepareTransport(Transport::TRAK_CONNECT);

      const bool ok = trakConnectSendOneCellular(
          sample.latitude, sample.longitude, sample.seq);
      trakConnectLastOk = ok;
      didWork = true;
      ++tcSent;

      if (!ok) {
        lastHttpFailureMs = millis();
        vTaskDelay(pdMS_TO_TICKS(HTTP_FAILURE_BACKOFF_MS));
        break;
      }
    }

    // ---------------------------------------------------------------
    // 2. Trackserver service slot
    // ---------------------------------------------------------------
    // Trackserver is intentionally not allowed to dictate network state.
    // Its SD FIFO is the lossless backlog. We give it one service opportunity
    // every 15 s, matching the normal moving tracking cadence, and use a
    // larger batch only when there is no live TRAK Connect traffic waiting.
    if (trackserverDue() &&
        (millis() - lastHttpFailureMs >= HTTP_FAILURE_BACKOFF_MS)) {

      const bool tcPending = hasTrakConnectPending();
      const uint8_t batchMax = tcPending ? 1 : TRACKSERVER_BATCH_MAX;

      for (uint8_t i = 0; i < batchMax; ++i) {
        if (!hasTrackserverPending()) break;

        // If a fresh live packet appeared while recovering the FIFO, finish
        // only the current Trackserver transaction and return to HTTPS.
        if (i > 0 && hasTrakConnectPending()) break;

        prepareTransport(Transport::TRACKSERVER);
        lastTrackserverServiceMs = millis();

        const bool ok = trackingEngineSendOneCellular();
        didWork = true;

        if (!ok) {
          lastHttpFailureMs = millis();
          vTaskDelay(pdMS_TO_TICKS(HTTP_FAILURE_BACKOFF_MS));
          break;
        }
      }
    }

    if (!didWork) {
      // New TRAK Connect samples wake the task immediately. Otherwise the
      // short timeout keeps the scheduler responsive to network changes.
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
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
    DevSerial.println("[UPLINK] ERREUR : tache impossible.");
    return;
  }

  DevSerial.println("[UPLINK] Scheduler 4G : canal TC prioritaire + Trackserver toutes les 15 s.");
}

void modemUplinkEnqueueTrakConnect(const TrakConnectSample& sample)
{
  if (queueMutex == nullptr) return;
  if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

  if (queueCount == TRAK_CONNECT_QUEUE_DEPTH) {
    // Live data: discard the oldest queued point, never the newest one.
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
