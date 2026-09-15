#include <Arduino.h>
#include <Preferences.h>
#include "TrakConfig.h"

namespace {
constexpr char PREF_NS[] = "trak_cfg";
constexpr char KEY_URL[] = "server_url";
constexpr char KEY_USER_PHONE[] = "user_phone";
constexpr char KEY_TRAK_PHONE[] = "trak_phone";
constexpr char KEY_PROVISIONED[] = "provisioned";
constexpr char KEY_ENCRYPTION[] = "enc_key";
Preferences prefs;
String webUrl;
String userPhone;
String storedTrakPhone;
String encryptionKey;
bool provisioned = false;
bool ready = false;

String normalizeUrl(String url) {
  url.trim();
  if (url.length() == 0) return String();
  if (!url.startsWith("http://") && !url.startsWith("https://")) return String();
  while (url.endsWith("/")) url.remove(url.length() - 1);
  url += "/";
  return url;
}

String normalizePhone(String phone) {
  phone.trim();
  String out;
  out.reserve(phone.length());
  for (size_t i = 0; i < phone.length(); ++i) {
    const char c = phone[i];
    if ((c >= '0' && c <= '9') || (c == '+' && out.length() == 0)) out += c;
  }
  return out;
}

void printConfig() {
  Serial.println("[CONFIG] TRAK configuration:");
  Serial.print("[CONFIG] URL_WEB_APP = "); Serial.println(webUrl.length() ? webUrl : "EMPTY");
  Serial.print("[CONFIG] USER_PHONE  = "); Serial.println(userPhone.length() ? userPhone : "EMPTY");
  Serial.print("[CONFIG] TRAK_PHONE  = "); Serial.println(storedTrakPhone.length() ? storedTrakPhone : "EMPTY");
  Serial.print("[CONFIG] PROVISIONED = "); Serial.println(provisioned ? "YES" : "NO");
  Serial.print("[CONFIG] ENCRYPTION_KEY = "); Serial.println(encryptionKey.length() == 64 ? "SET" : "EMPTY");
  Serial.println("[CONFIG] NVS: trak_cfg | Wi-Fi: trak_wifi (independant)");
  Serial.println("[CONFIG] URL serveur configuree uniquement par le Wizard.");
}

void resetConfig() {
  trakConfigResetProvisioning();
  Serial.println("[CONFIG] Provisioning efface; les profils Wi-Fi sont conserves.");
}
}

void trakConfigBegin() {
  if (ready) return;
  prefs.begin(PREF_NS, false);
  webUrl = normalizeUrl(prefs.getString(KEY_URL, ""));
  userPhone = normalizePhone(prefs.getString(KEY_USER_PHONE, ""));
  storedTrakPhone = normalizePhone(prefs.getString(KEY_TRAK_PHONE, ""));
  encryptionKey = prefs.getString(KEY_ENCRYPTION, "");
  provisioned = prefs.getBool(KEY_PROVISIONED, false);
  ready = true;
  printConfig();
}

void trakConfigHandleCommand(const String& command) {
  if (!ready) return;
  if (command.startsWith("SETURL ")) {
    String candidate = normalizeUrl(command.substring(7));
    if (candidate.length() == 0) {
      Serial.println("[CONFIG] URL invalide. Exemple: SETURL https://serveur.fr/trak/");
      return;
    }
    webUrl = candidate;
    prefs.putString(KEY_URL, webUrl);
    Serial.print("[CONFIG] URL_WEB_APP enregistree: "); Serial.println(webUrl);
  } else if (command == "SHOWCONFIG") {
    printConfig();
  } else if (command == "RESETCONFIG") {
    resetConfig();
    printConfig();
  }
}

void trakConfigTask() {
  if (!ready || !Serial.available()) return;
  String command = Serial.readStringUntil('\n');
  command.trim();
  trakConfigHandleCommand(command);
}

String trakWebAppUrl() { return webUrl; }
String trakUserPhone() { return userPhone; }
String trakPhone() { return storedTrakPhone; }
bool trakConfigProvisioned() { return provisioned; }
bool trakConfigReady() { return ready; }

bool trakConfigSetServerUrl(const String& url) {
  const String normalized = normalizeUrl(url);
  if (normalized.length() == 0) return false;
  webUrl = normalized;
  prefs.putString(KEY_URL, webUrl);
  return true;
}

bool trakConfigSetUserPhone(const String& phone) {
  userPhone = normalizePhone(phone);
  prefs.putString(KEY_USER_PHONE, userPhone);
  return userPhone.length() > 0;
}

bool trakConfigSetTrakPhone(const String& phone) {
  storedTrakPhone = normalizePhone(phone);
  prefs.putString(KEY_TRAK_PHONE, storedTrakPhone);
  return storedTrakPhone.length() > 0;
}

bool trakConfigSetProvisioned(bool value) {
  provisioned = value;
  prefs.putBool(KEY_PROVISIONED, provisioned);
  return true;
}

void trakConfigResetProvisioning() {
  webUrl = "";
  userPhone = "";
  storedTrakPhone = "";
  encryptionKey = "";
  provisioned = false;
  prefs.remove(KEY_URL);
  prefs.remove(KEY_USER_PHONE);
  prefs.remove(KEY_TRAK_PHONE);
  prefs.remove(KEY_PROVISIONED);
  prefs.remove(KEY_ENCRYPTION);
}

String trakEncryptionKey() { return encryptionKey; }
bool trakHasEncryptionKey() { return encryptionKey.length() == 64; }
void setTrakEncryptionKey(const String& key) {
  encryptionKey = key;
  prefs.putString(KEY_ENCRYPTION, encryptionKey);
}
void clearTrakEncryptionKey() {
  encryptionKey = "";
  prefs.remove(KEY_ENCRYPTION);
}

String generateTrakEncryptionKey() {
  uint8_t key[32];
  for (size_t i = 0; i < sizeof(key); ++i) key[i] = static_cast<uint8_t>(esp_random() & 0xFF);
  static const char HEX[] = "0123456789ABCDEF";
  String out;
  out.reserve(64);
  for (uint8_t b : key) {
    out += HEX[(b >> 4) & 0x0F];
    out += HEX[b & 0x0F];
  }
  return out;
}
