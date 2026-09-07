#include "NetworkManager.h"
#include <WiFi.h>
#include "freertos/FreeRTOS.h"
#include "Config.h"
#include "RuntimeConfig.h"
#include "ModemManager.h"
#include "DevLog.h"
#include "TrackerState.h"
#include "StatusLedManager.h"

// Forward declaration: used by the internal Wi-Fi connection helper below.
const char* wifiStatusName(wl_status_t status);

namespace {

static int8_t trackedWiFiSlot = -1;
static char trackedWiFiSsid[WIFI_SSID_MAX_LEN + 1] = {};
static char trackedWiFiPassword[WIFI_PASSWORD_MAX_LEN + 1] = {};

// NetworkManager owns only the physical network path. Application-level
// HTTP failures never cause a Wi-Fi/4G switch: Trackserver and TRAK Connect
// are allowed to recover independently at the uplink layer.
static constexpr uint32_t WIFI_LOSS_CONFIRM_MS = 1500UL;
static constexpr uint32_t CELLULAR_CHECK_MS = 5000UL;
static constexpr uint32_t WIFI_RETURN_SCAN_MS = 30000UL;
static constexpr uint32_t NETWORK_RETRY_MS = 3000UL;
static constexpr uint32_t WIFI_SCAN_WATCHDOG_MS = 8000UL;

static uint32_t wifiLostSince = 0;
static uint32_t lastCellularCheck = 0;
static uint32_t lastWiFiReturnScan = 0;
static uint32_t lastRecoveryAttempt = 0;
static bool wifiScanRunning = false;
static bool cellularUsable = false;

static bool configuredWiFiProfile(uint8_t slot, WiFiProfile& profile)
{
  if (slot >= MAX_WIFI_PROFILES) return false;
  if (!getWiFiProfile(slot, profile)) return false;
  return profile.ssid[0] != '\0';
}

static void rememberWiFiProfile(uint8_t slot, const WiFiProfile& profile)
{
  trackedWiFiSlot = static_cast<int8_t>(slot);
  strncpy(trackedWiFiSsid, profile.ssid, sizeof(trackedWiFiSsid) - 1);
  trackedWiFiSsid[sizeof(trackedWiFiSsid) - 1] = '\0';
  strncpy(trackedWiFiPassword, profile.password, sizeof(trackedWiFiPassword) - 1);
  trackedWiFiPassword[sizeof(trackedWiFiPassword) - 1] = '\0';
}

static void clearWiFiTracking()
{
  trackedWiFiSlot = -1;
  trackedWiFiSsid[0] = '\0';
  trackedWiFiPassword[0] = '\0';
}

static bool activeWiFiStillValid()
{
  if (WiFi.status() != WL_CONNECTED) return false;
  if (trackedWiFiSlot < 0) return false;

  WiFiProfile profile;
  if (!configuredWiFiProfile(static_cast<uint8_t>(trackedWiFiSlot), profile)) return false;
  if (strcmp(profile.ssid, trackedWiFiSsid) != 0) return false;
  if (strcmp(profile.password, trackedWiFiPassword) != 0) return false;
  if (WiFi.SSID() != profile.ssid) return false;

  return true;
}

static int findVisibleConfiguredProfile()
{
  const int count = WiFi.scanComplete();
  if (count < 0) return -1;

  for (uint8_t slot = 0; slot < MAX_WIFI_PROFILES; ++slot) {
    WiFiProfile profile;
    if (!configuredWiFiProfile(slot, profile)) continue;

    for (int i = 0; i < count; ++i) {
      if (WiFi.SSID(i) == profile.ssid) return slot;
    }
  }

  return -1;
}

static void startWiFiReturnScan()
{
  if (wifiScanRunning) return;
  if (wifiProfileCount() == 0) return;

  WiFi.mode(WIFI_AP_STA);
  const int result = WiFi.scanNetworks(true, true, false, 300);
  if (result == WIFI_SCAN_RUNNING) {
    wifiScanRunning = true;
    return;
  }

  // Some Arduino-ESP32 versions can return the number of results directly.
  if (result >= 0) {
    wifiScanRunning = false;
    return;
  }

  DevSerial.println("[WIFI] Scan retour impossible, 4G conservee.");
}

static void finishWiFiReturnScan()
{
  if (!wifiScanRunning) return;

  const int result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) {
    if (millis() - lastWiFiReturnScan > WIFI_SCAN_WATCHDOG_MS) {
      WiFi.scanDelete();
      wifiScanRunning = false;
    }
    return;
  }

  wifiScanRunning = false;

  if (result < 0) {
    WiFi.scanDelete();
    return;
  }

  const int slot = findVisibleConfiguredProfile();
  if (slot < 0) {
    WiFi.scanDelete();
    return;
  }

  WiFiProfile profile;
  if (!configuredWiFiProfile(static_cast<uint8_t>(slot), profile)) {
    WiFi.scanDelete();
    return;
  }

  DevSerial.print("[WIFI] Profil #");
  DevSerial.print(slot + 1);
  DevSerial.println(" retrouve : tentative de reconnexion.");

  WiFi.disconnect(false, false);
  vTaskDelay(pdMS_TO_TICKS(50));
  WiFi.begin(profile.ssid, profile.password);
  WiFi.scanDelete();

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == profile.ssid) {
    rememberWiFiProfile(static_cast<uint8_t>(slot), profile);
    setActiveNetwork(ActiveNetwork::WIFI, static_cast<int8_t>(slot));
    cellularUsable = false;
    wifiLostSince = 0;
    DevSerial.print("[NET] Wi-Fi retrouve : ");
    DevSerial.println(profile.ssid);
    return;
  }

  DevSerial.println("[WIFI] Reconnexion impossible, 4G conservee.");
}

static bool connectWiFiProfileInternal(uint8_t slot, uint32_t timeoutMs)
{
  WiFiProfile profile;
  if (!configuredWiFiProfile(slot, profile)) return false;

  DevSerial.print("[WIFI] Profil #");
  DevSerial.print(slot + 1);
  DevSerial.print(" : ");
  DevSerial.println(profile.ssid);

  statusLedSetNetworkConnecting(true, false);

  WiFi.disconnect(false, false);
  vTaskDelay(pdMS_TO_TICKS(50));
  WiFi.begin(profile.ssid, profile.password);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (WiFi.status() != WL_CONNECTED) {
    DevSerial.print("[WIFI] Echec profil #");
    DevSerial.print(slot + 1);
    DevSerial.print(" : ");
    DevSerial.println(wifiStatusName(WiFi.status()));
    return false;
  }

  rememberWiFiProfile(slot, profile);
  DevSerial.print("[WIFI] Connecte a ");
  DevSerial.print(profile.ssid);
  DevSerial.print(" | IP : ");
  DevSerial.println(WiFi.localIP());
  return true;
}

static bool cellularRegistrationOK()
{
  if (!modemCommunicationReady()) return false;

  const String reg = sendATCommand("AT+CEREG?", 1000);
  const int p = reg.indexOf("+CEREG:");
  if (p < 0) return cellularUsable;

  const int comma = reg.indexOf(',', p);
  if (comma < 0) return cellularUsable;

  const int stat = reg.substring(comma + 1).toInt();
  return stat == 1 || stat == 5;
}

} // namespace

int getWiFiSignalPercent()
{
  if (WiFi.status() != WL_CONNECTED) return 0;
  const int rssi = WiFi.RSSI();
  if (rssi <= -100) return 0;
  if (rssi >= -50) return 100;
  return constrain(2 * (rssi + 100), 0, 100);
}

void networkManagerBegin()
{
  trackedWiFiSlot = -1;
  trackedWiFiSsid[0] = '\0';
  trackedWiFiPassword[0] = '\0';
  wifiLostSince = 0;
  lastCellularCheck = 0;
  lastWiFiReturnScan = 0;
  lastRecoveryAttempt = 0;
  wifiScanRunning = false;
  cellularUsable = false;
}

const char* wifiStatusName(wl_status_t status)
{
  switch (status) {
    case WL_NO_SHIELD:        return "NO_SHIELD";
    case WL_IDLE_STATUS:      return "IDLE";
    case WL_NO_SSID_AVAIL:    return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED:   return "SCAN_COMPLETED";
    case WL_CONNECTED:        return "CONNECTED";
    case WL_CONNECT_FAILED:   return "CONNECT_FAILED";
    case WL_CONNECTION_LOST:  return "CONNECTION_LOST";
    case WL_DISCONNECTED:     return "DISCONNECTED";
    default:                  return "UNKNOWN";
  }
}

bool connectWiFiProfile(uint8_t slot, uint32_t timeoutMs)
{
  WiFi.mode(WIFI_AP_STA);
  if (!connectWiFiProfileInternal(slot, timeoutMs)) return false;

  WiFi.scanDelete();
  setActiveNetwork(ActiveNetwork::WIFI, static_cast<int8_t>(slot));
  wifiLostSince = 0;
  return true;
}

bool connectWiFi()
{
  WiFi.mode(WIFI_AP_STA);

  if (activeWiFiStillValid()) {
    setActiveNetwork(ActiveNetwork::WIFI, trackedWiFiSlot);
    return true;
  }

  const uint8_t profileCount = wifiProfileCount();
  if (profileCount == 0) return false;

  const int scanResult = WiFi.scanNetworks(false, true, false, 300);
  if (scanResult <= 0) {
    if (scanResult == 0) DevSerial.println("[WIFI] Aucun profil configure visible.");
    else DevSerial.println("[WIFI] Scan impossible.");
    WiFi.scanDelete();
    return false;
  }

  // Profiles remain ordered: #1, #2, #3.
  for (uint8_t slot = 0; slot < MAX_WIFI_PROFILES; ++slot) {
    WiFiProfile profile;
    if (!configuredWiFiProfile(slot, profile)) continue;

    bool visible = false;
    for (int i = 0; i < scanResult; ++i) {
      if (WiFi.SSID(i) == profile.ssid) {
        visible = true;
        break;
      }
    }
    if (!visible) continue;

    WiFi.scanDelete();
    if (connectWiFiProfileInternal(slot, WIFI_CONNECT_TIMEOUT_MS)) {
      setActiveNetwork(ActiveNetwork::WIFI, static_cast<int8_t>(slot));
      wifiLostSince = 0;
      return true;
    }

    WiFi.scanNetworks(false, true, false, 300);
  }

  WiFi.scanDelete();
  return false;
}

bool activateWiFi()
{
  return connectWiFi();
}

bool activateCellularAndPublish()
{
  statusLedSetNetworkConnecting(false, true);

  if (!modemCommunicationReady()) {
    DevSerial.println("[4G] Modem pas encore pret.");
    return false;
  }

  if (!connectCellularNetwork()) return false;

  setActiveNetwork(ActiveNetwork::CELLULAR);
  cellularUsable = true;
  DevSerial.println("[NET] 4G active.");
  return true;
}

void networkReportTrackserverResult(bool success)
{
  // Deliberately diagnostic only. HTTP/session errors are not proof that the
  // physical network is down and must never force a Wi-Fi/4G transition.
  if (!success) {
    DevSerial.println("[NET] Trackserver echec applicatif : transport conserve.");
  }
}

static void handleWiFiState(uint32_t now)
{
  if (activeWiFiStillValid()) {
    wifiLostSince = 0;
    return;
  }

  if (wifiLostSince == 0) wifiLostSince = now;
  if (now - wifiLostSince < WIFI_LOSS_CONFIRM_MS) return;
  if (now - lastRecoveryAttempt < NETWORK_RETRY_MS) return;

  lastRecoveryAttempt = now;
  DevSerial.println("[NET] Wi-Fi reellement perdu -> bascule 4G.");
  clearWiFiTracking();
  WiFi.disconnect(false, false);
  setActiveNetwork(ActiveNetwork::NONE);

  if (!activateCellularAndPublish()) {
    DevSerial.println("[NET] 4G indisponible -> OFFLINE.");
    setActiveNetwork(ActiveNetwork::NONE);
  }
}

static void handleCellularState(uint32_t now)
{
  // Never run Wi-Fi reconnect attempts continuously. A 30 s async scan keeps
  // 4G stable while still respecting the project's Wi-Fi priority.
  if (wifiProfileCount() > 0) {
    if (!wifiScanRunning && now - lastWiFiReturnScan >= WIFI_RETURN_SCAN_MS) {
      lastWiFiReturnScan = now;
      startWiFiReturnScan();
    }
    finishWiFiReturnScan();
    if (getActiveNetwork() == ActiveNetwork::WIFI) return;
  }

  if (now - lastCellularCheck < CELLULAR_CHECK_MS) return;
  lastCellularCheck = now;

  const bool registered = cellularRegistrationOK();
  if (registered) {
    cellularUsable = true;
    return;
  }

  cellularUsable = false;
  DevSerial.println("[NET] 4G radio indisponible -> recherche Wi-Fi.");
  setActiveNetwork(ActiveNetwork::NONE);

  if (wifiProfileCount() > 0 && connectWiFi()) return;

  if (now - lastRecoveryAttempt >= NETWORK_RETRY_MS) {
    lastRecoveryAttempt = now;
    if (activateCellularAndPublish()) return;
  }

  setActiveNetwork(ActiveNetwork::NONE);
}

static void handleNoNetworkState(uint32_t now)
{
  if (now - lastRecoveryAttempt < NETWORK_RETRY_MS) return;
  lastRecoveryAttempt = now;

  // Strict preference: Wi-Fi first, then A7670 cellular.
  if (wifiProfileCount() > 0 && connectWiFi()) return;
  if (activateCellularAndPublish()) return;

  setActiveNetwork(ActiveNetwork::NONE);
}

void networkTask(void* parameter)
{
  (void)parameter;
  vTaskDelay(pdMS_TO_TICKS(1500));

  lastRecoveryAttempt = millis() - NETWORK_RETRY_MS;
  lastCellularCheck = millis() - CELLULAR_CHECK_MS;
  lastWiFiReturnScan = millis() - WIFI_RETURN_SCAN_MS;

  DevSerial.println("[NET] Gestion reseau 3.0.11 : Wi-Fi prioritaire / 4G de secours / bascule stable");

  while (true) {
    const uint32_t now = millis();

    switch (getActiveNetwork()) {
      case ActiveNetwork::WIFI:
        handleWiFiState(now);
        break;

      case ActiveNetwork::CELLULAR:
        handleCellularState(now);
        break;

      case ActiveNetwork::NONE:
      default:
        handleNoNetworkState(now);
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
