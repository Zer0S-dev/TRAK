#pragma once
#include <Arduino.h>

void wifiManagerBegin();
bool wifiConnectBestSaved();
bool wifiInternetAvailable();
void wifiNetworkTick(bool cellularAvailable);
void wifiForceCellular();
bool wifiIsActive();
int wifiSignalPercent();
uint8_t wifiProfileCount();
void wifiSetProfile(uint8_t slot, const char* ssid, const char* password);
void wifiClearProfile(uint8_t slot);
