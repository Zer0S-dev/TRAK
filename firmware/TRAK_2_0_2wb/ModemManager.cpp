#include "ModemManager.h"
#include "Config.h"
#include "RuntimeConfig.h"

HardwareSerial modemSerial(1);

String sendATCommand(const char* cmd, uint32_t timeoutMs)
{
  String response;
  response.reserve(256);

  while (modemSerial.available()) {
    modemSerial.read();
  }

  modemSerial.println(cmd);

  const uint32_t start = millis();

  while (millis() - start < timeoutMs) {
    while (modemSerial.available()) {
      response += static_cast<char>(modemSerial.read());
    }

    delay(1);
  }

  return response;
}

void powerOnModem()
{
  pinMode(BOARD_LED, OUTPUT);
  digitalWrite(BOARD_LED, LOW);

  pinMode(MODEM_RESET, OUTPUT);
  digitalWrite(MODEM_RESET, HIGH);

  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(100);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(1000);
  digitalWrite(MODEM_PWRKEY, LOW);

  Serial.println("Démarrage du modem A7670...");
  delay(3000);
}

void configureMultiGNSS()
{
  Serial.println("Activation du mode Multi-Constellations...");

  sendATCommand("AT+CGNSSPWR=0", 1000);
  delay(500);
  sendATCommand("AT+CGNSSMODE=15", 1000);
  sendATCommand("AT+CGNSSTST=0", 1000);
  sendATCommand("AT+CGNSSPWR=1", 2000);

  delay(1000);
  Serial.println("GNSS prêt !");
}

String detectAutoAPN()
{
  Serial.println("[AUTO-APN] Lecture IMSI...");

  String response = sendATCommand("AT+CIMI", 2000);

  char mccmnc[6] = {0};
  int idx = 0;

  for (size_t i = 0; i < response.length() && idx < 5; ++i) {
    if (isDigit(response[i])) {
      mccmnc[idx++] = response[i];
    }
  }

  if (idx == 5) {
    Serial.print("[AUTO-APN] Code MCC/MNC : ");
    Serial.println(mccmnc);

    for (size_t i = 0; i < apnDatabaseSize; ++i) {
      if (strcmp(mccmnc, apnDatabase[i].mccmnc) == 0) {
        Serial.print("[AUTO-APN] APN trouvé : ");
        Serial.println(apnDatabase[i].apn);
        return String(apnDatabase[i].apn);
      }
    }
  }

  Serial.print("[AUTO-APN] APN par défaut : ");
  Serial.println(DEFAULT_APN);
  return String(DEFAULT_APN);
}

int getCellularSignalQuality()
{
  String res = sendATCommand("AT+CSQ", 500);
  const int idx = res.indexOf("+CSQ:");

  if (idx >= 0) {
    const int csq = res.substring(idx + 6).toInt();

    if (csq != 99) {
      return constrain((csq * 100) / 31, 0, 100);
    }
  }

  return 0;
}

bool readGNSS(GnssData& data)
{
  String response = sendATCommand("AT+CGNSSINFO", 1000);

  const int infoIndex = response.indexOf("+CGNSSINFO:");
  if (infoIndex < 0) {
    return false;
  }

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

  if (tokenCount < 9) {
    return false;
  }

  const int fixMode =
      (tokens[0] && *tokens[0]) ? atoi(tokens[0]) : 0;
  const int gpsSats =
      (tokens[1] && *tokens[1]) ? atoi(tokens[1]) : 0;
  const int glonassSats =
      (tokens[2] && *tokens[2]) ? atoi(tokens[2]) : 0;
  const int beidouSats =
      (tokens[3] && *tokens[3]) ? atoi(tokens[3]) : 0;
  const int galileoSats =
      (tokens[4] && *tokens[4]) ? atoi(tokens[4]) : 0;

  const int totalSats =
      gpsSats + glonassSats + beidouSats + galileoSats;

  float latitude =
      (tokens[5] && *tokens[5]) ? atof(tokens[5]) : 0.0f;
  const char* latDir = tokens[6];

  float longitude =
      (tokens[7] && *tokens[7]) ? atof(tokens[7]) : 0.0f;
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

  data.hasFix =
      (fixMode >= 2) &&
      (latitude != 0.0f) &&
      (longitude != 0.0f);

  const float rawAltitude =
      (tokenCount > 11 && tokens[11] && *tokens[11])
          ? atof(tokens[11])
          : 0.0f;

  const float rawSpeedKmh =
      (tokenCount > 12 && tokens[12] && *tokens[12])
          ? atof(tokens[12]) * 1.852f
          : 0.0f;

  if (!data.hasFix) {
    data.filteredAltitude = rawAltitude;
    data.filteredSpeedKmh = rawSpeedKmh;
  } else {
    data.filteredAltitude =
        data.filteredAltitude +
        ALTITUDE_FILTER_ALPHA * (rawAltitude - data.filteredAltitude);

    data.filteredSpeedKmh =
        data.filteredSpeedKmh +
        SPEED_FILTER_ALPHA * (rawSpeedKmh - data.filteredSpeedKmh);
  }

  if (data.filteredSpeedKmh < SPEED_ZERO_THRESHOLD_KMH) {
    data.filteredSpeedKmh = 0.0f;
  }

  data.rawAltitude = rawAltitude;
  data.rawSpeedKmh = rawSpeedKmh;
  data.altitude = data.filteredAltitude;
  data.speedKmh = data.filteredSpeedKmh;
  data.satellitesUpdatedAt = millis();

  return true;
}

bool sendToTrackserverWiFi(
    float lat,
    float lon,
    float altitude,
    float speedKmh,
    uint32_t& bytesSent)
{
  bytesSent = 0;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Non connecté : envoi annulé.");
    return false;
  }

  TrackserverConfig ts;
  if (!getTrackserverConfig(ts)) {
    Serial.println("[WIFI] Configuration Trackserver indisponible.");
    return false;
  }

  char path[TRACKSERVER_PATH_MAX_LEN + 96];

  snprintf(
      path,
      sizeof(path),
      "%s%slat=%.6f&lon=%.6f&altitude=%.1f&speed=%.1f",
      ts.path,
      strchr(ts.path, '?') ? "&" : "?",
      lat,
      lon,
      altitude,
      speedKmh);

  WiFiClient client;

  if (!client.connect(ts.host, 80)) {
    Serial.println("[WIFI] Connexion Trackserver impossible.");
    return false;
  }

  client.print("GET ");
  client.print(path);
  client.print(" HTTP/1.1\r\nHost: ");
  client.print(ts.host);
  client.print("\r\nConnection: close\r\n\r\n");

  bytesSent = (uint32_t)(
      strlen("GET ") +
      strlen(path) +
      strlen(" HTTP/1.1\r\nHost: ") +
      strlen(ts.host) +
      strlen("\r\nConnection: close\r\n\r\n"));

  const uint32_t start = millis();

  while (!client.available() && millis() - start < 3000) {
    delay(1);
  }

  if (!client.available()) {
    Serial.println("[WIFI] Timeout réponse HTTP.");
    client.stop();
    return false;
  }

  String statusLine = client.readStringUntil('\n');
  statusLine.trim();

  const bool success =
      statusLine.startsWith("HTTP/1.1 200") ||
      statusLine.startsWith("HTTP/1.0 200");

  if (success) {
    Serial.println(">> Success (Wi-Fi)");
  } else {
    Serial.print("[WIFI] Réponse HTTP : ");
    Serial.println(statusLine);
  }

  client.stop();
  return success;
}

bool sendToTrackserverCellular(
    float lat,
    float lon,
    float altitude,
    float speedKmh,
    uint32_t& bytesSent)
{
  bytesSent = 0;

  // Session HTTP persistante du A7670.
  // VALIDÉE PAR TEST 4G V4 :
  // HTTPINIT -> HTTPPARA URL -> HTTPACTION=0
  // AT+HTTPPARA="CID",1 est volontairement supprimé.
  static bool httpReady = false;

  if (!httpReady) {
    String initRes = sendATCommand("AT+HTTPINIT", 1000);

    if (initRes.indexOf("OK") < 0) {
      Serial.println("[4G] HTTPINIT impossible.");
      httpReady = false;
      return false;
    }

    httpReady = true;
    Serial.println("[4G] Session HTTP prête.");
  }

  TrackserverConfig ts;

  if (!getTrackserverConfig(ts)) {
    Serial.println("[4G] Configuration Trackserver indisponible.");
    return false;
  }

  char url[TRACKSERVER_URL_MAX_LEN + 96];

  snprintf(
      url,
      sizeof(url),
      "%s%slat=%.6f&lon=%.6f&altitude=%.1f&speed=%.1f",
      ts.url,
      strchr(ts.url, '?') ? "&" : "?",
      lat,
      lon,
      altitude,
      speedKmh);

  char cmdUrl[TRACKSERVER_URL_MAX_LEN + 120];

  snprintf(
      cmdUrl,
      sizeof(cmdUrl),
      "AT+HTTPPARA=\"URL\",\"%s\"",
      url);

  String urlRes = sendATCommand(cmdUrl, 1000);

  if (urlRes.indexOf("OK") < 0) {
    Serial.println("[4G] HTTPPARA URL impossible - reinitialisation au prochain envoi.");
    sendATCommand("AT+HTTPTERM", 500);
    httpReady = false;
    return false;
  }

  bytesSent = (uint32_t)(
      strlen("GET ") +
      strlen(url) +
      strlen(" HTTP/1.1\r\nHost: ") +
      strlen(ts.host) +
      strlen("\r\nConnection: close\r\n\r\n"));

  String actionRes =
      sendATCommand("AT+HTTPACTION=0", 8000);

  const bool success =
      actionRes.indexOf("+HTTPACTION: 0,200") >= 0;

  if (success) {
    Serial.println(">> Success (4G)");
  } else {
    Serial.print("[!] Erreur HTTP 4G: ");
    Serial.println(actionRes);

    // Une erreur ferme la session pour que le prochain envoi
    // reconstruise proprement HTTPINIT.
    sendATCommand("AT+HTTPTERM", 500);
    httpReady = false;
  }

  return success;
}

bool connectCellularNetwork()
{
  Serial.println("[4G] Activation connexion data...");

  String detectedAPN = detectAutoAPN();

  char apnCmd[96];

  snprintf(
      apnCmd,
      sizeof(apnCmd),
      "AT+CGDCONT=1,\"IP\",\"%s\"",
      detectedAPN.c_str());

  const String apnRes = sendATCommand(apnCmd, 1500);

  if (apnRes.indexOf("OK") < 0) {
    Serial.println("[4G] Configuration APN impossible.");
    return false;
  }

  const String actRes =
      sendATCommand("AT+CGACT=1,1", 5000);

  if (actRes.indexOf("OK") < 0 &&
      actRes.indexOf("+CGACT: 1,1") < 0) {
    Serial.println("[4G] PDP indisponible (SIM/reseau/data). ");
    return false;
  }

  Serial.println("[4G] PDP actif.");
  return true;
}

bool checkCellularInternet()
{
  TrackserverConfig ts;

  if (!getTrackserverConfig(ts)) {
    return false;
  }

  char cmd[128];

  snprintf(
      cmd,
      sizeof(cmd),
      "AT+CDNSGIP=\"%s\"",
      ts.host);

  const String res =
      sendATCommand(cmd, 6000);

  return res.indexOf(ts.host) >= 0 &&
         res.indexOf("OK") >= 0;
}
