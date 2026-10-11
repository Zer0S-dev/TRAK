#pragma once

#include <Arduino.h>

struct ElectricalSample {
  bool valid = false;
  float volt = 0.0f; // Tension cote charge, en volts.
  float amp = 0.0f;  // Courant, en amperes.
  uint32_t measuredAtMs = 0;
};

// Le bus Wire est initialise par MotionManager::motionBegin().
// Ne pas initialiser explicitement le bus ici. Adafruit BusIO appelle
// Wire.begin() en interne, mais sur ESP32 cela reste sans effet si le bus
// est deja initialise : les broches et la frequence du LSM6DS3 sont conservees.
bool electricalMonitorBegin();
void electricalMonitorUpdate(uint32_t now);
ElectricalSample electricalMonitorSnapshot();
