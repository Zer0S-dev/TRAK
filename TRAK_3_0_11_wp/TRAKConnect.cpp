#include "TRAKConnect.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>

#include "Config.h"
#include "DevLog.h"
#include "ModemManager.h"
#include "TrackerRuntime.h"

namespace {

Preferences prefs;

bool initialized = false;
volatile bool connected = false;

uint32_t lastAttemptMs = 0;
uint32_t lastLogMs = 0;

TaskHandle_t taskHandle = nullptr;

static constexpr uint32_t SEND_INTERVAL_MS = 10000;

static constexpr char PREF_NS[]  = "trakconnect";
static constexpr char PREF_URL[] = "url";

/*
 * TRAK Connect WordPress REST API.
 *
 * /trak/ est la page WordPress contenant le shortcode.
 * Les données du TRAK sont reçues par cette API.
 */
static constexpr char DEFAULT_URL[] =
    "https://surlereservoir.fr/wp-json/trak-connect/v1/position";

String loadUrl()
{
  if (!prefs.begin(PREF_NS, false)) {
    return String(DEFAULT_URL);
  }

  String url = prefs.getString(PREF_URL, DEFAULT_URL);

  if (url.length() == 0) {
    url = DEFAULT_URL;
  }

  prefs.end();
  return url;
}

String buildPositionJson(const WebTrackerData& snapshot)
{
  String json;
  json.reserve(128);

  json += "{\"type\":\"position\",\"lat\":";
  json += String(snapshot.latitude, 6);
  json += ",\"lon\":";
  json += String(snapshot.longitude, 6);
  json += "}";

  return json;
}

bool sendPositionWiFi(const String& url,
                      const String& json)
{
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  WiFiClientSecure client;

  // TEST 3.0.11.wp : certificat non vérifié.
  // La vérification CA sera activée pour la version production.
  client.setInsecure();

  HTTPClient http;

  if (!http.begin(client, url)) {
    DevSerial.println(
        "[TRAK-CONNECT][WiFi] Impossible d'ouvrir HTTPS.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", "TRAK-3.0.11.wp");

  const int httpCode = http.POST(json);
  const bool success = (httpCode >= 200 && httpCode < 300);

  if (success) {
    const uint32_t now = millis();

    if (now - lastLogMs >= 5000) {
      lastLogMs = now;
      DevSerial.print(
          "[TRAK-CONNECT][WiFi] Position envoyee HTTP ");
      DevSerial.print(httpCode);
      DevSerial.print(": ");
      DevSerial.println(json);
    }
  } else {
    DevSerial.print(
        "[TRAK-CONNECT][WiFi] Echec HTTP: ");
    DevSerial.println(httpCode);

    if (httpCode > 0) {
      const String response = http.getString();
      if (response.length() > 0) {
        DevSerial.print(
            "[TRAK-CONNECT][WiFi] Reponse serveur: ");
        DevSerial.println(response);
      }
    }
  }

  http.end();
  return success;
}

bool sendPositionCellular(const String& url,
                          const String& json)
{
  uint32_t bytesSent = 0;

  return sendHttpPostCellular(
      url,
      json,
      bytesSent);
}

bool sendPosition(const String& url,
                  const WebTrackerData& snapshot)
{
  const String json = buildPositionJson(snapshot);

  // NetworkManager reste maître de la connectivité.
  // TRAK Connect choisit uniquement le transport correspondant
  // au réseau actuellement disponible.
  if (WiFi.status() == WL_CONNECTED) {
    return sendPositionWiFi(url, json);
  }

  // Pas de Wi-Fi : tentative via le transport HTTP du A7670.
  return sendPositionCellular(url, json);
}

void trakConnectTask(void* parameter)
{
  (void)parameter;

  DevSerial.println(
      "[TRAK-CONNECT] Task demarree.");

  const String url = loadUrl();

  DevSerial.print(
      "[TRAK-CONNECT] API: ");
  DevSerial.println(url);

  for (;;) {
    WebTrackerData snapshot;

    if (getWebTrackerData(snapshot) &&
        snapshot.hasFix) {

      const uint32_t now = millis();

      if (now - lastAttemptMs >= SEND_INTERVAL_MS) {
        // Important : limiter les tentatives même lorsque le réseau
        // est absent, afin de ne pas marteler le modem toutes les 100 ms.
        lastAttemptMs = now;

        if (sendPosition(url, snapshot)) {
          connected = true;
        } else {
          connected = false;
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

} // namespace

void trakConnectBegin()
{
  if (initialized || taskHandle != nullptr) {
    return;
  }

  initialized = true;
  connected = false;

  xTaskCreatePinnedToCore(
      trakConnectTask,
      "TRAKConnect",
      6144,
      nullptr,
      1,
      &taskHandle,
      1);

  if (taskHandle == nullptr) {
    initialized = false;

    DevSerial.println(
        "[TRAK-CONNECT] ERREUR: task impossible.");

    return;
  }

  DevSerial.println(
      "[TRAK-CONNECT] HTTPS REST initialise.");
}

void trakConnectService(const GnssData& gps)
{
  /*
   * Compatibilite avec l'architecture existante.
   *
   * L'envoi est effectué par la tâche dédiée afin
   * de ne pas bloquer le thread GNSS/communication.
   */
  (void)gps;
}

bool trakConnectIsConnected()
{
  return connected;
}
