#pragma once
#include <Arduino.h>

void wifiManagerBegin();
bool wifiConnectBestSaved();
bool wifiInternetAvailable();
void wifiNetworkTick(bool cellularAvailable);
bool wifiIsActive();
int wifiSignalPercent();
bool wifiPostJson(const String& json, int& httpStatus);
bool wifiSyncProfilesFromServer();
uint8_t wifiProfileCount();
void wifiSetProfile(uint8_t slot, const char* ssid, const char* password);
void wifiClearProfile(uint8_t slot);
