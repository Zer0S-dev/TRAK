#pragma once
#include <Arduino.h>

// Persistent TRAK device configuration.
// URL and API key are stored in ESP32 NVS. The API key is generated once
// from the eFuse MAC plus hardware RNG and is never hard-coded in firmware.
void trakConfigBegin();
void trakConfigTask();

String trakWebAppUrl();
String trakApiKey();
bool trakConfigReady();

// USB/Serial provisioning commands:
//   SETURL https://example.com/trak/
//   SHOWCONFIG
//   RESETCONFIG
