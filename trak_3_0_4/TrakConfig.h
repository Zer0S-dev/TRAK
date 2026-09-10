#pragma once
#include <Arduino.h>

// Persistent TRAK provisioning configuration.
// Stored in ESP32 NVS namespace "trak_cfg".
// Wi-Fi profiles remain independent in "trak_wifi".
void trakConfigBegin();
void trakConfigTask();
void trakConfigHandleCommand(const String& command);

String trakWebAppUrl();
String trakApiKey();
String trakUserPhone();
String trakPhone();
bool trakConfigProvisioned();
bool trakConfigReady();

bool trakConfigSetServerUrl(const String& url);
bool trakConfigSetUserPhone(const String& phone);
bool trakConfigSetTrakPhone(const String& phone);
bool trakConfigSetProvisioned(bool value);
bool trakConfigRegenerateApiKey();

// Erase only server/personal provisioning data. Never touches "trak_wifi".
void trakConfigResetProvisioning();
