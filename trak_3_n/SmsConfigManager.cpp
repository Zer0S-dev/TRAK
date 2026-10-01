#include <Arduino.h>
#include <Preferences.h>
#include <HardwareSerial.h>
#include <mbedtls/md.h>
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
bool hexString(const String& value) {
  if (value.isEmpty()) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
  }
  return true;
}
String hmacSha256Hex(const String& secret, const String& message) {
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) return "";
  unsigned char digest[32] = {};
  if (mbedtls_md_hmac(info, reinterpret_cast<const unsigned char*>(secret.c_str()), secret.length(),
                      reinterpret_cast<const unsigned char*>(message.c_str()), message.length(), digest) != 0) return "";
  String out; out.reserve(64);
  char byteHex[3];
  for (uint8_t i = 0; i < sizeof(digest); ++i) { snprintf(byteHex, sizeof(byteHex), "%02x", digest[i]); out += byteHex; }
  return out;
}
bool splitPipe(const String& line, String* fields, size_t maxFields, size_t& count) {
  count = 0; int cursor = 0;
  while (cursor <= (int)line.length() && count < maxFields) {
    const int sep = line.indexOf('|', cursor);
    if (sep < 0) { fields[count++] = line.substring(cursor); return true; }
    fields[count++] = line.substring(cursor, sep); cursor = sep + 1;
  }
  return count == maxFields && line.indexOf('|', cursor) < 0;
}
String smsSenderFromHeader(const String& header) {
  int quote = header.indexOf('"'); if (quote < 0) return "";
  quote = header.indexOf('"', quote + 1); if (quote < 0) return "";
  quote = header.indexOf('"', quote + 1); if (quote < 0) return "";
  const int end = header.indexOf('"', quote + 1); if (end < 0) return "";
  return header.substring(quote + 1, end);
}
int smsIndexFromHeader(const String& header) {
  const int colon = header.indexOf(':'); if (colon < 0) return -1;
  int cursor = colon + 1; while (cursor < (int)header.length() && header[cursor] == ' ') ++cursor;
  int end = cursor; while (end < (int)header.length() && header[end] >= '0' && header[end] <= '9') ++end;
  if (end == cursor) return -1;
  return header.substring(cursor, end).toInt();
}
void sendSms(const String& phone, const String& text) {
  if (!modemReady || phone.isEmpty()) return;
  Serial.printf("[SMS] Envoi notification -> %s\n", phone.c_str());
  while (modem.available()) modem.read();
  modem.print("AT+CMGS=\""); modem.print(phone); modem.print("\"\r\n");
  const uint32_t promptStart = millis(); bool prompt = false;
  while (millis() - promptStart < 5000) {
    while (modem.available()) { if (static_cast<char>(modem.read()) == '>') { prompt = true; break; } }
    if (prompt) break;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  if (!prompt) { Serial.println("[SMS] Erreur: pas de prompt CMGS."); devLog("SMS | notification ERROR | no CMGS prompt"); return; }
  modem.print(text); modem.write(0x1A);
  String response; const uint32_t start = millis();
  while (millis() - start < 15000) {
    while (modem.available()) response += static_cast<char>(modem.read());
    if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0 || response.indexOf("+CMS ERROR") >= 0) break;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  if (response.indexOf("OK") >= 0) { Serial.println("[SMS] Notification envoyee."); devLog("SMS | notification sent"); }
  else { Serial.println("[SMS] Erreur envoi notification."); devLog("SMS | notification ERROR"); }
}
void deleteSms(int index) { if (index >= 0) at(String("AT+CMGD=") + String(index), 3000); }

void processSms(int index, const String& sender, const String& body) {
  String fields[10]; size_t count = 0; String cleanBody = body; cleanBody.trim();
  if (!splitPipe(cleanBody, fields, 10, count) || count != 10 || fields[0] != "TRAKCFG" || fields[1] != "1") {
    Serial.println("[SMS] Message ignore: format TRAKCFG invalide."); deleteSms(index); return;
  }
  const String expectedTrakId = trackerSerialNumber();
  if (fields[2] != expectedTrakId) {
    Serial.printf("[SMS] TRAK ID invalide: %s\n", fields[2].c_str());
    sendSms(sender, String("TRAK: erreur configuration - ID TRAK invalide (attendu ") + expectedTrakId + ")");
    deleteSms(index); return;
  }
  if (!phonesMatch(sender, fields[4])) {
    Serial.println("[SMS] Expediteur different du USER_PHONE du message.");
    sendSms(sender, "TRAK: erreur configuration - expediteur non autorise.");
    deleteSms(index); return;
  }
  if (!hexString(fields[7]) || !hexString(fields[8]) || fields[8].length() != 16 || fields[9].length() != 64 || !hexString(fields[9])) {
    Serial.println("[SMS] Parametres de securite invalides.");
    sendSms(sender, "TRAK: erreur configuration - signature ou nonce invalide.");
    deleteSms(index); return;
  }
  const String savedNonce = prefs.getString("nonce", "");
  if (savedNonce.length() && savedNonce == fields[8]) {
    Serial.println("[SMS] Rejeu detecte: nonce deja utilise.");
    sendSms(sender, "TRAK: erreur configuration - message deja utilise.");
    deleteSms(index); return;
  }
  const String signedPart = fields[0] + "|" + fields[1] + "|" + fields[2] + "|" + fields[3] + "|" + fields[4] + "|" + fields[5] + "|" + fields[6] + "|" + fields[7] + "|" + fields[8];
  const String expectedSignature = hmacSha256Hex(fields[6], signedPart);
  String receivedSignature = fields[9]; receivedSignature.toLowerCase();
  if (expectedSignature.length() != 64 || receivedSignature != expectedSignature) {
    Serial.println("[SMS] HMAC invalide."); devLog("SMS | config ERROR | HMAC invalide");
    sendSms(sender, "TRAK: erreur configuration - signature HMAC invalide."); deleteSms(index); return;
  }
  prefs.putString("trak_id", fields[2]); prefs.putString("trak_phone", fields[3]); prefs.putString("user_phone", fields[4]);
  prefs.putString("api_url", fields[5]); prefs.putString("api_key", fields[6]); prefs.putString("config_id", fields[7]); prefs.putString("nonce", fields[8]);
  Serial.printf("[SMS] Configuration acceptee | ID=%s | URL=%s\n", fields[7].c_str(), fields[5].c_str());
  devLog(String("SMS | config OK | config_id=") + fields[7]);
  sendSms(fields[4], String("TRAK ") + fields[2] + ": configuration recue et valide. CONFIG_ID=" + fields[7]);
  deleteSms(index);
}
void pollSms() {
  const String response = at("AT+CMGL=\"REC UNREAD\"", 5000);
  if (response.indexOf("+CMGL:") < 0) return;
  int cursor = 0;
  while (cursor < (int)response.length()) {
    const int headerStart = response.indexOf("+CMGL:", cursor); if (headerStart < 0) break;
    const int headerEnd = response.indexOf('\n', headerStart); if (headerEnd < 0) break;
    String header = response.substring(headerStart, headerEnd); header.trim();
    const int bodyStart = headerEnd + 1; const int bodyEnd = response.indexOf('\n', bodyStart);
    String body = bodyEnd < 0 ? response.substring(bodyStart) : response.substring(bodyStart, bodyEnd); body.trim();
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
  ready = true; lastPoll = millis() - SMS_POLL_MS;
  Serial.println("[SMS] Configuration SMS active."); devLog("SMS | configuration listener active");
}
void smsConfigTick() {
  if (!ready || !modemReady) return;
  const uint32_t now = millis(); if (now - lastPoll < SMS_POLL_MS) return;
  lastPoll = now; pollSms();
}


bool smsConfigIsConfigured() {
  return prefs.getString("api_key", "").length() > 0 && prefs.getString("api_url", "").length() > 0 && prefs.getString("trak_id", "").length() > 0;
}
String smsConfigUserPhone() { return prefs.getString("user_phone", ""); }
String smsConfigApiUrl() { return prefs.getString("api_url", ""); }
String smsConfigApiKey() { return prefs.getString("api_key", ""); }
String smsConfigTrakId() { return prefs.getString("trak_id", ""); }
