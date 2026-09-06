#include "TRAKConnect.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>

#include "Config.h"
#include "DevLog.h"
#include "ModemManager.h"
#include "ModemArbiter.h"
#include "TrackerRuntime.h"

namespace {

Preferences prefs;

bool initialized = false;
volatile bool connected = false;

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
static constexpr uint32_t MODEM_ARB_TIMEOUT_MS = 250;
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

bool sendPositionCellular(const String& url,
                          const String& json,
                          uint32_t seq,
                          uint32_t& elapsedMs)
{
  uint32_t bytesSent = 0;
  const uint32_t startMs = millis();

  // Short cooperative wait: TRAK Connect must remain regular without
  // blocking behind a long Trackserver cellular transaction.
  if (!modemArbiterAcquire(
          ModemArbiterClient::TRAK_CONNECT, MODEM_ARB_TIMEOUT_MS)) {
    DevSerial.println(
        "[TRAK-CONNECT][4G] A7670 occupe : envoi reporte.");
    elapsedMs = millis() - startMs;
    return false;
  }

  const bool success = sendHttpPostCellular(
      url,
      json,
      bytesSent);

  modemArbiterRelease(ModemArbiterClient::TRAK_CONNECT);

  elapsedMs = millis() - startMs;

  if (success) {
    DevSerial.print("[TRAK-CONNECT][4G] ACK HTTP seq=");
    DevSerial.print(seq);
    DevSerial.print(" ");
    DevSerial.print(elapsedMs);
    DevSerial.println(" ms");
  }

  return success;
}

bool sendPosition(const String& url,
                  const WebTrackerData& snapshot,
                  uint32_t seq,
                  uint32_t& elapsedMs)
{
  const String json = buildPositionJson(snapshot, seq);

  // NetworkManager reste maître de la connectivité.
  // TRAK Connect choisit uniquement le transport correspondant
  // au réseau actuellement disponible.
  if (WiFi.status() == WL_CONNECTED) {
    return sendPositionWiFi(url, json, seq, elapsedMs);
  }

  // Pas de Wi-Fi : tentative via le transport HTTP du A7670.
  return sendPositionCellular(url, json, seq, elapsedMs);
}

void trakConnectTask(void* parameter)
{
  (void)parameter;

  DevSerial.println(
      "[TRAK-CONNECT] Task demarree.");
  DevSerial.println(
      "[TRAK-CONNECT] Cadence : immobile 10 s | mobile 2 s | transition immediate.");

  const String url = loadUrl();

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
        DevSerial.print(WiFi.status() == WL_CONNECTED ? "WiFi" : "4G");
        if (immediate) DevSerial.print(" | IMMEDIATE");
        DevSerial.println();

        uint32_t elapsedMs = 0;
        const bool success = sendPosition(url, snapshot, seq, elapsedMs);

        if (success) {
          connected = true;
          DevSerial.print("[TRAK-CONNECT] OK #");
          DevSerial.print(txAttemptCount);
          DevSerial.print(" | seq=");
          DevSerial.print(seq);
          DevSerial.print(" | ");
          DevSerial.print(elapsedMs);
          DevSerial.println(" ms");
        } else {
          connected = false;
          DevSerial.print("[TRAK-CONNECT] TX #");
          DevSerial.print(txAttemptCount);
          DevSerial.print(" reporte/echec | seq=");
          DevSerial.println(seq);
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
