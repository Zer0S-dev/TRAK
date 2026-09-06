#include "DevLog.h"
#include "StatusLedManager.h"
#include "ModemManager.h"
#include "Config.h"
#include "RuntimeConfig.h"
#include <ctype.h>

HardwareSerial modemSerial(1);
static SemaphoreHandle_t modemMutex = nullptr;
static volatile bool modemSerialReady = false;

static bool lockModem(uint32_t timeoutMs = 15000)
{
  if (modemMutex == nullptr) {
    DevSerial.println("[MODEM] Mutex non initialise.");
    return false;
  }

  return xSemaphoreTakeRecursive(modemMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

static void unlockModem()
{
  if (modemMutex != nullptr) {
    xSemaphoreGiveRecursive(modemMutex);
  }
}

void modemMutexBegin()
{
  if (modemMutex != nullptr) return;

  modemMutex = xSemaphoreCreateRecursiveMutex();

  if (modemMutex == nullptr) {
    DevSerial.println("[MODEM] ERREUR : impossible de creer le mutex.");
  } else {
    DevSerial.println("[MODEM] Mutex transactionnel A7670 pret.");
  }
}

bool modemCommunicationReady()
{
  return modemSerialReady;
}

String sendATCommand(const char* cmd, uint32_t timeoutMs)
{
  String response;
  response.reserve(256);

  if (!lockModem(timeoutMs + 1000)) return response;

  while (modemSerial.available()) modemSerial.read();
  modemSerial.println(cmd);

  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (modemSerial.available()) {
      response += static_cast<char>(modemSerial.read());
    }
    delay(1);
  }

  unlockModem();
  return response;
}

void beginModemSerial()
{
  modemMutexBegin();
  modemSerial.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  modemSerialReady = true;
}

void powerOnModem()
{
  modemMutexBegin();

  pinMode(MODEM_RESET, OUTPUT);
  digitalWrite(MODEM_RESET, HIGH);

  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(100);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, LOW);

  DevSerial.println("Démarrage du modem A7670...");
  delay(3000);
}

void configureMultiGNSS()
{
  DevSerial.println("Activation du mode Multi-Constellations...");

  sendATCommand("AT+CGNSSPWR=0", 1000);
  delay(500);
  sendATCommand("AT+CGNSSMODE=15", 1000);
  sendATCommand("AT+CGNSSTST=0", 1000);
  sendATCommand("AT+CGNSSPWR=1", 2000);

  delay(1000);
  DevSerial.println("GNSS prêt !");
}

String detectAutoAPN()
{
  DevSerial.println("[AUTO-APN] Lecture IMSI...");

  String response = sendATCommand("AT+CIMI", 2000);

  char mccmnc[6] = {0};
  int idx = 0;

  for (size_t i = 0; i < response.length() && idx < 5; ++i) {
    if (isDigit(response[i])) mccmnc[idx++] = response[i];
  }

  if (idx == 5) {
    DevSerial.print("[AUTO-APN] Code MCC/MNC : ");
    DevSerial.println(mccmnc);

    for (size_t i = 0; i < apnDatabaseSize; ++i) {
      if (strcmp(mccmnc, apnDatabase[i].mccmnc) == 0) {
        DevSerial.print("[AUTO-APN] APN trouvé : ");
        DevSerial.println(apnDatabase[i].apn);
        return String(apnDatabase[i].apn);
      }
    }
  }

  DevSerial.print("[AUTO-APN] APN par défaut : ");
  DevSerial.println(DEFAULT_APN);
  return String(DEFAULT_APN);
}

int getCellularSignalQuality()
{
  String res = sendATCommand("AT+CSQ", 500);
  const int idx = res.indexOf("+CSQ:");

  if (idx >= 0) {
    const int csq = res.substring(idx + 6).toInt();
    if (csq != 99) return constrain((csq * 100) / 31, 0, 100);
  }

  return 0;
}

bool readGNSS(GnssData& data)
{
  String response = sendATCommand("AT+CGNSSINFO", 1000);

  const int infoIndex = response.indexOf("+CGNSSINFO:");
  if (infoIndex < 0) return false;

  String dataStr = response.substring(infoIndex + 11);
  dataStr.trim();

  char rawData[160];
  dataStr.toCharArray(rawData, sizeof(rawData));

  char* tokens[20] = {};
  int tokenCount = 0;
  char* running = rawData;

  while (running != nullptr && tokenCount < 20) {
    tokens[tokenCount++] = strsep(&running, ",");
  }

  if (tokenCount < 9) return false;

  const int fixMode = (tokens[0] && *tokens[0]) ? atoi(tokens[0]) : 0;
  const int gpsSats = (tokens[1] && *tokens[1]) ? atoi(tokens[1]) : 0;
  const int glonassSats = (tokens[2] && *tokens[2]) ? atoi(tokens[2]) : 0;
  const int beidouSats = (tokens[3] && *tokens[3]) ? atoi(tokens[3]) : 0;
  const int galileoSats = (tokens[4] && *tokens[4]) ? atoi(tokens[4]) : 0;
  const int totalSats = gpsSats + glonassSats + beidouSats + galileoSats;

  float latitude = (tokens[5] && *tokens[5]) ? atof(tokens[5]) : 0.0f;
  const char* latDir = tokens[6];
  float longitude = (tokens[7] && *tokens[7]) ? atof(tokens[7]) : 0.0f;
  const char* lonDir = tokens[8];

  if (latDir && strcmp(latDir, "S") == 0) latitude = -latitude;
  if (lonDir && strcmp(lonDir, "W") == 0) longitude = -longitude;

  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;

  if (tokenCount > 9 && tokens[9] && strlen(tokens[9]) >= 6) {
    const int dd = (tokens[9][0]-'0')*10 + (tokens[9][1]-'0');
    const int mm = (tokens[9][2]-'0')*10 + (tokens[9][3]-'0');
    const int yy = (tokens[9][4]-'0')*10 + (tokens[9][5]-'0');
    if (dd >= 1 && dd <= 31 && mm >= 1 && mm <= 12) {
      day = (uint8_t)dd;
      month = (uint8_t)mm;
      year = (uint16_t)(2000 + yy);
    }
  }

  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  uint16_t millisecond = 0;

  if (tokenCount > 10 && tokens[10] && strlen(tokens[10]) >= 6) {
    const char* utc = tokens[10];
    if (isdigit(utc[0]) && isdigit(utc[1]) && isdigit(utc[2]) &&
        isdigit(utc[3]) && isdigit(utc[4]) && isdigit(utc[5])) {
      hour = static_cast<uint8_t>((utc[0]-'0')*10 + (utc[1]-'0'));
      minute = static_cast<uint8_t>((utc[2]-'0')*10 + (utc[3]-'0'));
      second = static_cast<uint8_t>((utc[4]-'0')*10 + (utc[5]-'0'));
      const char* dot = strchr(utc, '.');
      if (dot && dot[1] && isdigit(dot[1])) {
        millisecond = static_cast<uint16_t>((dot[1]-'0') * 100);
        if (dot[2] && isdigit(dot[2])) millisecond += static_cast<uint16_t>((dot[2]-'0') * 10);
        if (dot[3] && isdigit(dot[3])) millisecond += static_cast<uint16_t>(dot[3]-'0');
      }
    }
  }

  if (year >= 2020 && month >= 1 && month <= 12 && day >= 1 && day <= 31) {
    devLogSyncTime(year, month, day, hour, minute, second, millisecond);
  }

  data.fixMode = fixMode;
  data.satellites = totalSats;
  data.gpsSatellites = gpsSats;
  data.glonassSatellites = glonassSats;
  data.beidouSatellites = beidouSats;
  data.galileoSatellites = galileoSats;
  data.latitude = latitude;
  data.longitude = longitude;
  data.year = year;
  data.month = month;
  data.day = day;
  data.hour = hour;
  data.minute = minute;
  data.second = second;
  data.millisecond = millisecond;
  data.hasFix = (fixMode >= 2) && (latitude != 0.0f) && (longitude != 0.0f);

  const float rawAltitude = (tokenCount > 11 && tokens[11] && *tokens[11]) ? atof(tokens[11]) : 0.0f;
  const float rawSpeedKmh = (tokenCount > 12 && tokens[12] && *tokens[12]) ? atof(tokens[12]) * 1.852f : 0.0f;

  if (!data.hasFix) {
    data.filteredAltitude = rawAltitude;
    data.filteredSpeedKmh = rawSpeedKmh;
  } else {
    data.filteredAltitude = data.filteredAltitude + ALTITUDE_FILTER_ALPHA * (rawAltitude - data.filteredAltitude);
    data.filteredSpeedKmh = data.filteredSpeedKmh + SPEED_FILTER_ALPHA * (rawSpeedKmh - data.filteredSpeedKmh);
  }

  if (data.filteredSpeedKmh < SPEED_ZERO_THRESHOLD_KMH) data.filteredSpeedKmh = 0.0f;

  data.rawAltitude = rawAltitude;
  data.rawSpeedKmh = rawSpeedKmh;
  data.altitude = data.filteredAltitude;
  data.speedKmh = data.filteredSpeedKmh;
  data.satellitesUpdatedAt = millis();

  return true;
}

static bool gnssTimeToUnixSeconds(
    uint16_t year, uint8_t month, uint8_t day,
    uint8_t hour, uint8_t minute, uint8_t second,
    uint64_t& epoch)
{
  if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 ||
      hour > 23 || minute > 59 || second > 59) return false;

  int64_t y = static_cast<int64_t>(year);
  const unsigned m = month;
  const unsigned d = day;
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = era * 146097 + static_cast<int64_t>(doe) - 719468;
  const int64_t seconds = days * 86400LL + static_cast<int64_t>(hour) * 3600LL + static_cast<int64_t>(minute) * 60LL + static_cast<int64_t>(second);
  if (seconds < 0) return false;
  epoch = static_cast<uint64_t>(seconds);
  return true;
}

static void appendTrackserverTimestamp(
    String& target,
    uint16_t year, uint8_t month, uint8_t day,
    uint8_t hour, uint8_t minute, uint8_t second, uint16_t millisecond)
{
  uint64_t epoch = 0;
  if (gnssTimeToUnixSeconds(year, month, day, hour, minute, second, epoch)) {
    char epochText[24];
    snprintf(epochText, sizeof(epochText), "%llu", static_cast<unsigned long long>(epoch));
    target += "&timestamp=";
    target += epochText;
    (void)millisecond;
  }
}

bool sendToTrackserverWiFi(
    float lat, float lon, float altitude, float speedKmh,
    uint16_t year, uint8_t month, uint8_t day,
    uint8_t hour, uint8_t minute, uint8_t second, uint16_t millisecond,
    uint32_t& bytesSent)
{
  bytesSent = 0;

  if (WiFi.status() != WL_CONNECTED) {
    DevSerial.println("[WIFI] Non connecté : envoi annulé.");
    return false;
  }

  TrackserverConfig ts;
  if (!getTrackserverConfig(ts)) {
    DevSerial.println("[WIFI] Configuration Trackserver indisponible.");
    return false;
  }

  char path[TRACKSERVER_PATH_MAX_LEN + 96];
  snprintf(path, sizeof(path), "%s%slat=%.6f&lon=%.6f&altitude=%.1f&speed=%.1f",
           ts.path, strchr(ts.path, '?') ? "&" : "?", lat, lon, altitude, speedKmh);

  String fullPath = String(path);
  appendTrackserverTimestamp(fullPath, year, month, day, hour, minute, second, millisecond);

  WiFiClient client;
  if (!client.connect(ts.host, 80)) {
    DevSerial.println("[WIFI] Connexion Trackserver impossible.");
    return false;
  }

  client.print("GET ");
  client.print(fullPath);
  client.print(" HTTP/1.1\r\nHost: ");
  client.print(ts.host);
  client.print("\r\nConnection: close\r\n\r\n");

  bytesSent = (uint32_t)(strlen("GET ") + fullPath.length() + strlen(" HTTP/1.1\r\nHost: ") + strlen(ts.host) + strlen("\r\nConnection: close\r\n\r\n"));

  const uint32_t start = millis();
  while (!client.available() && millis() - start < 3000) {
    statusLedService();
    delay(1);
  }

  if (!client.available()) {
    DevSerial.println("[WIFI] Timeout réponse HTTP.");
    client.stop();
    return false;
  }

  String statusLine = client.readStringUntil('\n');
  statusLine.trim();

  const bool success = statusLine.startsWith("HTTP/1.1 200") || statusLine.startsWith("HTTP/1.0 200");
  if (success) DevSerial.println(">> Success (Wi-Fi)");
  else {
    DevSerial.print("[WIFI] Réponse HTTP : ");
    DevSerial.println(statusLine);
  }

  client.stop();
  return success;
}

bool sendToTrackserverCellular(
    float lat, float lon, float altitude, float speedKmh,
    uint16_t year, uint8_t month, uint8_t day,
    uint8_t hour, uint8_t minute, uint8_t second, uint16_t millisecond,
    uint32_t& bytesSent)
{
  bytesSent = 0;
  static bool httpReady = false;

  if (!lockModem(15000)) {
    DevSerial.println("[4G] Modem occupe : transaction HTTP annulée.");
    return false;
  }

  if (!httpReady) {
    DevSerial.println("[4G] HTTPINIT...");
    String initRes = sendATCommand("AT+HTTPINIT", 1500);
    String initLog = initRes;
    initLog.replace("\r", "\\r");
    initLog.replace("\n", "\\n");
    DevSerial.print("[4G] HTTPINIT réponse: ");
    DevSerial.println(initLog);

    if (initRes.indexOf("OK") < 0) {
      DevSerial.println("[4G] HTTPINIT impossible : session HTTP non disponible.");
      String termRes = sendATCommand("AT+HTTPTERM", 1000);
      String termLog = termRes;
      termLog.replace("\r", "\\r");
      termLog.replace("\n", "\\n");
      DevSerial.print("[4G] HTTPTERM après échec: ");
      DevSerial.println(termLog);
      httpReady = false;
      unlockModem();
      return false;
    }

    httpReady = true;
    DevSerial.println("[4G] Session HTTP prête.");
  }

  TrackserverConfig ts;
  if (!getTrackserverConfig(ts)) {
    DevSerial.println("[4G] Configuration Trackserver indisponible.");
    unlockModem();
    return false;
  }

  char url[TRACKSERVER_URL_MAX_LEN + 96];
  snprintf(url, sizeof(url), "%s%slat=%.6f&lon=%.6f&altitude=%.1f&speed=%.1f",
           ts.url, strchr(ts.url, '?') ? "&" : "?", lat, lon, altitude, speedKmh);

  String fullUrl = String(url);
  appendTrackserverTimestamp(fullUrl, year, month, day, hour, minute, second, millisecond);

  char cmdUrl[TRACKSERVER_URL_MAX_LEN + 120];
  snprintf(cmdUrl, sizeof(cmdUrl), "AT+HTTPPARA=\"URL\",\"%s\"", fullUrl.c_str());

  String urlRes = sendATCommand(cmdUrl, 1000);
  if (urlRes.indexOf("OK") < 0) {
    DevSerial.println("[4G] HTTPPARA URL impossible - reinitialisation au prochain envoi.");
    sendATCommand("AT+HTTPTERM", 500);
    httpReady = false;
    unlockModem();
    return false;
  }

  bytesSent = (uint32_t)(strlen("GET ") + fullUrl.length() + strlen(" HTTP/1.1\r\nHost: ") + strlen(ts.host) + strlen("\r\nConnection: close\r\n\r\n"));

  String actionRes = sendATCommand("AT+HTTPACTION=0", 8000);
  const bool success = actionRes.indexOf("+HTTPACTION: 0,200") >= 0;

  if (success) {
    DevSerial.println(">> Success (4G)");
  } else {
    DevSerial.print("[!] Erreur HTTP 4G: ");
    DevSerial.println(actionRes);
    sendATCommand("AT+HTTPTERM", 500);
    httpReady = false;
  }

  unlockModem();
  return success;
}

// HTTP POST générique pour les services applicatifs du TRAK (ex. TRAK Connect).
// Cette fonction ne dépend volontairement pas de TrackserverConfig.
bool sendHttpPostCellular(const String& url, const String& body, uint32_t& bytesSent)
{
  bytesSent = 0;

  if (!modemSerialReady || url.length() == 0 || body.length() == 0) {
    return false;
  }

  if (!lockModem(20000)) {
    DevSerial.println("[4G] Modem occupe : POST HTTP annulé.");
    return false;
  }

  bool success = false;
  bool httpReady = false;

  do {
    /*
     * Configuration SSL du contexte 0.
     *
     * L'A7670 utilise AT+CSSLCFG pour configurer TLS.
     * SNI est indispensable pour les hébergements mutualisés
     * comme notre WordPress OVH.
     *
     * authmode=0 :
     *   pas de vérification du certificat pour cette version TEST.
     *
     * sslversion=4 :
     *   autorise la négociation TLS disponible sur le modem.
     */
    String sslVersionRes =
        sendATCommand("AT+CSSLCFG=\"sslversion\",0,4", 1000);

    if (sslVersionRes.indexOf("OK") < 0) {
      DevSerial.println(
          "[TRAK-CONNECT][4G] Configuration TLS impossible.");
      break;
    }

    String sniRes =
        sendATCommand("AT+CSSLCFG=\"enableSNI\",0,1", 1000);

    if (sniRes.indexOf("OK") < 0) {
      DevSerial.println(
          "[TRAK-CONNECT][4G] Activation SNI impossible.");
      break;
    }

    String authRes =
        sendATCommand("AT+CSSLCFG=\"authmode\",0,0", 1000);

    if (authRes.indexOf("OK") < 0) {
      DevSerial.println(
          "[TRAK-CONNECT][4G] Configuration authmode impossible.");
      break;
    }

    /*
     * Démarrage du service HTTP(S).
     */
    String initRes = sendATCommand("AT+HTTPINIT", 1500);

    if (initRes.indexOf("OK") < 0) {
      // Une session résiduelle peut rester après une erreur précédente.
      sendATCommand("AT+HTTPTERM", 1000);

      initRes = sendATCommand("AT+HTTPINIT", 1500);
    }

    if (initRes.indexOf("OK") < 0) {
      DevSerial.println(
          "[TRAK-CONNECT][4G] HTTPINIT impossible.");
      break;
    }

    httpReady = true;

    /*
     * Associer le contexte SSL 0 à la session HTTP.
     */
    String sslCfgRes =
        sendATCommand("AT+HTTPPARA=\"SSLCFG\",0", 1000);

    if (sslCfgRes.indexOf("OK") < 0) {
      DevSerial.println(
          "[TRAK-CONNECT][4G] HTTPPARA SSLCFG impossible.");
      break;
    }

    /*
     * URL HTTPS.
     *
     * L'A7670 détecte HTTPS directement grâce au préfixe
     * "https://".
     */
    String urlCmd = "AT+HTTPPARA=\"URL\",\"";
    urlCmd += url;
    urlCmd += "\"";

    if (sendATCommand(urlCmd.c_str(), 1500).indexOf("OK") < 0) {
      DevSerial.println(
          "[TRAK-CONNECT][4G] HTTPPARA URL impossible.");
      break;
    }

    if (sendATCommand(
            "AT+HTTPPARA=\"CONTENT\",\"application/json\"",
            1000).indexOf("OK") < 0) {

      DevSerial.println(
          "[TRAK-CONNECT][4G] HTTPPARA CONTENT impossible.");
      break;
    }

    if (sendATCommand(
            "AT+HTTPPARA=\"ACCEPT\",\"application/json\"",
            1000).indexOf("OK") < 0) {

      DevSerial.println(
          "[TRAK-CONNECT][4G] HTTPPARA ACCEPT impossible.");
      break;
    }

    /*
     * Préparation du payload POST.
     */
    String dataCmd = "AT+HTTPDATA=";
    dataCmd += String(body.length());
    dataCmd += ",10000";

    String dataRes = sendATCommand(dataCmd.c_str(), 1500);

    if (dataRes.indexOf("DOWNLOAD") < 0 &&
        dataRes.indexOf(">") < 0) {

      DevSerial.println(
          "[TRAK-CONNECT][4G] HTTPDATA : pas de prompt DOWNLOAD.");
      break;
    }

    /*
     * Envoi du JSON brut.
     *
     * Le mutex principal est relâché par sendATCommand(),
     * on le reprend donc avant d'écrire directement dans
     * modemSerial.
     */
    if (!lockModem(5000)) {
      DevSerial.println(
          "[TRAK-CONNECT][4G] Mutex perdu avant payload HTTP.");
      break;
    }

    while (modemSerial.available()) {
      modemSerial.read();
    }

    modemSerial.print(body);

    const uint32_t bodyStart = millis();
    String bodyRes;

    while (millis() - bodyStart < 12000) {
      while (modemSerial.available()) {
        bodyRes += static_cast<char>(modemSerial.read());
      }

      if (bodyRes.indexOf("OK") >= 0 ||
          bodyRes.indexOf("ERROR") >= 0) {
        break;
      }

      delay(1);
    }

    unlockModem();

    if (bodyRes.indexOf("OK") < 0) {
      DevSerial.println(
          "[TRAK-CONNECT][4G] Payload HTTP refusé.");
      break;
    }

    /*
     * POST = HTTPACTION 1.
     */
    String actionRes =
        sendATCommand("AT+HTTPACTION=1", 20000);

    if (actionRes.indexOf("+HTTPACTION: 1,200") >= 0) {
      success = true;
      bytesSent = (uint32_t)body.length();

      DevSerial.println(
          "[TRAK-CONNECT][4G] POST HTTPS 200.");
    } else {
      DevSerial.print(
          "[TRAK-CONNECT][4G] Echec HTTPACTION: ");
      DevSerial.println(actionRes);
    }

  } while (false);

  if (httpReady) {
    sendATCommand("AT+HTTPTERM", 1000);
  }

  unlockModem();
  return success;
}

bool connectCellularNetwork()
{
  DevSerial.println("[4G] Activation connexion data...");

  if (!lockModem(15000)) {
    DevSerial.println("[4G] Modem occupe : activation data annulée.");
    return false;
  }

  String detectedAPN = detectAutoAPN();
  char apnCmd[96];
  snprintf(apnCmd, sizeof(apnCmd), "AT+CGDCONT=1,\"IP\",\"%s\"", detectedAPN.c_str());

  const String apnRes = sendATCommand(apnCmd, 1500);
  if (apnRes.indexOf("OK") < 0) {
    DevSerial.println("[4G] Configuration APN impossible.");
    unlockModem();
    return false;
  }

  const String actRes = sendATCommand("AT+CGACT=1,1", 5000);
  if (actRes.indexOf("OK") < 0 && actRes.indexOf("+CGACT: 1,1") < 0) {
    DevSerial.println("[4G] PDP indisponible (SIM/reseau/data). ");
    unlockModem();
    return false;
  }

  DevSerial.println("[4G] PDP actif.");
  unlockModem();
  return true;
}

bool sendSMS(const char* phoneNumber, const char* message)
{
  if (!modemSerialReady) {
    DevSerial.println("[SMS] Modem serie non initialise.");
    return false;
  }

  if (phoneNumber == nullptr || message == nullptr || strlen(phoneNumber) == 0 || strlen(message) == 0) return false;
  if (!lockModem(15000)) {
    DevSerial.println("[SMS] Modem occupe.");
    return false;
  }

  bool success = false;
  for (uint8_t attempt = 1; attempt <= 2 && !success; ++attempt) {
    if (attempt > 1) {
      DevSerial.println("[SMS] Nouvelle tentative...");
      delay(800);
      while (modemSerial.available()) modemSerial.read();
    }

    modemSerial.println("AT+CMGF=1");
    uint32_t start = millis();
    String response;
    response.reserve(256);

    while (millis() - start < 2500) {
      while (modemSerial.available()) response += (char)modemSerial.read();
      if (response.indexOf("OK") >= 0) break;
      if (response.indexOf("ERROR") >= 0 || response.indexOf("+CMS ERROR") >= 0) break;
      delay(1);
    }

    if (response.indexOf("OK") < 0) {
      DevSerial.print("[SMS] AT+CMGF=1 refuse (tentative ");
      DevSerial.print(attempt);
      DevSerial.println(").");
      modemSerial.write((uint8_t)27);
      delay(100);
      while (modemSerial.available()) modemSerial.read();
      continue;
    }

    modemSerial.println("AT+CSCS=\"GSM\"");
    start = millis();
    response = "";
    while (millis() - start < 2500) {
      while (modemSerial.available()) response += (char)modemSerial.read();
      if (response.indexOf("OK") >= 0) break;
      if (response.indexOf("ERROR") >= 0 || response.indexOf("+CMS ERROR") >= 0) break;
      delay(1);
    }

    while (modemSerial.available()) modemSerial.read();
    modemSerial.print("AT+CMGS=\"");
    modemSerial.print(phoneNumber);
    modemSerial.println("\"");

    start = millis();
    response = "";
    bool prompt = false;
    while (millis() - start < 7000) {
      while (modemSerial.available()) {
        const char c = (char)modemSerial.read();
        response += c;
        if (c == '>') {
          prompt = true;
          break;
        }
      }
      if (prompt) break;
      if (response.indexOf("ERROR") >= 0 || response.indexOf("+CMS ERROR") >= 0) break;
      delay(1);
    }

    if (!prompt) {
      DevSerial.print("[SMS] Pas de prompt CMGS (tentative ");
      DevSerial.print(attempt);
      DevSerial.println(").");
      if (response.length()) {
        DevSerial.print("[SMS] Modem: ");
        DevSerial.println(response);
      }
      modemSerial.write((uint8_t)27);
      delay(200);
      while (modemSerial.available()) modemSerial.read();
      continue;
    }

    modemSerial.print(message);
    modemSerial.write((uint8_t)26);
    start = millis();
    response = "";
    while (millis() - start < 20000) {
      while (modemSerial.available()) response += (char)modemSerial.read();
      if (response.indexOf("+CMGS:") >= 0 || response.indexOf("OK") >= 0) {
        success = true;
        break;
      }
      if (response.indexOf("ERROR") >= 0 || response.indexOf("+CMS ERROR") >= 0) break;
      delay(1);
    }

    if (!success) {
      DevSerial.print("[SMS] Echec tentative ");
      DevSerial.println(attempt);
      if (response.length()) {
        DevSerial.print("[SMS] Modem: ");
        DevSerial.println(response);
      }
      modemSerial.write((uint8_t)27);
      delay(200);
      while (modemSerial.available()) modemSerial.read();
    }
  }

  unlockModem();

  if (success) DevSerial.println("[SMS] Envoye.");
  else DevSerial.println("[SMS] Echec envoi.");
  return success;
}

bool readIncomingSMS(String& sender, String& message)
{
  sender = "";
  message = "";
  if (!modemSerialReady) return false;
  if (!lockModem(1500)) return false;

  bool found = false;
  int messageIndex = -1;
  String response;
  response.reserve(1024);

  while (modemSerial.available()) modemSerial.read();
  modemSerial.println("AT+CMGF=1");
  uint32_t start = millis();
  while (millis() - start < 2500) {
    while (modemSerial.available()) response += static_cast<char>(modemSerial.read());
    if (response.indexOf("OK") >= 0) break;
    if (response.indexOf("ERROR") >= 0 || response.indexOf("+CMS ERROR") >= 0) break;
    delay(1);
  }

  if (response.indexOf("OK") < 0) {
    unlockModem();
    return false;
  }

  response = "";
  while (modemSerial.available()) modemSerial.read();
  modemSerial.println("AT+CMGL=\"REC UNREAD\"");
  start = millis();

  while (millis() - start < 5000) {
    while (modemSerial.available()) response += static_cast<char>(modemSerial.read());
    if (response.indexOf("\r\nOK") >= 0 || response.endsWith("OK\r\n") || response.indexOf("\nERROR") >= 0 || response.indexOf("\n+CMS ERROR") >= 0) break;
    delay(1);
  }

  const int headerPos = response.indexOf("+CMGL:");
  if (headerPos < 0) {
    unlockModem();
    return false;
  }

  int comma = response.indexOf(',', headerPos);
  if (comma < 0) {
    unlockModem();
    return false;
  }

  messageIndex = response.substring(headerPos + 6, comma).toInt();
  const int firstQuote = response.indexOf('"', comma);
  const int secondQuote = firstQuote >= 0 ? response.indexOf('"', firstQuote + 1) : -1;
  const int thirdQuote = secondQuote >= 0 ? response.indexOf('"', secondQuote + 1) : -1;
  const int fourthQuote = thirdQuote >= 0 ? response.indexOf('"', thirdQuote + 1) : -1;

  if (thirdQuote < 0 || fourthQuote < 0) {
    unlockModem();
    return false;
  }

  sender = response.substring(thirdQuote + 1, fourthQuote);
  const int headerEnd = response.indexOf('\n', fourthQuote);
  if (headerEnd < 0) {
    unlockModem();
    return false;
  }

  int bodyStart = headerEnd + 1;
  while (bodyStart < static_cast<int>(response.length()) && (response[bodyStart] == '\r' || response[bodyStart] == '\n')) ++bodyStart;
  int bodyEnd = response.indexOf('\n', bodyStart);
  if (bodyEnd < 0) bodyEnd = response.length();

  message = response.substring(bodyStart, bodyEnd);
  message.trim();

  if (messageIndex >= 0) {
    String delCmd = "AT+CMGD=";
    delCmd += String(messageIndex);
    modemSerial.println(delCmd);

    start = millis();
    String delResponse;
    while (millis() - start < 2500) {
      while (modemSerial.available()) delResponse += static_cast<char>(modemSerial.read());
      if (delResponse.indexOf("OK") >= 0 || delResponse.indexOf("ERROR") >= 0 || delResponse.indexOf("+CMS ERROR") >= 0) break;
      delay(1);
    }
  }

  found = sender.length() > 0 && message.length() > 0;
  unlockModem();
  return found;
}
