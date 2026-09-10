#pragma once

#include <Arduino.h>

struct MotionState {
  bool calibrated = false;
  bool moving = false;
  bool stationaryConfirmed = false;
  uint32_t lastMotionAt = 0;
  uint32_t stationarySince = 0;
  float xDps = 0.0f;
  float yDps = 0.0f;
  float zDps = 0.0f;
  float motionDps = 0.0f;
};

// Firmware-only: active -> idle uses an 8 s no-motion confirmation.
constexpr uint32_t MOTION_IDLE_CONFIRM_SEC = 8;
// Keep the original 30 s startup confirmation before entering idle.
constexpr uint32_t MOTION_INITIAL_CONFIRM_SEC = 30;
constexpr uint32_t GYRO_MOTION_CONFIRM_SEC = 2;

void motionBegin();
void motionUpdate(uint32_t now);
const MotionState& motionState();
uint32_t motionStationaryConfirmationRemainingMs();
uint32_t currentSendIntervalMs();

uint8_t motionSensitivityLevel();
float motionSensitivityThresholdDps();
bool setMotionSensitivityLevel(uint8_t level);

uint32_t motionActiveIntervalSec();
uint32_t motionIdleIntervalSec();
bool setMotionIntervals(uint32_t activeSeconds, uint32_t idleSeconds);

// External telemetry is deliberately reduced to the two user-visible states.
// The actual state is produced exclusively by the LSM6DS3 logic in MotionManager.cpp.
const char* motionModeName();
