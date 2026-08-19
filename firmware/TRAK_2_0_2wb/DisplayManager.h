#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>

#include "Config.h"
#include "ModemManager.h"

extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2;

void initDisplay();
int getWiFiSignalPercent();
void updateOLED(const GnssData& gps, int signalPercent, bool httpSuccess,
                uint32_t txCount, uint32_t lastTxAgeMs, bool constellationPage, bool wifiActive, int8_t activeWiFiSlot);

#endif
