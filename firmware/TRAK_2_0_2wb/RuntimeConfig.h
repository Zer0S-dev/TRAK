#pragma once

#include <Arduino.h>
#include "Config.h"

constexpr uint8_t MAX_WIFI_PROFILES = 3;
constexpr size_t WIFI_SSID_MAX_LEN = 32;
constexpr size_t WIFI_PASSWORD_MAX_LEN = 64;

enum class NetworkMode : uint8_t {
  WIFI = 1,
  CELLULAR = 0
};

struct WiFiProfile {
  char ssid[WIFI_SSID_MAX_LEN + 1] = {};
  char password[WIFI_PASSWORD_MAX_LEN + 1] = {};
  bool valid = false;
};

void runtimeConfigBegin();
NetworkMode getNetworkMode();
bool isWiFiMode();
void setNetworkMode(NetworkMode mode);
const char* networkModeName();

struct TrackserverConfig {
  char url[TRACKSERVER_URL_MAX_LEN + 1] = {};
  char host[TRACKSERVER_HOST_MAX_LEN + 1] = {};
  char path[TRACKSERVER_PATH_MAX_LEN + 1] = {};
};

bool getTrackserverConfig(TrackserverConfig& out);
bool saveTrackserverUrl(const char* url);

bool getWiFiProfile(uint8_t slot, WiFiProfile& out);
bool saveWiFiProfile(uint8_t slot, const char* ssid, const char* password);
bool removeWiFiProfile(uint8_t slot);
uint8_t wifiProfileCount();

struct DataUsage {
  uint16_t year = 0;
  uint8_t month = 0;
  uint64_t wifiBytes = 0;
  uint64_t cellularBytes = 0;
  uint32_t planMb = DATA_PLAN_DEFAULT_MB;
  float operatorCoefficient = DATA_OPERATOR_COEF_DEFAULT;
};

void getDataUsage(DataUsage& out);
bool saveDataUsageSettings(uint32_t planMb, float operatorCoefficient);
void recordDataUsage(bool cellular, uint32_t bytes, uint16_t year, uint8_t month);
