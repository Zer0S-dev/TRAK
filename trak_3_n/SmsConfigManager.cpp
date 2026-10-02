#include <Arduino.h>
#include <Preferences.h>
#include <HardwareSerial.h>
#include "Config.h"
#include "SmsConfigManager.h"

extern HardwareSerial modem;
extern volatile bool modemReady;
extern String at(const String& command, uint32_t timeoutMs);
extern void devLog(const String& message);

namespace {
Preferences prefs;
constexpr char PREF_NS[] = "trak_cfg";
constexpr uint32_t SMS_POLL_MS = 5000;
constexpr size_t API_KEY_LEN = 16;
constexpr size_t MAX_TRAK_ID_LEN = 5;
constexpr size_t NONCE_LEN = 16;
constexpr size_t MAX_trackserver_url_LEN = 160;
constexpr size_t MAX_DASHBOARD_URL_LEN = 160;
uint32_t lastPoll = 0;
bool ready = false;

String normalizePhone(const String& value) {
  String out;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c >= '0' && c <= '9') out += c;
  }
  if (out.length() > 10) out = out.substring(out.length() - 10);
  return out;
}

bool phonesMatch(const String& a, const String& b) {
  const String na = normalizePhone(a), nb = normalizePhone(b);
  return na.length() >= 8 && nb.length() >= 8 && na == nb;
}

bool validTrakId(const String& value) {
  if (value.isEmpty() || value.length() > MAX_TRAK_ID_LEN) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return true;
}

bool validConfigId(const String& value) {
  if (value.isEmpty() || value.length() > 64) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return true;
}

bool validAlphaNum(const String& value, size_t expectedLength) {
  if (value.length() != expectedLength) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9'))) return false;
  }
  return true;
}

bool validNonce(const String& value) {
  return validAlphaNum(value, NONCE_LEN);
}

bool validTrackserverUrl(const String& value) {
  if (value.isEmpty() || value.length() > MAX_trackserver_url_LEN) return false;
  if (!value.startsWith("https://")) return false;
  if (value.indexOf('|') >= 0 || value.indexOf('\r') >= 0 || value.indexOf('\n') >= 0) return false;
  return true;
}

bool splitPipe(const String& line, String* fields, size_t maxFields, size_t& count) {
  count = 0;
  int cursor = 0;
  while (cursor <= (int)line.length() && count < maxFields) {
    const int sep = line.indexOf('|', cursor);
    if (sep < 0) {
      fields[count++] = line.substring(cursor);
      return true;
    }
    fields[count++] = line.substring(cursor, sep);
    cursor = sep + 1;
  }
  return count == maxFields && line.indexOf('|', cursor) < 0;
}

String smsSenderFromHeader(const String& header) {
  int quote = header.indexOf('"');
  if (quote < 0) return "";
  quote = header.indexOf('"', quote + 1);
  if (quote < 0) return "";
  quote = header.indexOf('"', quote + 1);
  if (quote < 0) return "";
  const int end = header.indexOf('"', quote + 1);
  if (end < 0) return "";
  return header.substring(quote + 1, end);
}

int smsIndexFromHeader(const String& header) {
  const int colon = header.indexOf(':');
  if (colon < 0) return -1;
  int cursor = colon + 1;
  while (cursor < (int)header.length() && header[cursor] == ' ') ++cursor;
  int end = cursor;
  while (end < (int)header.length() && header[end] >= '0' && header[end] <= '9') ++end;
  if (end == cursor) return -1;
  return header.substring(cursor, end).toInt();
}

void sendSms(const String& phone, const String& text) {
  if (!modemReady || phone.isEmpty()) return;
  while (modem.available()) modem.read();
  modem.print("AT+CMGS=\"");
  modem.print(phone);
  modem.print("\"\r\n");

  const uint32_t promptStart = millis();
  bool prompt = false;
  while (millis() - promptStart < 5000) {
    while (modem.available()) {
      if (static_cast<char>(modem.read()) == '>') {
        prompt = true;
        break;
      }
    }
    if (prompt) break;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  if (!prompt) {
    devLog("SMS | notification ERROR | no CMGS prompt");
    return;
  }

  modem.print(text);
  modem.write(0x1A);

  String response;
  const uint32_t start = millis();
  while (millis() - start < 15000) {
    while (modem.available()) response += static_cast<char>(modem.read());
    if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0 ||
        response.indexOf("+CMS ERROR") >= 0) break;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  if (response.indexOf("OK") >= 0) devLog("SMS | notification sent");
  else devLog("SMS | notification ERROR");
}

void deleteSms(int index) {
  if (index >= 0) at(String("AT+CMGD=") + String(index), 3000);
}

void clearPending() {
  prefs.remove("p_cfg");
  prefs.remove("p_sender");
  prefs.remove("p_trak_id");
  prefs.remove("p_trak_phone");
  prefs.remove("p_user_phone");
  prefs.remove("p_api_key");
  prefs.remove("p_nonce");
  prefs.remove("p_url1");
  prefs.remove("p_url2");
  prefs.remove("p_dash1");
  prefs.remove("p_dash2");
}

bool pendingMatches(const String& configId, const String& sender) {
  return prefs.getString("p_cfg", "") == configId &&
         phonesMatch(prefs.getString("p_sender", ""), sender);
}

bool tryCommitPending() {
  const String configId = prefs.getString("p_cfg", "");
  const String sender = prefs.getString("p_sender", "");
  const String trakId = prefs.getString("p_trak_id", "");
  const String trakPhone = prefs.getString("p_trak_phone", "");
  const String userPhone = prefs.getString("p_user_phone", "");
  const String apiKey = prefs.getString("p_api_key", "");
  const String nonce = prefs.getString("p_nonce", "");
  const String url1 = prefs.getString("p_url1", "");
  const String url2 = prefs.getString("p_url2", "");

  if (!validConfigId(configId) || sender.isEmpty() ||
      !validTrakId(trakId) || trakPhone.isEmpty() || userPhone.isEmpty() ||
      !phonesMatch(sender, userPhone) ||
      !validAlphaNum(apiKey, API_KEY_LEN) || !validNonce(nonce) ||
      url1.isEmpty()) {
    return false;
  }

  String trackserverUrl = url1 + url2;
  if (!validTrackserverUrl(trackserverUrl)) return false;

  const String usedConfig = prefs.getString("config_id", "");
  const String usedNonce = prefs.getString("nonce", "");
  if ((usedConfig.length() && usedConfig == configId) ||
      (usedNonce.length() && usedNonce == nonce)) {
    Serial.println("[SMS] Rejeu detecte: CONFIG_ID ou NONCE deja utilise.");
    sendSms(sender, "TRAK: configuration deja utilisee.");
    clearPending();
    return true;
  }

  prefs.putString("trak_id", trakId);
  prefs.putString("trak_phone", trakPhone);
  prefs.putString("user_phone", userPhone);
  prefs.putString("trackserver_url", trackserverUrl);
  const String dashboardUrl = prefs.getString("p_dash1", "") + prefs.getString("p_dash2", "");
  if (!dashboardUrl.isEmpty() && validTrackserverUrl(dashboardUrl) && dashboardUrl.length() <= MAX_DASHBOARD_URL_LEN) {
    if (!validTrackserverUrl(dashboardUrl)) {
      Serial.println("[SMS] URL Dashboard finale invalide.");
      prefs.remove("p_dash1");
      prefs.remove("p_dash2");
      return true;
    }
    prefs.putString("dashboard_url", dashboardUrl);
  }
  prefs.putString("api_key", apiKey);
  prefs.putString("config_id", configId);
  prefs.putString("nonce", nonce);
  clearPending();

  Serial.printf("[SMS] Configuration acceptee | CONFIG_ID=%s | trackserver_url=%s\n",
                configId.c_str(), trackserverUrl.c_str());
  devLog(String("SMS | config OK | config_id=") + configId);
  sendSms(userPhone, String("TRAK ") + trakId + ": configuration recue et valide. CONFIG_ID=" + configId);
  return true;
}

bool processConfig1(const String& sender, const String& body) {
  String fields[6];
  size_t count = 0;
  if (!splitPipe(body, fields, 6, count) || count != 6 ||
      fields[0] != "TRAKCFG1" || fields[1] != "1") return false;

  if (!validConfigId(fields[2]) || !validTrakId(fields[3]) ||
      fields[4].isEmpty() || fields[5].isEmpty() ||
      !phonesMatch(sender, fields[5])) {
    Serial.println("[SMS] TRAKCFG1 invalide ou USER_PHONE different de l'expediteur.");
    return true;
  }

  const String usedConfig = prefs.getString("config_id", "");
  if (usedConfig.length() && usedConfig == fields[2]) {
    Serial.println("[SMS] TRAKCFG1 rejete: CONFIG_ID deja utilise.");
    return true;
  }

  clearPending();
  prefs.putString("p_cfg", fields[2]);
  prefs.putString("p_sender", sender);
  prefs.putString("p_trak_id", fields[3]);
  prefs.putString("p_trak_phone", fields[4]);
  prefs.putString("p_user_phone", fields[5]);

  Serial.printf("[SMS] TRAKCFG1 recu | CONFIG_ID=%s | TRAK_ID=%s\n",
                fields[2].c_str(), fields[3].c_str());
  devLog(String("SMS | CFG1 | config_id=") + fields[2]);
  tryCommitPending();
  return true;
}

bool processConfig3(const String& sender, const String& body) {
  String fields[5];
  size_t count = 0;
  if (!splitPipe(body, fields, 5, count) || count != 5 ||
      fields[0] != "TRAKCFG3" || fields[1] != "1") return false;

  if (!validConfigId(fields[2]) || !validAlphaNum(fields[3], API_KEY_LEN) ||
      !validNonce(fields[4]) || !pendingMatches(fields[2], sender)) {
    Serial.println("[SMS] TRAKCFG3 invalide ou configuration correspondante absente.");
    return true;
  }

  prefs.putString("p_api_key", fields[3]);
  prefs.putString("p_nonce", fields[4]);
  Serial.printf("[SMS] TRAKCFG3 recu | CONFIG_ID=%s | API_KEY=%u | NONCE=%u\n", fields[2].c_str(), (unsigned)fields[3].length(), (unsigned)fields[4].length());
  devLog(String("SMS | CFG3 | config_id=") + fields[2]);
  tryCommitPending();
  return true;
}

bool processConfig2(const String& sender, const String& body) {
  String fields[5];
  size_t count = 0;
  if (!splitPipe(body, fields, 5, count) || count != 5 ||
      fields[0] != "TRAKCFG2" || (fields[1] != "1" && fields[1] != "2") ||
      (fields[4] != "0" && fields[4] != "1")) {
    return false;
  }

  const int part = fields[1].toInt();
  const bool isFinal = fields[4] == "1";
  if (!validConfigId(fields[2]) || fields[3].isEmpty() ||
      !pendingMatches(fields[2], sender)) {
    Serial.println("[SMS] TRAKCFG2 invalide ou configuration correspondante absente.");
    return true;
  }

  if (part == 1) prefs.putString("p_url1", fields[3]);
  else prefs.putString("p_url2", fields[3]);

  const size_t combinedLength = prefs.getString("p_url1", "").length() +
                                prefs.getString("p_url2", "").length();
  if (combinedLength > MAX_trackserver_url_LEN) {
    Serial.println("[SMS] trackserver_url trop longue.");
    clearPending();
    return true;
  }

  Serial.printf("[SMS] TRAKCFG2 partie %d recu | FIN=%d | CONFIG_ID=%s\n",
                part, isFinal ? 1 : 0, fields[2].c_str());
  devLog(String("SMS | CFG2 part=") + String(part) + " | fin=" + String(isFinal ? 1 : 0) + " | config_id=" + fields[2]);

  if (isFinal) {
    if (part == 2 && prefs.getString("p_url1", "").isEmpty()) {
      Serial.println("[SMS] TRAKCFG2 finale refusee: partie 1 absente.");
      return true;
    }
    tryCommitPending();
  }
  return true;
}


bool dashboardConfigMatches(const String& configId, const String& sender) {
  if (pendingMatches(configId, sender)) return true;
  return prefs.getString("config_id", "") == configId &&
         phonesMatch(prefs.getString("user_phone", ""), sender);
}

bool processConfig4(const String& sender, const String& body) {
  String fields[5];
  size_t count = 0;
  if (!splitPipe(body, fields, 5, count) || count != 5 ||
      fields[0] != "TRAKCFG4" || (fields[1] != "1" && fields[1] != "2") ||
      (fields[4] != "0" && fields[4] != "1")) return false;

  const int part = fields[1].toInt();
  const bool isFinal = fields[4] == "1";
  if (!validConfigId(fields[2]) || fields[3].isEmpty() ||
      fields[3].length() > MAX_DASHBOARD_URL_LEN ||
      fields[3].indexOf('|') >= 0 || fields[3].indexOf('\r') >= 0 || fields[3].indexOf('\n') >= 0 ||
      !dashboardConfigMatches(fields[2], sender)) {
    Serial.println("[SMS] TRAKCFG4 URL Dashboard invalide ou configuration absente.");
    return true;
  }

  if (part == 1) prefs.putString("p_dash1", fields[3]);
  else prefs.putString("p_dash2", fields[3]);

  const String dashboardUrl = prefs.getString("p_dash1", "") + prefs.getString("p_dash2", "");
  if (dashboardUrl.length() > MAX_DASHBOARD_URL_LEN) {
    Serial.println("[SMS] URL Dashboard trop longue.");
    prefs.remove("p_dash1");
    prefs.remove("p_dash2");
    return true;
  }

  Serial.printf("[SMS] TRAKCFG4 partie %d recu | FIN=%d | CONFIG_ID=%s\n",
                part, isFinal ? 1 : 0, fields[2].c_str());
  devLog(String("SMS | CFG4 part=") + String(part) + " | fin=" + String(isFinal ? 1 : 0) + " | config_id=" + fields[2]);

  if (isFinal) {
    if (part == 2 && prefs.getString("p_dash1", "").isEmpty()) {
      Serial.println("[SMS] TRAKCFG4 finale refusee: partie 1 absente.");
      return true;
    }
    if (!validTrackserverUrl(dashboardUrl)) {
      Serial.println("[SMS] URL Dashboard finale invalide.");
      prefs.remove("p_dash1");
      prefs.remove("p_dash2");
      return true;
    }
    prefs.putString("dashboard_url", dashboardUrl);
    prefs.remove("p_dash1");
    prefs.remove("p_dash2");
    Serial.printf("[SMS] URL Dashboard acceptee : %s\n", dashboardUrl.c_str());
    devLog(String("SMS | dashboard_url updated | config_id=") + fields[2]);
  }
  return true;
}

bool processSms(int index, const String& sender, const String& body) {
  String cleanBody = body;
  cleanBody.trim();
  if (cleanBody.startsWith("TRAKCFG1|")) processConfig1(sender, cleanBody);
  else if (cleanBody.startsWith("TRAKCFG2|")) processConfig2(sender, cleanBody);
  else if (cleanBody.startsWith("TRAKCFG3|")) processConfig3(sender, cleanBody);
  else if (cleanBody.startsWith("TRAKCFG4|")) processConfig4(sender, cleanBody);
  else return false;

  deleteSms(index);
  return true;
}

void pollSms() {
  const String response = at("AT+CMGL=\"REC UNREAD\"", 5000);
  if (response.indexOf("+CMGL:") < 0) return;

  int cursor = 0;
  while (cursor < (int)response.length()) {
    const int headerStart = response.indexOf("+CMGL:", cursor);
    if (headerStart < 0) break;
    const int headerEnd = response.indexOf('\n', headerStart);
    if (headerEnd < 0) break;

    String header = response.substring(headerStart, headerEnd);
    header.trim();
    const int bodyStart = headerEnd + 1;
    const int bodyEnd = response.indexOf('\n', bodyStart);
    String body = bodyEnd < 0 ? response.substring(bodyStart) : response.substring(bodyStart, bodyEnd);
    body.trim();

    processSms(smsIndexFromHeader(header), smsSenderFromHeader(header), body);
    cursor = bodyEnd < 0 ? response.length() : bodyEnd + 1;
  }
}
}

void smsConfigBegin() {
  prefs.begin(PREF_NS, false);
  if (!modemReady) return;
  at("AT+CMGF=1", 3000);
  at("AT+CSCS=\"GSM\"", 3000);
  at("AT+CNMI=2,1,0,0,0", 3000);
  ready = true;
  lastPoll = millis() - SMS_POLL_MS;
  Serial.println("[SMS] Configuration SMS active.");
  devLog("SMS | configuration listener active");
}

void smsConfigTick() {
  if (!ready || !modemReady) return;
  const uint32_t now = millis();
  if (now - lastPoll < SMS_POLL_MS) return;
  lastPoll = now;
  pollSms();
}

bool smsConfigIsConfigured() {
  return prefs.getString("api_key", "").length() == API_KEY_LEN &&
         prefs.getString("trackserver_url", "").length() > 0 &&
         prefs.getString("trak_id", "").length() > 0;
}

String smsConfigUserPhone() { return prefs.getString("user_phone", ""); }
String smsConfigTrackserverUrl() { return prefs.getString("trackserver_url", ""); }
String smsConfigApiKey() { return prefs.getString("api_key", ""); }
String smsConfigTrakId() { return prefs.getString("trak_id", ""); }
String smsConfigDashboardUrl() { return prefs.getString("dashboard_url", ""); }
