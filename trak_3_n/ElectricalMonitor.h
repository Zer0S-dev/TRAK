#pragma once

#include <Arduino.h>

struct ElectricalSample {
  bool valid = false;
  float volt = 0.0f; // Tension cote charge, en volts.
  float amp = 0.0f;  // Courant, en amperes.
  uint32_t measuredAtMs = 0;
};

// Le bus Wire est initialise par MotionManager::motionBegin().
// Ce module ne doit jamais appeler Wire.begin() ni Wire.setClock().
bool electricalMonitorBegin();
void electricalMonitorUpdate(uint32_t now);
ElectricalSample electricalMonitorSnapshot();
