#pragma once

#include <Arduino.h>
#include "ModemManager.h"
#include "MotionManager.h"

// TrackingEngine v2 : séparation production / transport.
// Le point GNSS est d'abord persisté dans le FIFO SD, puis un task
// indépendant se charge de l'expédier vers Trackserver.
void trackingEngineBegin();
void trackingEngineUpdate(uint32_t now, const GnssData& gps, const MotionState& motion);
uint32_t trackingEngineTxCount();
uint32_t trackingEngineLastTxAt();
