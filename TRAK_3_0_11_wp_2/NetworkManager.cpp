#include "NetworkManager.h"
#include <WiFi.h>
#include "freertos/FreeRTOS.h"
#include "Config.h"
#include "RuntimeConfig.h"
#include "ModemManager.h"
#include "DevLog.h"
#include "TrackerState.h"
#include "StatusLedManager.h"

static int8_t trackedWiFiSlot = -1;
static char trackedWiFiSsid[WIFI_SSID_MAX_LEN + 1] = {};
static char trackedWiFiPassword[WIFI_PASSWORD_MAX_LEN + 1] = {};

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
}

const char* wifiStatusName(wl_status_t status)
{
  switch (status) {
    case WL_NO_SHIELD:       return "NO_SHIELD";
    case WL_IDLE_STATUS:    return "IDLE";
    case WL_NO_SSID_AVAIL:  return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
    case WL_CONNECTED:      return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST:return "CONNECTION_LOST";
    case WL_DISCONNECTED:   return "DISCONNECTED";
    default:                return "UNKNOWN";
  }
}

// Recherche une seule fois les réseaux visibles. La machine d'état réseau
// reste ainsi séquentielle : on scanne le Wi-Fi, puis on tente les profils
// configurés dans leur ordre de priorité.
static bool scanForConfiguredWiFiProfiles()
{
  const int count = WiFi.scanNetworks(false, true, false, 300);
  if (count <= 0) {
    if (count == 0) {
      DevSerial.println("[WIFI] Aucun réseau visible.");
    } else {
      DevSerial.println("[WIFI] Echec du scan Wi-Fi.");
    }
    return false;
  }

  for (uint8_t slot = 0; slot < MAX_WIFI_PROFILES; ++slot) {
    WiFiProfile profile;
    if (!getWiFiProfile(slot, profile)) continue;

    for (int i = 0; i < count; ++i) {
      if (WiFi.SSID(i) == profile.ssid) {
        return true;
      }
    }
  }

  return false;
}

static bool wifiSsidVisible(const char* ssid)
{
  if (ssid == nullptr || ssid[0] == '\0') return false;

  const int count = WiFi.scanComplete();
  if (count <= 0) return false;

  for (int i = 0; i < count; ++i) {
    if (WiFi.SSID(i) == ssid) {
      return true;
    }
  }

  return false;
}

bool connectWiFiProfile(uint8_t slot, uint32_t timeoutMs)
{
  WiFiProfile profile;
  if (!getWiFiProfile(slot, profile)) return false;

  DevSerial.print("[WIFI] Profil #");
  DevSerial.print(slot + 1);
  DevSerial.print(" : ");
  DevSerial.println(profile.ssid);

  statusLedSetNetworkConnecting(true, false);

  WiFi.disconnect(false);
  vTaskDelay(pdMS_TO_TICKS(100));
  WiFi.begin(profile.ssid, profile.password);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  if (WiFi.status() != WL_CONNECTED) {
    DevSerial.print("[WIFI] Echec profil #");
    DevSerial.print(slot + 1);
    DevSerial.print(" : ");
    DevSerial.println(wifiStatusName(WiFi.status()));
    return false;
  }

  DevSerial.print("[WIFI] Connecte a ");
  DevSerial.print(profile.ssid);
  DevSerial.print(" | IP : ");
  DevSerial.println(WiFi.localIP());

  // L'envoi réel à Trackserver valide Internet.
  // Ici on valide uniquement le lien Wi-Fi.
  return true;
}

bool connectWiFi()
{
  WiFi.mode(WIFI_AP_STA);

  DevSerial.println("[NET] Etat WIFI : recherche d'un profil configure...");

  // Si le STA est déjà connecté à un profil configuré, on le conserve.
  if (WiFi.status() == WL_CONNECTED) {
    for (uint8_t slot = 0; slot < MAX_WIFI_PROFILES; ++slot) {
      WiFiProfile profile;
      if (!getWiFiProfile(slot, profile)) continue;

      if (WiFi.SSID() == profile.ssid) {
        trackedWiFiSlot = (int8_t)slot;
        strncpy(trackedWiFiSsid, profile.ssid, sizeof(trackedWiFiSsid) - 1);
        trackedWiFiSsid[sizeof(trackedWiFiSsid) - 1] = '\0';
        strncpy(trackedWiFiPassword, profile.password, sizeof(trackedWiFiPassword) - 1);
        trackedWiFiPassword[sizeof(trackedWiFiPassword) - 1] = '\0';

        setActiveNetwork(ActiveNetwork::WIFI, (int8_t)slot);
        DevSerial.println("[WIFI] Connexion existante conservee.");
        return true;
      }
    }
  }

  if (!scanForConfiguredWiFiProfiles()) {
    return false;
  }

  for (uint8_t slot = 0; slot < MAX_WIFI_PROFILES; ++slot) {
    WiFiProfile profile;
    if (!getWiFiProfile(slot, profile)) continue;

    if (!wifiSsidVisible(profile.ssid)) {
      DevSerial.print("[WIFI] Profil #");
      DevSerial.print(slot + 1);
      DevSerial.println(" absent du scan.");
      continue;
    }

    if (connectWiFiProfile(slot, WIFI_CONNECT_TIMEOUT_MS)) {
      trackedWiFiSlot = (int8_t)slot;
      strncpy(trackedWiFiSsid, profile.ssid, sizeof(trackedWiFiSsid) - 1);
      trackedWiFiSsid[sizeof(trackedWiFiSsid) - 1] = '\0';
      strncpy(trackedWiFiPassword, profile.password, sizeof(trackedWiFiPassword) - 1);
      trackedWiFiPassword[sizeof(trackedWiFiPassword) - 1] = '\0';
      WiFi.scanDelete();
      setActiveNetwork(ActiveNetwork::WIFI, (int8_t)slot);
      DevSerial.println("[NET] Wi-Fi actif. Internet sera valide par le prochain envoi Trackserver.");
      return true;
    }
  }

  WiFi.scanDelete();
  DevSerial.println("[WIFI] Aucun profil configure utilisable.");
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

  if (!connectCellularNetwork()) {
    return false;
  }

  setActiveNetwork(ActiveNetwork::CELLULAR);
  DevSerial.println("[NET] 4G active. Internet sera valide par le prochain envoi Trackserver.");
  return true;
}

static constexpr uint8_t TRACKSERVER_FAILURES_BEFORE_SWITCH = 3;
static volatile uint8_t trackserverFailures = 0;
static portMUX_TYPE trackserverResultMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t lastStateAction = 0;
static constexpr uint32_t NETWORK_STATE_RETRY_MS = 5000UL;

void networkReportTrackserverResult(bool success)
{
  uint8_t failures;

  portENTER_CRITICAL(&trackserverResultMux);
  if (success) {
    trackserverFailures = 0;
  } else if (trackserverFailures < 255) {
    ++trackserverFailures;
  }
  failures = trackserverFailures;
  portEXIT_CRITICAL(&trackserverResultMux);

  if (!success) {
    DevSerial.print("[NET] Echec Trackserver #");
    DevSerial.print(failures);
    DevSerial.print("/");
    DevSerial.println(TRACKSERVER_FAILURES_BEFORE_SWITCH);
  }
}

static uint8_t getTrackserverFailures()
{
  uint8_t failures;
  portENTER_CRITICAL(&trackserverResultMux);
  failures = trackserverFailures;
  portEXIT_CRITICAL(&trackserverResultMux);
  return failures;
}

static void resetTrackserverFailures()
{
  portENTER_CRITICAL(&trackserverResultMux);
  trackserverFailures = 0;
  portEXIT_CRITICAL(&trackserverResultMux);
}

static bool activeWiFiProfileStillValid()
{
  if (getActiveNetwork() != ActiveNetwork::WIFI) return false;

  const int8_t slot = getActiveWiFiSlot();
  if (slot < 0) return false;

  WiFiProfile profile;
  if (!getWiFiProfile((uint8_t)slot, profile)) return false;

  if (WiFi.status() != WL_CONNECTED) return false;
  if (WiFi.SSID() != profile.ssid) return false;

  // Le profil actif peut être modifié depuis le Dashboard. Même SSID ne
  // signifie pas nécessairement mêmes identifiants : dans ce cas, la machine
  // d'état doit quitter le Wi-Fi et reprendre sa séquence normalement.
  if (strcmp(profile.ssid, trackedWiFiSsid) != 0 ||
      strcmp(profile.password, trackedWiFiPassword) != 0) {
    return false;
  }

  return true;
}

static void clearWiFiTracking()
{
  trackedWiFiSlot = -1;
  trackedWiFiSsid[0] = '\0';
  trackedWiFiPassword[0] = '\0';
}

static void switchFromWiFiTo4G()
{
  DevSerial.println("[NET] Wi-Fi perdu -> passage a la 4G.");
  WiFi.disconnect(false, false);
  clearWiFiTracking();
  setActiveNetwork(ActiveNetwork::NONE);

  if (activateCellularAndPublish()) {
    resetTrackserverFailures();
    return;
  }

  DevSerial.println("[NET] 4G indisponible -> OFFLINE, prochain cycle : Wi-Fi.");
  setActiveNetwork(ActiveNetwork::NONE);
  resetTrackserverFailures();
}

static uint32_t lastCellularLinkCheck = 0;
static bool lastCellularLinkUsable = true;
static constexpr uint32_t CELLULAR_LINK_CHECK_MS = 5000UL;

// Même en 4G, le Wi-Fi reste prioritaire : on recherche périodiquement
// les 3 profils configurés. Le scan ne change l'état réseau que si un
// profil est réellement retrouvé et connecté.
static uint32_t lastWiFiReturnScan = 0;
static constexpr uint32_t WIFI_RETURN_SCAN_MS = 10000UL;

static bool cellularLinkStillUsable()
{
  const uint32_t now = millis();

  if (!modemCommunicationReady()) return false;

  if (now - lastCellularLinkCheck < CELLULAR_LINK_CHECK_MS) {
    return lastCellularLinkUsable;
  }

  lastCellularLinkCheck = now;

  // Vérification légère du réseau radio. La connectivité Internet est
  // validée par les vrais envois Trackserver.
  const String reg = sendATCommand("AT+CEREG?", 1500);
  const int p = reg.indexOf("+CEREG:");

  if (p < 0) {
    // Réponse ambiguë : ne pas provoquer une bascule inutile.
    lastCellularLinkUsable = true;
    return true;
  }

  const int comma = reg.indexOf(',', p);
  if (comma < 0) {
    lastCellularLinkUsable = true;
    return true;
  }

  const int stat = reg.substring(comma + 1).toInt();
  lastCellularLinkUsable = (stat == 1 || stat == 5);

  if (!lastCellularLinkUsable) {
    DevSerial.print("[4G] Enregistrement réseau perdu (CEREG=");
    DevSerial.print(stat);
    DevSerial.println(").");
  }

  return lastCellularLinkUsable;
}

static void switchFrom4GToWiFi()
{
  DevSerial.println("[NET] 4G perdue -> retour prioritaire au Wi-Fi.");
  setActiveNetwork(ActiveNetwork::NONE);
  resetTrackserverFailures();
}

static void handleWiFiState(uint32_t now)
{
  if (!activeWiFiProfileStillValid()) {
    switchFromWiFiTo4G();
    return;
  }

  if (getTrackserverFailures() >= TRACKSERVER_FAILURES_BEFORE_SWITCH &&
      now - lastStateAction >= NETWORK_STATE_RETRY_MS) {
    lastStateAction = now;
    resetTrackserverFailures();
    switchFromWiFiTo4G();
  }
}

static void handleCellularState(uint32_t now)
{
  // Priorité Wi-Fi : tant que la 4G fonctionne, on ne l'abandonne pas au
  // hasard ; on recherche périodiquement si l'un des 3 Wi-Fi configurés
  // est revenu. Un scan négatif laisse la 4G totalement intacte.
  if (now - lastWiFiReturnScan >= WIFI_RETURN_SCAN_MS) {
    lastWiFiReturnScan = now;

    if (wifiProfileCount() > 0) {
      DevSerial.println("[WIFI-TASK] 4G active -> recherche des 3 profils Wi-Fi...");

      if (connectWiFi()) {
        // Le Wi-Fi est maintenant connecté. L'état actif est volontairement
        // changé ici : le prochain envoi Trackserver validera Internet.
        resetTrackserverFailures();
        DevSerial.println("[NET] Wi-Fi retrouve -> retour au Wi-Fi prioritaire.");
        return;
      }
    }
  }

  if (!cellularLinkStillUsable()) {
    switchFrom4GToWiFi();
    return;
  }

  if (getTrackserverFailures() >= TRACKSERVER_FAILURES_BEFORE_SWITCH &&
      now - lastStateAction >= NETWORK_STATE_RETRY_MS) {
    lastStateAction = now;
    resetTrackserverFailures();
    switchFrom4GToWiFi();
  }
}

static void handleNoNetworkState(uint32_t now)
{
  if (now - lastStateAction < NETWORK_STATE_RETRY_MS) return;
  lastStateAction = now;

  // Priorité stricte : Wi-Fi d'abord.
  if (wifiProfileCount() > 0 && connectWiFi()) {
    resetTrackserverFailures();
    return;
  }

  // Si aucun Wi-Fi utilisable, on passe immédiatement à la 4G.
  // Les deux transports ne sont jamais gérés simultanément par cette machine.
  if (activateCellularAndPublish()) {
    resetTrackserverFailures();
    return;
  }

  setActiveNetwork(ActiveNetwork::NONE);
}

void networkTask(void* parameter)
{
  (void)parameter;

  vTaskDelay(pdMS_TO_TICKS(1500));

  lastStateAction = millis() - NETWORK_STATE_RETRY_MS;
  resetTrackserverFailures();

  DevSerial.println("[NET] Machine réseau 3.0.9 : WIFI -> 4G -> WIFI -> 4G");

  while (true) {
    const uint32_t now = millis();
    const ActiveNetwork state = getActiveNetwork();

    switch (state) {
      case ActiveNetwork::WIFI: {
        handleWiFiState(now);

        break;
      }

      case ActiveNetwork::CELLULAR:
        handleCellularState(now);
        break;

      case ActiveNetwork::NONE:
      default:
        handleNoNetworkState(now);
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(250));
  }
}
