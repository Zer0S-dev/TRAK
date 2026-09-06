#ifndef MOTION_MANAGER_H
#define MOTION_MANAGER_H

#include <Arduino.h>
#include "Config.h"

struct MotionState {
  // Compatibility flag: true means the gyro is initialized and usable.
  // It is not a blocking startup calibration state.
  bool calibrated = false;
  bool moving = false;
  bool stationaryConfirmed = false;

  // LSM6DS3 gyro values, in degrees/second.
  float xDps = 0.0f;
  float yDps = 0.0f;
  float zDps = 0.0f;
  float motionDps = 0.0f;

  uint32_t lastMotionAt = 0;
  uint32_t stationarySince = 0;
};

void motionBegin();
void motionUpdate(uint32_t now);
const MotionState& motionState();
uint32_t currentSendIntervalMs();
uint8_t sendIntervalLevel();
uint32_t sendIntervalMovingSec();
uint32_t motionStationaryConfirmationRemainingMs();
const char* motionModeName();

// Sensibilité sélectionnée par le dashboard (1..5), mémorisée en NVS.
uint8_t motionSensitivityLevel();
float motionSensitivityThresholdDps();
bool setMotionSensitivityLevel(uint8_t level);

#endif
