#pragma once
#include <Arduino.h>

struct GnssPosition {
  bool valid = false;
  uint8_t fixMode = 0;
  double latitude = 0.0;
  double longitude = 0.0;
  double altitude = 0.0;
  String timestamp;
  uint8_t gpsSatellites = 0;
  uint8_t galileoSatellites = 0;
  uint8_t beidouSatellites = 0;
  uint8_t glonassSatellites = 0;
  uint8_t totalSatellites = 0;
  uint8_t usedSatellites = 0;
  float speedKnots = 0.0f;
  float courseDeg = 0.0f;
  float pdop = 0.0f;
  float hdop = 0.0f;
  float vdop = 0.0f;
};

void trakRuntimeInit();
void trakCommunicationTask(void* parameter);
void trakLedTask(void* parameter);
