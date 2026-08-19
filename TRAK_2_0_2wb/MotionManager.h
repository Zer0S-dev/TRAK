#ifndef MOTION_MANAGER_H
#define MOTION_MANAGER_H

#include <Arduino.h>
#include "Config.h"

struct MotionState {
  bool calibrated = false;
  bool moving = false;
  bool stationaryConfirmed = false;
  float xG = 0.0f;
  float yG = 0.0f;
  float motionG = 0.0f;
  uint32_t lastMotionAt = 0;
  uint32_t stationarySince = 0;
};

void motionBegin();
void motionUpdate(uint32_t now);
const MotionState& motionState();
uint32_t currentSendIntervalMs();
const char* motionModeName();

#endif
