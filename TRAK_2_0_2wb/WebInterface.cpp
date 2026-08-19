#include "WebInterface.h"
#include "Config.h"
#include "RuntimeConfig.h"

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>

namespace {

WebServer server(80);
WebDataProvider dataProvider = nullptr;
bool started = false;

String sessionToken;

Preferences authPreferences;

const char* AP_SSID = "TRAK";
const char* AP_PASSWORD = "trak@dmin";

constexpr const char* AUTH_NAMESPACE = "trak_auth";
constexpr const char* AUTH_TOKEN_KEY = "session_token";

String jsonEscape(const char* value)
{
  String out;
  if (value == nullptr) return out;

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

String jsonStringArg(const String& body, const char* key)
{
  String needle = String("\"") + key + "\"";

  int pos = body.indexOf(needle);

  if (pos < 0)
    return String();

  pos = body.indexOf(':', pos + needle.length());

  if (pos < 0)
    return String();

  while (
    pos + 1 < (int)body.length() &&
    isspace((unsigned char)body[pos + 1])
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

  while (pos < (int)body.length()) {

    char c = body[pos++];

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
  String needle = String("\"") + key + "\"";

  int pos = body.indexOf(needle);

  if (pos < 0)
    return fallback;

  pos = body.indexOf(
    ':',
    pos + needle.length()
  );

  if (pos < 0)
    return fallback;

  ++pos;

  while (
    pos < (int)body.length() &&
    isspace((unsigned char)body[pos])
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
  String needle = String("\"") + key + "\"";

  int pos = body.indexOf(needle);

  if (pos < 0)
    return fallback;

  pos = body.indexOf(
    ':',
    pos + needle.length()
  );

  if (pos < 0)
    return fallback;

  ++pos;

  while (
    pos < (int)body.length() &&
    isspace((unsigned char)body[pos])
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


/*
 * Charge le token de session depuis la NVS.
 *
 * Le token survit donc à un redémarrage
 * du tracker.
 */
static void loadSessionToken()
{
  if (!authPreferences.begin(
        AUTH_NAMESPACE,
        false
      )) {

    Serial.println(
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

  if (sessionToken.length() > 0) {

    Serial.println(
      "[AUTH] Session persistante chargée"
    );

  } else {

    Serial.println(
      "[AUTH] Aucune session persistante"
    );
  }
}


/*
 * Sauvegarde le token de session dans la NVS.
 */
static void saveSessionToken()
{
  if (!authPreferences.putString(
        AUTH_TOKEN_KEY,
        sessionToken
      )) {

    Serial.println(
      "[AUTH] Erreur sauvegarde session"
    );

  } else {

    Serial.println(
      "[AUTH] Session persistante sauvegardée"
    );
  }
}


/*
 * Supprime le token de session.
 */
static void clearSessionToken()
{
  sessionToken = String();

  authPreferences.remove(
    AUTH_TOKEN_KEY
  );

  Serial.println(
    "[AUTH] Session supprimée"
  );
}


/*
 * Vérifie le cookie TRAK_SESSION.
 */
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


/*
 * Vérifie l'authentification.
 */
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


  /*
   * Si une session existe déjà, on la conserve.
   *
   * Cela évite de changer inutilement le token
   * lors d'un nouveau passage par la page de login.
   */
  if (sessionToken.length() == 0) {

    sessionToken =
      makeSessionToken();

    saveSessionToken();

    Serial.println(
      "[AUTH] Nouvelle session créée"
    );

  } else {

    Serial.println(
      "[AUTH] Session existante réutilisée"
    );
  }


  /*
   * Cookie persistant côté WebView.
   *
   * IMPORTANT :
   * le cookie reste lié à l'hôte HTTP.
   * MainActivity devra donc gérer le passage
   * 192.168.1.30 <-> 192.168.4.1.
   */
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


  server.send(
    200,
    "application/json",
    "{\"ok\":true}"
  );
}


// ============================================================
// SESSION
// ============================================================

/*
 * Nouvelle API :
 *
 * GET /api/session
 *
 * Réponse si connecté :
 *
 * {
 *   "authenticated":true
 * }
 *
 * Sinon :
 *
 * {
 *   "authenticated":false
 * }
 */
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
  if (!d.motionCalibrated)
    return "CALIBRATION";

  if (d.moving)
    return "MOBILE";

  if (d.stationaryConfirmed)
    return "STATIONNAIRE";

  return "IMMOBILE_CONFIRMATION";
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


  if (!LittleFS.exists("/login.html")) {

    server.send(
      500,
      "text/plain; charset=utf-8",
      "/data/login.html absent. Uploade le LittleFS filesystem."
    );

    return;
  }


  File file =
    LittleFS.open(
      "/login.html",
      "r"
    );


  if (!file) {

    server.send(
      500,
      "text/plain; charset=utf-8",
      "Impossible d'ouvrir /login.html"
    );

    return;
  }


  server.streamFile(
    file,
    "text/html; charset=utf-8"
  );

  file.close();
}


// ============================================================
// ROOT
// ============================================================

static void handleRoot()
{
  if (!authenticated(false))
    return;


  if (!LittleFS.exists("/index.html")) {

    server.send(
      500,
      "text/plain; charset=utf-8",
      "/data/index.html absent. Uploade le LittleFS filesystem."
    );

    return;
  }


  File file =
    LittleFS.open(
      "/index.html",
      "r"
    );


  if (!file) {

    server.send(
      500,
      "text/plain; charset=utf-8",
      "Impossible d'ouvrir /index.html"
    );

    return;
  }


  server.streamFile(
    file,
    "text/html; charset=utf-8"
  );

  file.close();
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
  json += networkModeName();

  json += "\",\"network\":\"";
  json += d.activeNetworkName;

  json += "\",\"preferredNetwork\":\"";
  json += networkModeName();

  json += "\",\"firmwareVersion\":\"";
  json += FIRMWARE_VERSION;

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

  json += ",\"motionXG\":";
  json += String(d.motionXG, 3);

  json += ",\"motionYG\":";
  json += String(d.motionYG, 3);

  json += ",\"motionG\":";
  json += String(d.motionG, 3);

  json += ",\"moving\":";
  json += d.moving ? "true" : "false";

  json += ",\"stationaryConfirmed\":";
  json += d.stationaryConfirmed ? "true" : "false";

  json += ",\"motionCalibrated\":";
  json += d.motionCalibrated ? "true" : "false";

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
  json += d.lastTxAt;;

  json += ",\"bufferCount\":";
  json += d.bufferCount;

  json += ",\"bufferCapacity\":";
  json += d.bufferCapacity;


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
// TRACKSERVER
// ============================================================

static void handleTrackserverGet()
{
  if (!authenticated())
    return;


  TrackserverConfig ts;


  if (!getTrackserverConfig(ts)) {

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


  if (!saveTrackserverUrl(url.c_str())) {

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
// MODE
// ============================================================

static void handleMode()
{
  if (!authenticated())
    return;


  String body =
    server.arg("plain");


  if (
    body.indexOf(
      "\"mode\":\"wifi\""
    ) >= 0
  ) {

    setNetworkMode(
      NetworkMode::WIFI
    );

  } else if (
    body.indexOf(
      "\"mode\":\"cellular\""
    ) >= 0
  ) {

    setNetworkMode(
      NetworkMode::CELLULAR
    );

  } else {

    server.send(
      400,
      "text/plain",
      "mode invalide"
    );

    return;
  }


  server.send(
    200,
    "text/plain",
    "Préférence enregistrée. Redémarrage..."
  );


  delay(300);

  ESP.restart();
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


    if (getWiFiProfile(i, p)) {

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
    "Profil Wi-Fi enregistré"
  );
}


static void handleWiFiDelete()
{
  if (!authenticated())
    return;


  const int slot =
    server.arg("slot").toInt();


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

} // namespace


// ============================================================
// WEB BEGIN
// ============================================================

void webBegin(
  WebDataProvider provider
)
{
  dataProvider = provider;


  if (!LittleFS.begin(false)) {

    Serial.println(
      "[WEB] LittleFS absent/inaccessible. Interface non disponible."
    );

    return;
  }


  /*
   * Initialise l'authentification persistante.
   */
  loadSessionToken();


  server.serveStatic(
    "/logo_dark_web.png",
    LittleFS,
    "/logo_dark_web.png"
  );


  WiFi.mode(
    WIFI_AP_STA
  );


  WiFi.softAP(
    AP_SSID,
    AP_PASSWORD
  );


  if (MDNS.begin("trak"))
    MDNS.addService(
      "http",
      "tcp",
      80
    );


  const char* headerKeys[] = {
    "Cookie"
  };


  server.collectHeaders(
    headerKeys,
    1
  );


  // ----------------------------------------------------------
  // AUTHENTIFICATION
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


  // ----------------------------------------------------------
  // CONFIGURATION
  // ----------------------------------------------------------

  server.on(
    "/api/data-usage",
    HTTP_POST,
    handleDataUsageSave
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
    "/api/mode",
    HTTP_POST,
    handleMode
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


  /*
   * Pas de serveStatic("/") :
   * sinon les fichiers LittleFS contourneraient
   * le contrôle d'accès.
   *
   * Seuls login.html et index.html
   * sont servis explicitement.
   */


  server.begin();

  started = true;


  Serial.print(
    "[WEB] AP local : http://"
  );

  Serial.println(
    WiFi.softAPIP()
  );


  Serial.println(
    "[WEB] Profils Wi-Fi : 3 maximum, gestion via Web/NVS"
  );


  Serial.println(
    "[WEB] Authentification : /login.html + session cookie persistante"
  );


  Serial.println(
    "[WEB] API session : /api/session"
  );


  Serial.println(
    "[WEB] Dashboard : LittleFS:/index.html (session requise)"
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