#include "TrackerState.h"
#include <WiFi.h>
#include "PositionBuffer.h"
#include "Config.h"
#include "DevLog.h"
#include "StatusLedManager.h"

static SharedState state;
static SemaphoreHandle_t stateMutex = nullptr;
static SemaphoreHandle_t networkMutex = nullptr;
static ActiveNetwork activeNetwork = ActiveNetwork::NONE;
static int8_t activeWiFiSlot = -1;

bool trackerStateBegin()
{
  stateMutex = xSemaphoreCreateMutex();
  networkMutex = xSemaphoreCreateMutex();
  return stateMutex != nullptr && networkMutex != nullptr;
}

bool copyState(SharedState& destination)
{
  if (stateMutex == nullptr) return false;
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
  destination = state;
  xSemaphoreGive(stateMutex);
  return true;
}

void updateState(const GnssData* gps, int signalPercent, bool httpSuccess,
                 bool wifiConnected, bool communicationReady,
                 uint32_t txCount, uint32_t lastTxAt)
{
  if (stateMutex == nullptr) return;
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
  if (gps != nullptr) state.gps = *gps;
  state.signalPercent = signalPercent;
  state.httpSuccess = httpSuccess;
  state.wifiConnected = wifiConnected;
  state.communicationReady = communicationReady;
  state.txCount = txCount;
  state.lastTxAt = lastTxAt;
  xSemaphoreGive(stateMutex);
}

void updateMotionSnapshot(const MotionState& motion)
{
  if (stateMutex == nullptr) return;
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
  state.motion = motion;
  xSemaphoreGive(stateMutex);
}

void updateBufferSnapshot()
{
  if (stateMutex == nullptr) return;
  if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
  state.bufferCount = positionBufferCount();
  xSemaphoreGive(stateMutex);
}

void publishNetworkState(const GnssData* gps, int signalPercent, bool httpSuccess,
                         uint32_t txCount, uint32_t lastTxAt)
{
  const ActiveNetwork network = getActiveNetwork();
  const int8_t wifiSlot = getActiveWiFiSlot();
  updateState(gps, signalPercent, httpSuccess,
              WiFi.status() == WL_CONNECTED,
              network != ActiveNetwork::NONE,
              txCount, lastTxAt);

  if (stateMutex != nullptr && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    state.activeWiFi = network == ActiveNetwork::WIFI;
    state.activeWiFiSlot = (network == ActiveNetwork::WIFI) ? wifiSlot : -1;
    state.internetAvailable = network != ActiveNetwork::NONE;
    xSemaphoreGive(stateMutex);
  }
}

ActiveNetwork getActiveNetwork()
{
  if (networkMutex == nullptr) return ActiveNetwork::NONE;
  if (xSemaphoreTake(networkMutex, pdMS_TO_TICKS(10)) != pdTRUE) return ActiveNetwork::NONE;
  const ActiveNetwork value = activeNetwork;
  xSemaphoreGive(networkMutex);
  return value;
}

int8_t getActiveWiFiSlot()
{
  if (networkMutex == nullptr) return -1;
  if (xSemaphoreTake(networkMutex, pdMS_TO_TICKS(10)) != pdTRUE) return -1;
  const int8_t value = activeWiFiSlot;
  xSemaphoreGive(networkMutex);
  return value;
}

void setActiveNetwork(ActiveNetwork network, int8_t wifiSlot)
{
  if (networkMutex == nullptr) return;
  if (xSemaphoreTake(networkMutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
  activeNetwork = network;
  activeWiFiSlot = (network == ActiveNetwork::WIFI) ? wifiSlot : -1;
  xSemaphoreGive(networkMutex);

  statusLedSetNetwork(
      network == ActiveNetwork::WIFI,
      network == ActiveNetwork::CELLULAR);
}

const char* activeNetworkName(ActiveNetwork network)
{
  switch (network) {
    case ActiveNetwork::WIFI: return "Wi-Fi";
    case ActiveNetwork::CELLULAR: return "4G";
    default: return "Aucun";
  }
}

const char* activeNetworkName()
{
  return activeNetworkName(getActiveNetwork());
}
