#pragma once
#include <Arduino.h>

struct GnssPosition {
  bool valid = false;
  double latitude = 0.0;
  double longitude = 0.0;
  double altitude = 0.0;
  String timestamp;
};

void trakRuntimeInit();
void trakCommunicationTask(void* parameter);
void trakLedTask(void* parameter);
