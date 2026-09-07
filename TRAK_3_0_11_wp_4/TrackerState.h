#pragma once
#include <Arduino.h>
#include "ModemManager.h"
#include "MotionManager.h"

enum class ActiveNetwork : uint8_t { NONE, WIFI, CELLULAR };

struct SharedState {
  GnssData gps;
  int signalPercent = 0;
  bool httpSuccess = false;
  bool communicationReady = false;
  bool wifiConnected = false;
  bool activeWiFi = false;
  int8_t activeWiFiSlot = -1;
  bool internetAvailable = false;
  uint32_t txCount = 0;
  uint32_t lastTxAt = 0;
  uint32_t bufferCount = 0;
  MotionState motion;
};

bool trackerStateBegin();
bool copyState(SharedState& destination);
void updateState(const GnssData* gps, int signalPercent, bool httpSuccess,
                 bool wifiConnected, bool communicationReady,
                 uint32_t txCount, uint32_t lastTxAt);
void updateMotionSnapshot(const MotionState& motion);
void updateBufferSnapshot();
void publishNetworkState(const GnssData* gps, int signalPercent, bool httpSuccess,
                         uint32_t txCount, uint32_t lastTxAt);

ActiveNetwork getActiveNetwork();
int8_t getActiveWiFiSlot();
void setActiveNetwork(ActiveNetwork network, int8_t wifiSlot = -1);
const char* activeNetworkName(ActiveNetwork network);
const char* activeNetworkName();
