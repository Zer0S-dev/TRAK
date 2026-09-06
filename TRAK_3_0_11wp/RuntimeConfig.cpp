#include "RuntimeConfig.h"
#include "Config.h"
#include "DevLog.h"
#include <Preferences.h>

namespace {
Preferences prefs;
SemaphoreHandle_t configMutex = nullptr;
DataUsage dataUsage;
uint8_t dataUsagePendingWrites = 0;

bool validSlot(uint8_t slot) { return slot < MAX_WIFI_PROFILES; }

void copyString(char* dst, size_t dstSize, const String& src)
{
  if (dstSize == 0) return;
  src.toCharArray(dst, dstSize);
  dst[dstSize - 1] = '\0';
}

bool lockConfig(uint32_t timeoutMs = 100)
{
  return configMutex == nullptr || xSemaphoreTake(configMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void unlockConfig()
{
  if (configMutex != nullptr) xSemaphoreGive(configMutex);
}

TrackserverConfig trackserver;

bool parseTrackserverUrl(const char* input, TrackserverConfig& out)
{
  if (input == nullptr) return false;

  while (*input && isspace((unsigned char)*input)) ++input;
  const size_t len = strlen(input);
  if (len == 0 || len > TRACKSERVER_URL_MAX_LEN) return false;

  char normalized[TRACKSERVER_URL_MAX_LEN + 1] = {};
  memcpy(normalized, input, len);
  normalized[len] = '\0';

  char* end = normalized + strlen(normalized);
  while (end > normalized && isspace((unsigned char)end[-1])) {
    *--end = '\0';
  }

  constexpr const char* prefix = "http://";
  constexpr size_t prefixLen = 7;
  if (strncmp(normalized, prefix, prefixLen) != 0) return false;

  const char* hostStart = normalized + prefixLen;
  const char* pathStart = strchr(hostStart, '/');
  const char* queryStart = strchr(hostStart, '?');
  if (pathStart == nullptr || (queryStart != nullptr && queryStart < pathStart)) {
    pathStart = queryStart;
  }

  const char* hostEnd = pathStart ? pathStart : normalized + strlen(normalized);
  const size_t hostLen = (size_t)(hostEnd - hostStart);
  if (hostLen == 0 || hostLen > TRACKSERVER_HOST_MAX_LEN) return false;

  // Le firmware actuel utilise le port HTTP 80. On refuse donc un port explicite.
  if (memchr(hostStart, ':', hostLen) != nullptr) return false;

  const size_t pathLen = strlen(pathStart ? pathStart : "/");
  if (pathLen == 0 || pathLen > TRACKSERVER_PATH_MAX_LEN) return false;

  memset(&out, 0, sizeof(out));
  memcpy(out.url, normalized, strlen(normalized));
  memcpy(out.host, hostStart, hostLen);
  if (pathStart) {
    memcpy(out.path, pathStart, pathLen);
  } else {
    strcpy(out.path, "/");
  }
  return true;
}

void loadDataUsageUnlocked()
{
  dataUsage.year = prefs.getUShort("duyear", 0);
  dataUsage.month = prefs.getUChar("dumonth", 0);
  dataUsage.wifiBytes = prefs.getULong64("duwifi", 0);
  dataUsage.cellularBytes = prefs.getULong64("ducell", 0);
  dataUsage.planMb = prefs.getULong("duplan", DATA_PLAN_DEFAULT_MB);
  dataUsage.operatorCoefficient = prefs.getFloat("ducoef", DATA_OPERATOR_COEF_DEFAULT);
  if (dataUsage.planMb == 0) dataUsage.planMb = DATA_PLAN_DEFAULT_MB;
  if (dataUsage.operatorCoefficient < 1.0f || dataUsage.operatorCoefficient > 3.0f)
    dataUsage.operatorCoefficient = DATA_OPERATOR_COEF_DEFAULT;
}

void persistDataUsageUnlocked()
{
  prefs.putUShort("duyear", dataUsage.year);
  prefs.putUChar("dumonth", dataUsage.month);
  prefs.putULong64("duwifi", dataUsage.wifiBytes);
  prefs.putULong64("ducell", dataUsage.cellularBytes);
  prefs.putULong("duplan", dataUsage.planMb);
  prefs.putFloat("ducoef", dataUsage.operatorCoefficient);
  dataUsagePendingWrites = 0;
}

} // namespace

void runtimeConfigBegin()
{
  prefs.begin("tracker", false);
  configMutex = xSemaphoreCreateMutex();
  loadDataUsageUnlocked();

  TrackserverConfig loaded;
  const String storedUrl = prefs.getString("tsurl", TRACKSERVER_DEFAULT_BASE);
  if (!parseTrackserverUrl(storedUrl.c_str(), loaded)) {
    parseTrackserverUrl(TRACKSERVER_DEFAULT_BASE, loaded);
  }
  trackserver = loaded;

  // Aucun profil Wi-Fi n'est injecté automatiquement. Les profils sont créés par l'utilisateur depuis le Dashboard.
}

bool getTrackserverConfig(TrackserverConfig& out)
{
  if (!lockConfig()) return false;
  out = trackserver;
  unlockConfig();
  return true;
}

bool saveTrackserverUrl(const char* url)
{
  TrackserverConfig parsed;
  if (!parseTrackserverUrl(url, parsed) || !lockConfig()) return false;

  const bool ok = prefs.putString("tsurl", parsed.url) > 0;
  if (ok) trackserver = parsed;
  unlockConfig();
  return ok;
}

void getDataUsage(DataUsage& out)
{
  if (!lockConfig()) {
    out = DataUsage{};
    return;
  }
  out = dataUsage;
  unlockConfig();
}

bool saveDataUsageSettings(uint32_t planMb, float operatorCoefficient)
{
  if (planMb == 0 || planMb > 1000000 || operatorCoefficient < 1.0f || operatorCoefficient > 3.0f)
    return false;
  if (!lockConfig()) return false;
  dataUsage.planMb = planMb;
  dataUsage.operatorCoefficient = operatorCoefficient;
  persistDataUsageUnlocked();
  unlockConfig();
  return true;
}

uint8_t sendIntervalLevel()
{
  if (!lockConfig()) return SEND_INTERVAL_MOVING_SEC_DEFAULT_LEVEL;
  uint8_t level = prefs.getUChar("sendint", SEND_INTERVAL_MOVING_SEC_DEFAULT_LEVEL);
  unlockConfig();
  if (level < 1 || level > 5) return SEND_INTERVAL_MOVING_SEC_DEFAULT_LEVEL;
  return level;
}

bool setSendIntervalLevel(uint8_t level)
{
  if (level < 1 || level > 5) return false;

  // Lire, écrire et vérifier sous le même verrou.
  // Cela évite les échecs intermittents liés à des accès concurrents à la NVS.
  if (!lockConfig()) {
    DevSerial.println("[TRACK] Interval save failed: config lock timeout");
    return false;
  }

  const uint8_t oldLevel = prefs.getUChar("sendint", SEND_INTERVAL_MOVING_SEC_DEFAULT_LEVEL);
  const bool writeOk = prefs.putUChar("sendint", level) == 1;
  const uint8_t storedLevel = prefs.getUChar("sendint", 0);
  unlockConfig();

  if (!writeOk || storedLevel != level) {
    DevSerial.printf("[TRACK] Interval save failed: requested=%u stored=%u\n",
                     (unsigned)level, (unsigned)storedLevel);
    return false;
  }

  if (oldLevel != level) {
    const uint32_t oldSeconds = (oldLevel >= 1 && oldLevel <= 5)
        ? (oldLevel == 1 ? SEND_INTERVAL_MOVING_SEC_VALUE_1
           : oldLevel == 2 ? SEND_INTERVAL_MOVING_SEC_VALUE_2
           : oldLevel == 3 ? SEND_INTERVAL_MOVING_SEC_VALUE_3
           : oldLevel == 4 ? SEND_INTERVAL_MOVING_SEC_VALUE_4
                            : SEND_INTERVAL_MOVING_SEC_VALUE_5)
        : SEND_INTERVAL_MOVING_SEC_VALUE_3;

    const uint32_t newSeconds = (level == 1 ? SEND_INTERVAL_MOVING_SEC_VALUE_1
        : level == 2 ? SEND_INTERVAL_MOVING_SEC_VALUE_2
        : level == 3 ? SEND_INTERVAL_MOVING_SEC_VALUE_3
        : level == 4 ? SEND_INTERVAL_MOVING_SEC_VALUE_4
                      : SEND_INTERVAL_MOVING_SEC_VALUE_5);

    DevSerial.printf("[TRACK] Interval changed: %lus -> %lus\n",
                     (unsigned long)oldSeconds,
                     (unsigned long)newSeconds);
  }

  return true;
}

uint32_t sendIntervalMovingSec()
{
  switch (sendIntervalLevel()) {
    case 1: return SEND_INTERVAL_MOVING_SEC_VALUE_1;
    case 2: return SEND_INTERVAL_MOVING_SEC_VALUE_2;
    case 4: return SEND_INTERVAL_MOVING_SEC_VALUE_4;
    case 5: return SEND_INTERVAL_MOVING_SEC_VALUE_5;
    default: return SEND_INTERVAL_MOVING_SEC_VALUE_3;
  }
}

void recordDataUsage(bool cellular, uint32_t bytes, uint16_t year, uint8_t month)
{
  if (year < 2000 || month < 1 || month > 12 || !lockConfig()) return;

  const bool monthChanged =
      dataUsage.year != year || dataUsage.month != month;

  if (monthChanged) {
    dataUsage.year = year;
    dataUsage.month = month;
    dataUsage.wifiBytes = 0;
    dataUsage.cellularBytes = 0;
    dataUsagePendingWrites = 0;
  }

  if (bytes > 0) {
    if (cellular) dataUsage.cellularBytes += bytes;
    else dataUsage.wifiBytes += bytes;
    ++dataUsagePendingWrites;
  }

  if (monthChanged || dataUsagePendingWrites >= 10) {
    persistDataUsageUnlocked();
  }
  unlockConfig();
}

static uint32_t g_wifiProfileRevision = 0;

bool getWiFiProfile(uint8_t slot, WiFiProfile& out)
{
  out = WiFiProfile{};
  if (!validSlot(slot) || !lockConfig()) return false;

  String ssidKey = String("ssid") + slot;
  String passKey = String("pass") + slot;
  String ssid = prefs.getString(ssidKey.c_str(), "");
  String pass = prefs.getString(passKey.c_str(), "");

  if (ssid.length() == 0) {
    unlockConfig();
    return false;
  }

  copyString(out.ssid, sizeof(out.ssid), ssid);
  copyString(out.password, sizeof(out.password), pass);
  out.valid = true;
  unlockConfig();
  return true;
}

bool saveWiFiProfile(uint8_t slot, const char* ssid, const char* password)
{
  if (!validSlot(slot) || ssid == nullptr || password == nullptr) return false;
  if (strlen(ssid) == 0 || strlen(ssid) > WIFI_SSID_MAX_LEN) return false;
  if (strlen(password) > WIFI_PASSWORD_MAX_LEN) return false;
  if (!lockConfig()) return false;

  String ssidKey = String("ssid") + slot;
  String passKey = String("pass") + slot;
  prefs.putString(ssidKey.c_str(), ssid);
  prefs.putString(passKey.c_str(), password);
  ++g_wifiProfileRevision;
  unlockConfig();
  return true;
}

bool removeWiFiProfile(uint8_t slot)
{
  if (!validSlot(slot) || !lockConfig()) return false;

  String ssidKey = String("ssid") + slot;
  String passKey = String("pass") + slot;
  prefs.remove(ssidKey.c_str());
  prefs.remove(passKey.c_str());
  ++g_wifiProfileRevision;
  unlockConfig();
  return true;
}

uint32_t wifiProfileRevision()
{
  return g_wifiProfileRevision;
}

uint8_t wifiProfileCount()
{
  uint8_t count = 0;
  for (uint8_t i = 0; i < MAX_WIFI_PROFILES; ++i) {
    WiFiProfile profile;
    if (getWiFiProfile(i, profile)) ++count;
  }
  return count;
}
