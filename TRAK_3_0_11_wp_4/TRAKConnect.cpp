#include "TRAKConnect.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>

#include "Config.h"
#include "DevLog.h"
#include "ModemManager.h"
#include "ModemUplink.h"
#include "TrackerRuntime.h"

namespace {

Preferences prefs;

bool initialized = false;
volatile bool connected = false;
String cachedUrl;

uint32_t lastAttemptMs = 0;
uint32_t lastLogMs = 0;
bool previousMoving = false;
bool motionStateInitialized = false;

uint32_t txSequence = 0;
uint32_t txSequenceBlockEnd = 0;
uint32_t txAttemptCount = 0;

TaskHandle_t taskHandle = nullptr;

static constexpr uint32_t STATIONARY_INTERVAL_MS = 10000;
static constexpr uint32_t MOVING_INTERVAL_MS = 2000;
static constexpr uint32_t SEQUENCE_BLOCK_SIZE = 1024;

static constexpr char PREF_NS[]        = "trakconnect";
static constexpr char PREF_URL[]       = "url";
static constexpr char PREF_SEQ_BLOCK[] = "seqblock";

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

uint32_t nextTxSequence()
{
  if (txSequence == 0 || txSequence >= txSequenceBlockEnd) {
    if (!prefs.begin(PREF_NS, false)) {
      DevSerial.println("[TRAK-CONNECT] ERREUR NVS sequence.");
      return ++txSequence;
    }

    const uint32_t storedBlockEnd = prefs.getULong(PREF_SEQ_BLOCK, 0);
    const uint32_t newBlockEnd = storedBlockEnd + SEQUENCE_BLOCK_SIZE;

    // Réserve un bloc complet en NVS. On évite ainsi une écriture flash à
    // chaque transmission, tout en gardant un seq monotone après reboot.
    if (prefs.putULong(PREF_SEQ_BLOCK, newBlockEnd) != sizeof(uint32_t)) {
      DevSerial.println("[TRAK-CONNECT] ERREUR reservation sequence NVS.");
      prefs.end();
      return ++txSequence;
    }

    prefs.end();

    txSequence = storedBlockEnd + 1;
    txSequenceBlockEnd = newBlockEnd + 1;
  }

  return txSequence++;
}

String buildPositionJson(const WebTrackerData& snapshot, uint32_t seq)
{
  String json;
  json.reserve(160);

  json += "{\"type\":\"position\",\"seq\":";
  json += String(seq);
  json += ",\"lat\":";
  json += String(snapshot.latitude, 6);
  json += ",\"lon\":";
  json += String(snapshot.longitude, 6);
  json += "}";

  return json;
}

bool sendPositionWiFi(const String& url,
                      const String& json,
                      uint32_t seq,
                      uint32_t& elapsedMs)
{
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  const uint32_t startMs = millis();

  WiFiClientSecure client;

  // TEST 3.0.11.wp : certificat non vérifié.
  // La vérification CA sera activée pour la version production.
  client.setInsecure();

  HTTPClient http;

  if (!http.begin(client, url)) {
    DevSerial.println(
        "[TRAK-CONNECT][WiFi] Impossible d'ouvrir HTTPS.");
    elapsedMs = millis() - startMs;
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", "TRAK-3.0.11.wp");

  const int httpCode = http.POST(json);
  const bool success = (httpCode >= 200 && httpCode < 300);
  const uint32_t elapsed = millis() - startMs;
  elapsedMs = elapsed;

  if (success) {
    const String response = http.getString();

    // Le code HTTP 2xx constitue l'ACK de transport. Si le serveur renvoie
    // le seq, on vérifie également qu'il correspond à notre transmission.
    bool ackOk = true;
    const String expectedSeq = String("\"seq\":") + String(seq);
    if (response.length() > 0 && response.indexOf("\"seq\"") >= 0) {
      ackOk = response.indexOf(expectedSeq) >= 0;
    }

    if (elapsed >= 0) {
      DevSerial.print("[TRAK-CONNECT][WiFi] ACK HTTP ");
      DevSerial.print(httpCode);
      DevSerial.print(" seq=");
      DevSerial.print(seq);
      DevSerial.print(" ");
      DevSerial.print(elapsed);
      DevSerial.print(" ms");
      if (!ackOk) DevSerial.print(" | ACK seq incoherent");
      DevSerial.println();
    }

    if (!ackOk) {
      DevSerial.print("[TRAK-CONNECT][WiFi] Reponse serveur: ");
      DevSerial.println(response);
    }

    http.end();
    return ackOk;
  }

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

  http.end();
  return false;
}

// L'envoi cellulaire n'est plus déclenché ici : il est délégué à
// ModemUplink, seul propriétaire du modem 4G (voir modemUplinkEnqueueTrakConnect
// et trakConnectSendOneCellular ci-dessous). Sur ce transport, l'appel est
// donc non-bloquant : l'échantillon est simplement mis en file, et le
// résultat réel arrive de façon asynchrone via `connected`.
void queuePositionCellular(const WebTrackerData& snapshot, uint32_t seq)
{
  TrakConnectSample sample;
  sample.latitude = snapshot.latitude;
  sample.longitude = snapshot.longitude;
  sample.seq = seq;
  modemUplinkEnqueueTrakConnect(sample);
}

void trakConnectTask(void* parameter)
{
  (void)parameter;

  DevSerial.println(
      "[TRAK-CONNECT] Task demarree.");
  DevSerial.println(
      "[TRAK-CONNECT] Cadence : immobile 10 s | mobile 2 s | transition immediate.");

  const String url = cachedUrl;

  DevSerial.print(
      "[TRAK-CONNECT] API: ");
  DevSerial.println(url);

  for (;;) {
    WebTrackerData snapshot;

    if (getWebTrackerData(snapshot) &&
        snapshot.hasFix) {

      const uint32_t nowMs = millis();
      const bool moving = snapshot.moving;
      const uint32_t intervalMs = moving
          ? MOVING_INTERVAL_MS
          : STATIONARY_INTERVAL_MS;

      bool immediate = false;
      if (!motionStateInitialized) {
        motionStateInitialized = true;
        previousMoving = moving;
        lastAttemptMs = nowMs - intervalMs;
      } else if (moving && !previousMoving) {
        // Passage stationnaire -> mobile : transmission immédiate.
        immediate = true;
        lastAttemptMs = nowMs - intervalMs;
      }
      previousMoving = moving;

      if (immediate || nowMs - lastAttemptMs >= intervalMs) {
        lastAttemptMs = nowMs;

        const uint32_t seq = nextTxSequence();
        ++txAttemptCount;

        DevSerial.print("[TRAK-CONNECT] TX #");
        DevSerial.print(txAttemptCount);
        DevSerial.print(" | seq=");
        DevSerial.print(seq);
        DevSerial.print(" | ");
        const bool wifi = WiFi.status() == WL_CONNECTED;
        DevSerial.print(wifi ? "WiFi" : "4G");
        if (immediate) DevSerial.print(" | IMMEDIATE");
        DevSerial.println();

        if (wifi) {
          // Pas de ressource matérielle partagée en WiFi : envoi direct,
          // comme avant.
          uint32_t elapsedMs = 0;
          const String json = buildPositionJson(snapshot, seq);
          const bool success = sendPositionWiFi(url, json, seq, elapsedMs);
          connected = success;

          if (success) {
            DevSerial.print("[TRAK-CONNECT] OK #");
            DevSerial.print(txAttemptCount);
            DevSerial.print(" | seq=");
            DevSerial.print(seq);
            DevSerial.print(" | ");
            DevSerial.print(elapsedMs);
            DevSerial.println(" ms");
          } else {
            DevSerial.print("[TRAK-CONNECT] TX #");
            DevSerial.print(txAttemptCount);
            DevSerial.print(" echec | seq=");
            DevSerial.println(seq);
          }
        } else {
          // 4G : ne pas bloquer cette tâche sur le modem. L'échantillon
          // est déposé dans la file de ModemUplink, qui l'enverra dès
          // que son tour arrive (voir ModemUplink.cpp). `connected`
          // reflète alors le résultat du dernier envoi effectivement
          // réalisé par ModemUplink.
          queuePositionCellular(snapshot, seq);
          DevSerial.print("[TRAK-CONNECT] TX #");
          DevSerial.print(txAttemptCount);
          DevSerial.print(" mis en file (4G) | seq=");
          DevSerial.println(seq);
        }
      }
    }

    if (WiFi.status() != WL_CONNECTED) {
      connected = modemUplinkTrakConnectLastOk();
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

} // namespace

bool trakConnectSendOneCellular(float latitude, float longitude, uint32_t seq)
{
  WebTrackerData snapshot{};
  snapshot.latitude = latitude;
  snapshot.longitude = longitude;

  const String json = buildPositionJson(snapshot, seq);
  uint32_t bytesSent = 0;
  const bool success = sendHttpPostCellular(cachedUrl, json, bytesSent);

  if (success) {
    DevSerial.print("[TRAK-CONNECT][4G] ACK HTTP seq=");
    DevSerial.println(seq);
  }

  return success;
}

void trakConnectBegin()
{
  if (initialized || taskHandle != nullptr) {
    return;
  }

  initialized = true;
  connected = false;

  // Chargé ici (pas seulement dans trakConnectTask) pour que cachedUrl
  // soit toujours prêt avant que ModemUplink ne puisse déclencher un
  // envoi cellulaire.
  cachedUrl = loadUrl();

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
