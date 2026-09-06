#include "SDMutex.h"

#include "DevLog.h"
#include "WebInterface.h"
#include "Config.h"
#include "RuntimeConfig.h"
#include "SentinelManager.h"

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <Preferences.h>

namespace {

WebServer server(80);
WebDataProvider dataProvider = nullptr;
bool started = false;

String sessionToken;

Preferences authPreferences;

const char* AP_SSID = "TRAK_DIRECT";
const char* AP_PASSWORD = "trak@dmin";

constexpr const char* AUTH_NAMESPACE = "trak_auth";
constexpr const char* AUTH_TOKEN_KEY = "session_token";


// ============================================================
// JSON HELPERS
// ============================================================

String jsonEscape(const char* value)
{
  String out;

  if (value == nullptr)
    return out;

  for (const char* p = value; *p; ++p) {

    switch (*p) {

      case '"':
        out += "\\\"";
        break;

      case '\\':
        out += "\\\\";
        break;

      case '\n':
        out += "\\n";
        break;

      case '\r':
        out += "\\r";
        break;

      default:
        out += *p;
        break;
    }
  }

  return out;
}


String jsonStringArg(
  const String& body,
  const char* key
)
{
  String needle =
    String("\"") + key + "\"";

  int pos =
    body.indexOf(needle);

  if (pos < 0)
    return String();

  pos =
    body.indexOf(
      ':',
      pos + needle.length()
    );

  if (pos < 0)
    return String();

  while (
    pos + 1 < (int)body.length() &&
    isspace(
      (unsigned char)body[pos + 1]
    )
  ) {
    ++pos;
  }

  if (
    pos + 1 >= (int)body.length() ||
    body[pos + 1] != '"'
  ) {
    return String();
  }

  pos += 2;

  String value;
  bool escaped = false;

  while (
    pos < (int)body.length()
  ) {

    char c =
      body[pos++];

    if (escaped) {

      if (c == 'n')
        value += '\n';

      else if (c == 'r')
        value += '\r';

      else
        value += c;

      escaped = false;

    } else if (c == '\\') {

      escaped = true;

    } else if (c == '"') {

      break;

    } else {

      value += c;
    }
  }

  return value;
}


int jsonIntArg(
  const String& body,
  const char* key,
  int fallback = -1
)
{
  String needle =
    String("\"") + key + "\"";

  int pos =
    body.indexOf(needle);

  if (pos < 0)
    return fallback;

  pos =
    body.indexOf(
      ':',
      pos + needle.length()
    );

  if (pos < 0)
    return fallback;

  ++pos;

  while (
    pos < (int)body.length() &&
    isspace(
      (unsigned char)body[pos]
    )
  ) {
    ++pos;
  }

  return body.substring(pos).toInt();
}


float jsonFloatArg(
  const String& body,
  const char* key,
  float fallback = -1.0f
)
{
  String needle =
    String("\"") + key + "\"";

  int pos =
    body.indexOf(needle);

  if (pos < 0)
    return fallback;

  pos =
    body.indexOf(
      ':',
      pos + needle.length()
    );

  if (pos < 0)
    return fallback;

  ++pos;

  while (
    pos < (int)body.length() &&
    isspace(
      (unsigned char)body[pos]
    )
  ) {
    ++pos;
  }

  return body.substring(pos).toFloat();
}


// ============================================================
// AUTHENTIFICATION
// ============================================================

static String makeSessionToken()
{
  char token[33];

  uint32_t a = esp_random();
  uint32_t b = esp_random();
  uint32_t c = esp_random();
  uint32_t d = esp_random();

  snprintf(
    token,
    sizeof(token),
    "%08lX%08lX%08lX%08lX",
    (unsigned long)a,
    (unsigned long)b,
    (unsigned long)c,
    (unsigned long)d
  );

  return String(token);
}


static void loadSessionToken()
{
  if (
    !authPreferences.begin(
      AUTH_NAMESPACE,
      false
    )
  ) {

    DevSerial.println(
      "[AUTH] Impossible d'ouvrir NVS authentification"
    );

    sessionToken = String();

    return;
  }

  sessionToken =
    authPreferences.getString(
      AUTH_TOKEN_KEY,
      ""
    );

  if (
    sessionToken.length() > 0
  ) {

    DevSerial.println(
      "[AUTH] Session persistante chargée"
    );

  } else {

    DevSerial.println(
      "[AUTH] Aucune session persistante"
    );
  }
}


static void saveSessionToken()
{
  if (
    !authPreferences.putString(
      AUTH_TOKEN_KEY,
      sessionToken
    )
  ) {

    DevSerial.println(
      "[AUTH] Erreur sauvegarde session"
    );

  } else {

    DevSerial.println(
      "[AUTH] Session persistante sauvegardée"
    );
  }
}


static void clearSessionToken()
{
  sessionToken = String();

  authPreferences.remove(
    AUTH_TOKEN_KEY
  );

  DevSerial.println(
    "[AUTH] Session supprimée"
  );
}


static bool hasValidSession()
{
  if (
    sessionToken.length() == 0 ||
    !server.hasHeader("Cookie")
  ) {
    return false;
  }

  const String cookie =
    server.header("Cookie");

  const String needle =
    String("TRAK_SESSION=") +
    sessionToken;

  return cookie.indexOf(
    needle
  ) >= 0;
}


static bool authenticated(
  bool apiRequest = true
)
{
  if (hasValidSession())
    return true;

  if (apiRequest) {

    server.send(
      401,
      "application/json",
      "{\"error\":\"authentication required\"}"
    );

  } else {

    server.sendHeader(
      "Location",
      "/login.html",
      true
    );

    server.send(
      302,
      "text/plain; charset=utf-8",
      "Connexion requise"
    );
  }

  return false;
}


// ============================================================
// LOGIN
// ============================================================

static void handleLogin()
{
  const String body =
    server.arg("plain");

  const String username =
    jsonStringArg(
      body,
      "username"
    );

  const String password =
    jsonStringArg(
      body,
      "password"
    );

  if (
    username != WEB_USER ||
    password != WEB_PASSWORD
  ) {

    server.send(
      401,
      "application/json",
      "{\"error\":\"invalid credentials\"}"
    );

    return;
  }


  if (
    sessionToken.length() == 0
  ) {

    sessionToken =
      makeSessionToken();

    saveSessionToken();

    DevSerial.println(
      "[AUTH] Nouvelle session créée"
    );

  } else {

    DevSerial.println(
      "[AUTH] Session existante réutilisée"
    );
  }


  String cookie =
    String("TRAK_SESSION=") +
    sessionToken +
    "; Path=/" +
    "; Max-Age=31536000" +
    "; HttpOnly" +
    "; SameSite=Strict";


  server.sendHeader(
    "Set-Cookie",
    cookie,
    true
  );


  String json;

  json.reserve(140);

  json += "{\"ok\":true,\"state\":\"";
  json += sentinelStateName();
  json += "\",\"confirmationRemainingMs\":";
  json += sentinelConfirmationRemainingMs();
  json += "}";


  server.send(
    200,
    "application/json",
    json
  );
}


// ============================================================
// SESSION
// ============================================================

static void handleSession()
{
  if (hasValidSession()) {

    server.send(
      200,
      "application/json",
      "{\"authenticated\":true}"
    );

    return;
  }

  server.send(
    401,
    "application/json",
    "{\"authenticated\":false}"
  );
}


// ============================================================
// LOGOUT
// ============================================================

static void handleLogout()
{
  clearSessionToken();


  server.sendHeader(
    "Set-Cookie",
    "TRAK_SESSION=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict",
    true
  );


  server.sendHeader(
    "Location",
    "/login.html",
    true
  );


  server.send(
    302,
    "text/plain; charset=utf-8",
    "Déconnexion"
  );
}


// ============================================================
// MOTION
// ============================================================

static const char* motionModeText(
  const WebTrackerData& d
)
{
  if (d.moving)
    return "MOBILE";

  if (d.stationaryConfirmed)
    return "STATIONNAIRE";

  return "IMMOBILE_CONFIRMATION";
}


// ============================================================
// SD WEB FILES
// ============================================================

static constexpr const char* WEB_ROOT =
  "/web";


static bool streamWebFile(
  const char* fileName,
  const char* contentType
)
{
  String path =
    String(WEB_ROOT) +
    "/" +
    fileName;

  if (!sdMutexLock(pdMS_TO_TICKS(5000)))
    return false;

  if (!SD.exists(path.c_str())) {
    sdMutexUnlock();
    return false;
  }

  File file = SD.open(path.c_str(), FILE_READ);
  if (!file) {
    sdMutexUnlock();
    return false;
  }

  server.streamFile(file, contentType);
  file.close();
  sdMutexUnlock();
  return true;
}
static bool streamMapFile(
  const char* fileName,
  const char* contentType
)
{
  String path =
    "/" +
    String(fileName);

  if (!sdMutexLock(pdMS_TO_TICKS(5000)))
    return false;

  if (!SD.exists(path.c_str())) {
    sdMutexUnlock();
    return false;
  }

  File file =
    SD.open(
      path.c_str(),
      FILE_READ
    );

  if (!file) {
    sdMutexUnlock();
    return false;
  }

  server.streamFile(
    file,
    contentType
  );

  file.close();
  sdMutexUnlock();

  return true;
}

static void handleWebStatic(
  const char* fileName,
  const char* contentType
)
{
  if (!authenticated())
    return;


  if (
    !streamWebFile(
      fileName,
      contentType
    )
  ) {

    server.send(
      404,
      "text/plain; charset=utf-8",
      "Fichier Web absent sur la SD"
    );
  }
}


// ============================================================
// LOGIN PAGE
// ============================================================

static void handleLoginPage()
{
  if (hasValidSession()) {

    server.sendHeader(
      "Location",
      "/",
      true
    );

    server.send(
      302,
      "text/plain; charset=utf-8",
      "Déjà connecté"
    );

    return;
  }


  if (
    !streamWebFile(
      "login.html",
      "text/html; charset=utf-8"
    )
  ) {

    server.send(
      500,
      "text/plain; charset=utf-8",
      "/web/login.html absent sur la SD"
    );

    return;
  }
}


// ============================================================
// ROOT
// ============================================================

static void handleRoot()
{
  if (!authenticated(false))
    return;


  if (
    !streamWebFile(
      "index.html",
      "text/html; charset=utf-8"
    )
  ) {

    server.send(
      500,
      "text/plain; charset=utf-8",
      "/web/index.html absent sur la SD"
    );

    return;
  }
}


// ============================================================
// DATA
// ============================================================

static void handleData()
{
  if (!authenticated())
    return;


  WebTrackerData d;


  if (
    dataProvider == nullptr ||
    !dataProvider(d)
  ) {

    server.send(
      503,
      "application/json",
      "{\"error\":\"snapshot unavailable\"}"
    );

    return;
  }


  String json;

  json.reserve(1800);


  json += "{\"mode\":\"";
  json += d.activeNetworkName;

  json += "\",\"network\":\"";
  json += d.activeNetworkName;

  json += "\",\"firmwareVersion\":\"";
  json += FIRMWARE_VERSION;

  json += "\",\"serialNumber\":\"";
  json += trackerSerialNumber();

  json += "\",\"hasFix\":";
  json += d.hasFix ? "true" : "false";

  json += ",\"satellites\":";
  json += d.satellites;

  json += ",\"gpsSatellites\":";
  json += d.gpsSatellites;

  json += ",\"glonassSatellites\":";
  json += d.glonassSatellites;

  json += ",\"beidouSatellites\":";
  json += d.beidouSatellites;

  json += ",\"galileoSatellites\":";
  json += d.galileoSatellites;

  json += ",\"fixMode\":";
  json += d.fixMode;

  json += ",\"latitude\":";
  json += String(d.latitude, 6);

  json += ",\"longitude\":";
  json += String(d.longitude, 6);

  json += ",\"altitude\":";
  json += String(d.altitude, 1);

  json += ",\"speedKmh\":";
  json += String(d.speedKmh, 1);

  json += ",\"rawSpeedKmh\":";
  json += String(d.rawSpeedKmh, 1);

  json += ",\"filteredSpeedKmh\":";
  json += String(d.filteredSpeedKmh, 1);

  json += ",\"rawAltitude\":";
  json += String(d.rawAltitude, 1);

  json += ",\"filteredAltitude\":";
  json += String(d.filteredAltitude, 1);

  json += ",\"motionXDps\":";
  json += String(d.motionXDps, 3);

  json += ",\"motionYDps\":";
  json += String(d.motionYDps, 3);

  json += ",\"motionZDps\":";
  json += String(d.motionZDps, 3);

  json += ",\"motionDps\":";
  json += String(d.motionDps, 3);

  json += ",\"moving\":";
  json += d.moving ? "true" : "false";

  json += ",\"stationaryConfirmed\":";
  json += d.stationaryConfirmed ? "true" : "false";

  json += ",\"motionStationaryConfirmationRemainingMs\":";
  json += d.motionStationaryConfirmationRemainingMs;

  json += ",\"motionCalibrated\":";
  json += d.motionCalibrated ? "true" : "false";

  json += ",\"motionSensitivityLevel\":";
  json += d.motionSensitivityLevel;

  json += ",\"motionSensitivityThresholdDps\":";
  json += String(
    d.motionSensitivityThresholdDps,
    1
  );

  json += ",\"sendIntervalLevel\":";
  json += d.sendIntervalLevel;

  json += ",\"sendIntervalMovingSec\":";
  json += d.sendIntervalMovingSec;

  json += ",\"motionMode\":\"";
  json += motionModeText(d);

  json += "\",\"signalPercent\":";
  json += d.signalPercent;

  json += ",\"httpSuccess\":";
  json += d.httpSuccess ? "true" : "false";

  json += ",\"wifiConnected\":";
  json += d.wifiConnected ? "true" : "false";

  json += ",\"communicationReady\":";
  json += d.communicationReady ? "true" : "false";

  json += ",\"activeWiFi\":";
  json += d.activeWiFi ? "true" : "false";

  json += ",\"activeWiFiSlot\":";
  json += d.activeWiFiSlot;

  json += ",\"internetAvailable\":";
  json += d.internetAvailable ? "true" : "false";

  json += ",\"txCount\":";
  json += d.txCount;

  json += ",\"lastTxAt\":";
  json += d.lastTxAt;

  json += ",\"bufferCount\":";
  json += d.bufferCount;

  json += ",\"bufferCapacity\":";
  json += d.bufferCapacity;

  json += ",\"sentinelPhoneConfigured\":";
  json += d.sentinelPhoneConfigured
    ? "true"
    : "false";

  json += ",\"sentinelState\":\"";
  json += d.sentinelState;
  json += "\"";

  json += ",\"sentinelConfirmationRemainingMs\":";
  json += d.sentinelConfirmationRemainingMs;


  DataUsage usage;

  getDataUsage(
    usage
  );


  const uint64_t estimated4G =
    (uint64_t)(
      usage.cellularBytes *
      usage.operatorCoefficient
    );


  json += ",\"dataYear\":";
  json += usage.year;

  json += ",\"dataMonth\":";
  json += usage.month;

  json += ",\"wifiBytes\":";
  json += String(
    (unsigned long long)
    usage.wifiBytes
  );

  json += ",\"cellularBytes\":";
  json += String(
    (unsigned long long)
    usage.cellularBytes
  );

  json += ",\"totalBytes\":";
  json += String(
    (unsigned long long)(
      usage.wifiBytes +
      usage.cellularBytes
    )
  );

  json += ",\"estimated4GBytes\":";
  json += String(
    (unsigned long long)
    estimated4G
  );

  json += ",\"dataPlanMb\":";
  json += usage.planMb;

  json += ",\"dataCoefficient\":";
  json += String(
    usage.operatorCoefficient,
    2
  );

  json += "}";


  server.send(
    200,
    "application/json",
    json
  );
}


// ============================================================
// SENTINEL
// ============================================================

static void handleSentinelGet()
{
  if (!authenticated())
    return;


  char phone[
    SENTINEL_PHONE_MAX_LEN + 1
  ] = {};

  char trakPhone[
    SENTINEL_PHONE_MAX_LEN + 1
  ] = {};


  sentinelGetUserPhone(
    phone,
    sizeof(phone)
  );

  sentinelGetTrakPhone(
    trakPhone,
    sizeof(trakPhone)
  );


  String json;

  json.reserve(320);

  json += "{\"phoneConfigured\":";
  json += sentinelHasUserPhone()
    ? "true"
    : "false";

  json += ",\"phone\":\"";
  json += jsonEscape(phone);

  json += "\",\"trakPhoneConfigured\":";
  json += sentinelHasTrakPhone()
    ? "true"
    : "false";

  json += ",\"trakPhone\":\"";
  json += jsonEscape(trakPhone);

  json += "\",\"state\":\"";
  json += sentinelStateName();

  json += "\",\"confirmationRemainingMs\":";
  json += sentinelConfirmationRemainingMs();

  json += "}";


  server.send(
    200,
    "application/json",
    json
  );
}


static void handleSentinelPhoneSave()
{
  if (!authenticated())
    return;


  const String body =
    server.arg("plain");


  const String phone =
    jsonStringArg(
      body,
      "phone"
    );


  if (
    !sentinelSetUserPhone(
      phone.c_str()
    )
  ) {

    server.send(
      400,
      "text/plain; charset=utf-8",
      "Numero invalide ou SMS de confirmation impossible"
    );

    return;
  }


  server.send(
    200,
    "application/json",
    "{\"ok\":true,\"message\":\"Numero enregistre et SMS de confirmation envoye\"}"
  );
}


static void handleSentinelTrakPhoneSave()
{
  if (!authenticated())
    return;

  const String body =
    server.arg("plain");

  const String phone =
    jsonStringArg(
      body,
      "phone"
    );

  if (!sentinelSetTrakPhone(phone.c_str())) {
    server.send(
      400,
      "text/plain; charset=utf-8",
      "Numero TRAK invalide"
    );
    return;
  }

  server.send(
    200,
    "application/json",
    "{\"ok\":true,\"message\":\"Numero TRAK enregistre\"}"
  );
}


static void handleSentinelTrakPhoneDelete()
{
  if (!authenticated())
    return;

  if (!sentinelClearTrakPhone()) {
    server.send(
      409,
      "text/plain; charset=utf-8",
      "Suppression refusee. Sentinel doit etre OFF."
    );
    return;
  }

  server.send(
    200,
    "application/json",
    "{\"ok\":true,\"message\":\"Numero TRAK supprime\"}"
  );
}


static void handleSentinelPhoneDelete()
{
  if (!authenticated())
    return;


  if (
    !sentinelClearUserPhone()
  ) {

    server.send(
      409,
      "text/plain; charset=utf-8",
      "Suppression refusee. Sentinel doit etre OFF."
    );

    return;
  }


  server.send(
    200,
    "application/json",
    "{\"ok\":true,\"message\":\"Numero utilisateur supprime\"}"
  );
}


static void handleSentinelAction()
{
  if (!authenticated())
    return;


  if (!sentinelAdvance())
  {
    server.send(
      409,
      "text/plain; charset=utf-8",
      "Action Sentinel refusee. Verifiez le numero, le delai de confirmation, le LSM6DS3 et le SMS."
    );

    return;
  }


  String json;

  json.reserve(140);

  json += "{\"ok\":true,\"state\":\"";
  json += sentinelStateName();

  json += "\",\"confirmationRemainingMs\":";
  json += sentinelConfirmationRemainingMs();

  json += "}";


  server.send(
    200,
    "application/json",
    json
  );
}


// ============================================================
// TRACKSERVER
// ============================================================

static void handleTrackserverGet()
{
  if (!authenticated())
    return;


  TrackserverConfig ts;


  if (!getTrackserverConfig(ts))
  {
    server.send(
      500,
      "application/json",
      "{\"error\":\"trackserver indisponible\"}"
    );

    return;
  }


  String json;

  json.reserve(220);

  json += "{\"url\":\"";
  json += jsonEscape(ts.url);

  json += "\",\"host\":\"";
  json += jsonEscape(ts.host);

  json += "\"}";


  server.send(
    200,
    "application/json",
    json
  );
}


static void handleTrackserverSave()
{
  if (!authenticated())
    return;


  const String body =
    server.arg("plain");


  const String url =
    jsonStringArg(
      body,
      "url"
    );


  if (
    url.length() == 0 ||
    url.length() >
      TRACKSERVER_URL_MAX_LEN
  ) {

    server.send(
      400,
      "text/plain; charset=utf-8",
      "URL Trackserver invalide ou trop longue"
    );

    return;
  }


  if (
    !saveTrackserverUrl(
      url.c_str()
    )
  ) {

    server.send(
      400,
      "text/plain; charset=utf-8",
      "URL invalide : utilisez http://hote/chemin, sans port explicite"
    );

    return;
  }


  server.send(
    200,
    "text/plain; charset=utf-8",
    "URL Trackserver enregistrée"
  );
}


// ============================================================
// DATA USAGE
// ============================================================

static void handleDataUsageSave()
{
  if (!authenticated())
    return;


  const String body =
    server.arg("plain");


  const int planMb =
    jsonIntArg(
      body,
      "planMb",
      -1
    );


  const float coefficient =
    jsonFloatArg(
      body,
      "coefficient",
      -1.0f
    );


  if (
    planMb <= 0 ||
    coefficient < 1.0f ||
    coefficient > 3.0f
  ) {

    server.send(
      400,
      "text/plain; charset=utf-8",
      "Forfait ou coefficient invalide"
    );

    return;
  }


  if (
    !saveDataUsageSettings(
      (uint32_t)planMb,
      coefficient
    )
  ) {

    server.send(
      400,
      "text/plain; charset=utf-8",
      "Impossible d'enregistrer la configuration data"
    );

    return;
  }


  server.send(
    200,
    "text/plain; charset=utf-8",
    "Configuration data enregistrée"
  );
}


// ============================================================
// MOTION SENSITIVITY
// ============================================================

static void handleMotionSensitivity()
{
  if (!authenticated())
    return;


  const String body =
    server.arg("plain");


  const int level =
    jsonIntArg(
      body,
      "level",
      -1
    );


  if (
    level < 1 ||
    level > 5
  ) {

    server.send(
      400,
      "text/plain; charset=utf-8",
      "Niveau de sensibilite invalide"
    );

    return;
  }


  if (
    !setMotionSensitivityLevel(
      static_cast<uint8_t>(level)
    )
  ) {

    server.send(
      500,
      "text/plain; charset=utf-8",
      "Impossible d'enregistrer la sensibilite"
    );

    return;
  }


  String json =
    "{\"level\":" +
    String(
      motionSensitivityLevel()
    ) +
    ",\"thresholdDps\":" +
    String(
      motionSensitivityThresholdDps(),
      1
    ) +
    "}";


  server.send(
    200,
    "application/json; charset=utf-8",
    json
  );
}


// ============================================================
// SEND INTERVAL
// ============================================================

static void handleSendInterval()
{
  if (!authenticated())
    return;


  const String body =
    server.arg("plain");


  const int level =
    jsonIntArg(
      body,
      "level",
      -1
    );


  if (
    level < 1 ||
    level > 5
  ) {

    server.send(
      400,
      "text/plain; charset=utf-8",
      "Intervalle invalide"
    );

    return;
  }


  if (
    !setSendIntervalLevel(
      static_cast<uint8_t>(level)
    )
  ) {

    server.send(
      500,
      "text/plain; charset=utf-8",
      "Impossible d'enregistrer l'intervalle"
    );

    return;
  }


  String json =
    "{\"level\":" +
    String(
      sendIntervalLevel()
    ) +
    ",\"seconds\":" +
    String(
      sendIntervalMovingSec()
    ) +
    "}";


  server.send(
    200,
    "application/json; charset=utf-8",
    json
  );
}


// ============================================================
// WIFI PROFILES
// ============================================================

static void handleWiFiProfiles()
{
  if (!authenticated())
    return;


  String json = "[";


  for (
    uint8_t i = 0;
    i < MAX_WIFI_PROFILES;
    ++i
  ) {

    if (i > 0)
      json += ',';


    WiFiProfile p;


    json += "{\"slot\":";
    json += i;


    if (
      getWiFiProfile(
        i,
        p
      )
    ) {

      json += ",\"configured\":true,\"ssid\":\"";
      json += jsonEscape(p.ssid);

      json += "\",\"hasPassword\":";
      json += (
        strlen(p.password) > 0
        ? "true"
        : "false"
      );

    } else {

      json +=
        ",\"configured\":false,\"ssid\":\"\",\"hasPassword\":false";
    }


    json += "}";
  }


  json += "]";


  server.send(
    200,
    "application/json",
    json
  );
}


static void handleWiFiSave()
{
  if (!authenticated())
    return;


  String body =
    server.arg("plain");


  const int slot =
    jsonIntArg(
      body,
      "slot"
    );


  const String ssid =
    jsonStringArg(
      body,
      "ssid"
    );


  const String password =
    jsonStringArg(
      body,
      "password"
    );


  if (
    slot < 0 ||
    slot >= MAX_WIFI_PROFILES ||
    ssid.length() == 0 ||
    ssid.length() > WIFI_SSID_MAX_LEN ||
    password.length() > WIFI_PASSWORD_MAX_LEN
  ) {

    server.send(
      400,
      "text/plain; charset=utf-8",
      "SSID, mot de passe ou emplacement invalide"
    );

    return;
  }


  if (
    !saveWiFiProfile(
      (uint8_t)slot,
      ssid.c_str(),
      password.c_str()
    )
  ) {

    server.send(
      500,
      "text/plain; charset=utf-8",
      "Impossible d'enregistrer le profil Wi-Fi"
    );

    return;
  }


  server.send(
    200,
    "text/plain; charset=utf-8",
    "Profil Wi-Fi enregistré - scan automatique en cours"
  );
}


static void handleWiFiDelete()
{
  if (!authenticated())
    return;


  const int slot =
    server.arg(
      "slot"
    ).toInt();


  if (
    slot < 0 ||
    slot >= MAX_WIFI_PROFILES
  ) {

    server.send(
      400,
      "text/plain; charset=utf-8",
      "Emplacement invalide"
    );

    return;
  }


  removeWiFiProfile(
    (uint8_t)slot
  );


  server.send(
    200,
    "text/plain; charset=utf-8",
    "Profil Wi-Fi supprimé"
  );
}


// ============================================================
// NOT FOUND
// ============================================================
//
// Toutes les ressources dynamiques non déclarées explicitement
// passent ici.
//
// /images/foo.png
//      ↓
// /web/images/foo.png
//
// /maps/12/2056/1395.png
//      ↓
// /web/maps/12/2056/1395.png
//
// ============================================================

static void handleNotFound()
{
  const String uri =
    server.uri();


  // ----------------------------------------------------------
  // IMAGES DASHBOARD
  // ----------------------------------------------------------

  if (
    uri.startsWith(
      "/images/"
    )
  ) {

    if (!authenticated())
      return;


    const String relativePath =
      uri.substring(1);


    if (
      !streamWebFile(
        relativePath.c_str(),
        "image/png"
      )
    ) {

      server.send(
        404,
        "text/plain; charset=utf-8",
        "Image absente sur la SD"
      );
    }

    return;
  }


  // ----------------------------------------------------------
  // OFFLINE MAP TILES
  // ----------------------------------------------------------

  if (
    uri.startsWith(
      "/maps/"
    )
  ) {

    if (!authenticated())
      return;


    const String relativePath =
      uri.substring(1);


    if (
      !streamMapFile(
        relativePath.c_str(),
        "image/png"
      )
    ) {

      server.send(
        404,
        "text/plain; charset=utf-8",
        "Tuile absente sur la SD"
      );
    }

    return;
  }


  // ----------------------------------------------------------
  // AUTRE RESSOURCE
  // ----------------------------------------------------------

  server.send(
    404,
    "text/plain; charset=utf-8",
    "Ressource introuvable"
  );
}

} // namespace


// ============================================================
// WEB BEGIN
// ============================================================

void webBegin(
  WebDataProvider provider
)
{
  dataProvider =
    provider;


  /*
   * La carte SD est initialisée par positionBufferBegin()
   * avant webBegin().
   *
   * Le Dashboard est stocké dans /web
   * à la racine de la SD.
   */
  if (!sdMutexLock(pdMS_TO_TICKS(5000))) {
    DevSerial.println("[WEB] Impossible de verrouiller la SD.");
    return;
  }

  const bool cardOk = SD.cardSize() != 0;
  const bool indexOk = cardOk && SD.exists("/web/index.html");
  sdMutexUnlock();

  if (!cardOk) {
    DevSerial.println(
      "[WEB] SD absente/inaccessible. Interface non disponible."
    );
    return;
  }

  if (!indexOk) {
    DevSerial.println(
      "[WEB] /web/index.html absent sur la SD. Interface non disponible."
    );
    return;
  }


  // ----------------------------------------------------------
  // AUTHENTIFICATION
  // ----------------------------------------------------------

  loadSessionToken();


  WiFi.mode(
    WIFI_AP_STA
  );


  WiFi.softAP(
    AP_SSID,
    AP_PASSWORD
  );


  if (
    MDNS.begin("trak")
  ) {

    MDNS.addService(
      "http",
      "tcp",
      80
    );
  }


  const char* headerKeys[] = {
    "Cookie"
  };


  server.collectHeaders(
    headerKeys,
    1
  );


  // ----------------------------------------------------------
  // LOGIN
  // ----------------------------------------------------------

  server.on(
    "/login.html",
    HTTP_GET,
    handleLoginPage
  );


  server.on(
    "/api/login",
    HTTP_POST,
    handleLogin
  );


  server.on(
    "/api/session",
    HTTP_GET,
    handleSession
  );


  server.on(
    "/api/logout",
    HTTP_POST,
    handleLogout
  );


  // ----------------------------------------------------------
  // DASHBOARD
  // ----------------------------------------------------------

  server.on(
    "/",
    HTTP_GET,
    handleRoot
  );


  server.on(
    "/index.html",
    HTTP_GET,
    handleRoot
  );


  server.on(
    "/api/data",
    HTTP_GET,
    handleData
  );


  server.on(
    "/api/motion-sensitivity",
    HTTP_POST,
    handleMotionSensitivity
  );


  server.on(
    "/api/send-interval",
    HTTP_POST,
    handleSendInterval
  );


  // ----------------------------------------------------------
  // CONFIGURATION
  // ----------------------------------------------------------

  server.on(
    "/api/data-usage",
    HTTP_POST,
    handleDataUsageSave
  );


  server.on(
    "/api/sentinel",
    HTTP_GET,
    handleSentinelGet
  );


  server.on(
    "/api/sentinel/phone",
    HTTP_POST,
    handleSentinelPhoneSave
  );


  server.on(
    "/api/sentinel/phone/delete",
    HTTP_POST,
    handleSentinelPhoneDelete
  );


  server.on(
    "/api/sentinel/trak-phone",
    HTTP_POST,
    handleSentinelTrakPhoneSave
  );


  server.on(
    "/api/sentinel/trak-phone/delete",
    HTTP_POST,
    handleSentinelTrakPhoneDelete
  );


  server.on(
    "/api/sentinel/action",
    HTTP_POST,
    handleSentinelAction
  );


  server.on(
    "/api/trackserver",
    HTTP_GET,
    handleTrackserverGet
  );


  server.on(
    "/api/trackserver",
    HTTP_POST,
    handleTrackserverSave
  );


  server.on(
    "/api/wifi",
    HTTP_GET,
    handleWiFiProfiles
  );


  server.on(
    "/api/wifi",
    HTTP_POST,
    handleWiFiSave
  );


  server.on(
    "/api/wifi",
    HTTP_DELETE,
    handleWiFiDelete
  );


  // ----------------------------------------------------------
  // WEB STATIC FILES
  // ----------------------------------------------------------

  /*
   * Les fichiers Web sont lus depuis /web sur la SD.
   *
   * Chaque fichier passe par le contrôle d'accès.
   */

  server.on(
    "/style.css",
    HTTP_GET,
    []() {

      handleWebStatic(
        "style.css",
        "text/css; charset=utf-8"
      );
    }
  );


  server.on(
    "/app.js",
    HTTP_GET,
    []() {

      handleWebStatic(
        "app.js",
        "application/javascript; charset=utf-8"
      );
    }
  );


  server.on(
    "/logo_dark_web.png",
    HTTP_GET,
    []() {

      handleWebStatic(
        "logo_dark_web.png",
        "image/png"
      );
    }
  );

    server.on(
    "/logo_light_web.png",
    HTTP_GET,
    []() {

      handleWebStatic(
        "logo_light_web.png",
        "image/png"
      );
    }
  );


  // ----------------------------------------------------------
  // LEAFLET
  // ----------------------------------------------------------

  server.on(
    "/leaflet/leaflet.css",
    HTTP_GET,
    []() {

      handleWebStatic(
        "leaflet/leaflet.css",
        "text/css; charset=utf-8"
      );
    }
  );


  server.on(
    "/leaflet/leaflet.js",
    HTTP_GET,
    []() {

      handleWebStatic(
        "leaflet/leaflet.js",
        "application/javascript; charset=utf-8"
      );
    }
  );


  // ----------------------------------------------------------
  // UNKNOWN ROUTES
  // ----------------------------------------------------------

  /*
   * IMPORTANT :
   *
   * Il ne doit y avoir QU'UN SEUL server.onNotFound().
   *
   * Les routes /images/ et /maps/ sont traitées ici.
   */

  server.onNotFound(
    handleNotFound
  );


  // ----------------------------------------------------------
  // START SERVER
  // ----------------------------------------------------------

  server.begin();

  started = true;


  DevSerial.print(
    "[WEB] AP local : http://"
  );

  DevSerial.println(
    WiFi.softAPIP()
  );


  DevSerial.println(
    "[WEB] Profils Wi-Fi : 3 maximum, gestion via Web/NVS"
  );


  DevSerial.println(
    "[WEB] Authentification : /login.html + session cookie persistante"
  );


  DevSerial.println(
    "[WEB] API session : /api/session"
  );


  DevSerial.println(
    "[WEB] Dashboard : SD:/web/index.html (session requise)"
  );


  DevSerial.println(
    "[WEB] Images : SD:/web/images/"
  );


  DevSerial.println(
    "[WEB] Cartes offline : SD:/web/maps/{z}/{x}/{y}.png"
  );
}


// ============================================================
// WEB TASK
// ============================================================

void webTask(
  void* parameter
)
{
  (void)parameter;


  while (true) {

    if (started)
      server.handleClient();


    vTaskDelay(
      pdMS_TO_TICKS(10)
    );
  }
}
