#pragma once
#include <Arduino.h>

// Persistent TRAK provisioning configuration.
// Stored in ESP32 NVS namespace "trak_cfg".
// Wi-Fi profiles remain independent in "trak_wifi".
void trakConfigBegin();
void trakConfigTask();

String trakWebAppUrl();
String trakApiKey();
String trakUserPhone();
String trakPhone();
bool trakConfigProvisioned();
bool trakConfigReady();

// Update persistent provisioning values. These will be used by the Wizard
// integration in phase 2; they are kept separate from Wi-Fi profiles.
bool trakConfigSetServerUrl(const String& url);
bool trakConfigSetUserPhone(const String& phone);
bool trakConfigSetTrakPhone(const String& phone);
bool trakConfigSetProvisioned(bool value);

// Erase only server/personal provisioning data. Never touches "trak_wifi".
void trakConfigResetProvisioning();

// USB/Serial provisioning commands kept for development:
//   SETURL https://example.com/trak/
//   SHOWCONFIG
//   SHOWKEY
//   RESETCONFIG
