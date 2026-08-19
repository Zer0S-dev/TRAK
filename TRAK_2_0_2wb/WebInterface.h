#pragma once

#include <Arduino.h>

struct WebTrackerData {
  bool hasFix = false;
  int satellites = 0;
  int gpsSatellites = 0;
  int glonassSatellites = 0;
  int beidouSatellites = 0;
  int galileoSatellites = 0;
  int fixMode = 0;
  float latitude = 0.0f;
  float longitude = 0.0f;
  float altitude = 0.0f;
  float speedKmh = 0.0f;
  float rawSpeedKmh = 0.0f;
  float filteredSpeedKmh = 0.0f;
  float rawAltitude = 0.0f;
  float filteredAltitude = 0.0f;

  float motionXG = 0.0f;
  float motionYG = 0.0f;
  float motionG = 0.0f;
  bool moving = false;
  bool stationaryConfirmed = false;
  bool motionCalibrated = false;

  int signalPercent = 0;
  bool httpSuccess = false;
  bool wifiConnected = false;
  bool communicationReady = false;
  bool activeWiFi = false;
  int8_t activeWiFiSlot = -1;
  bool internetAvailable = false;
  const char* activeNetworkName = "Aucun";
  uint32_t txCount = 0;
  uint32_t lastTxAt = 0;
  uint32_t bufferCount = 0;
  uint32_t bufferCapacity = 0;
};

using WebDataProvider = bool (*)(WebTrackerData&);

void webBegin(WebDataProvider provider);
void webTask(void* parameter);
