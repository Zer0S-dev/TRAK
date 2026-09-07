#pragma once

#include <Arduino.h>
#include "ModemManager.h"
#include "MotionManager.h"

enum class SentinelState : uint8_t {
  OFF = 0,
  ARMING = 1,
  ON = 2,
  DISARMING = 3
};

void sentinelBegin();
bool sentinelSetUserPhone(const char* phone);
bool sentinelClearUserPhone();
bool sentinelGetUserPhone(char* out, size_t outSize);
bool sentinelHasUserPhone();
bool sentinelSetTrakPhone(const char* phone);
bool sentinelClearTrakPhone();
bool sentinelGetTrakPhone(char* out, size_t outSize);
bool sentinelHasTrakPhone();
// Reutilise exactement le SMS de position utilise par Sentinel.
bool sentinelSendPositionSms(const GnssData& gps);

SentinelState sentinelState();
const char* sentinelStateName();
uint32_t sentinelConfirmationRemainingMs();

bool sentinelAdvance();
void sentinelUpdate(uint32_t now, const GnssData& gps, const MotionState& motion);
