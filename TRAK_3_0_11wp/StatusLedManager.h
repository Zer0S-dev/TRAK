#pragma once
#include <Arduino.h>
#include <FastLED.h>
#include "StatusLedAnimations.h"

void statusLedBegin();
void statusLedSetNetwork(bool wifiActive, bool cellularActive);
void statusLedSetNetworkConnecting(bool wifiConnecting, bool cellularConnecting);
void statusLedSetGpsFix(bool hasFix);
void statusLedPulseWiFi();
void statusLedPulseCellular();
void statusLedHttpStartWiFi();
void statusLedHttpStartCellular();
void statusLedHttpStop();
void statusLedService();

// Primitives de rendu : utilisees uniquement par StatusLedAnimations.cpp.
void statusLedSetPixel(uint8_t index, const CRGB &color);
void statusLedSetAllPixels(const CRGB &color);
void statusLedClearPixels();
void statusLedShowPixels();
