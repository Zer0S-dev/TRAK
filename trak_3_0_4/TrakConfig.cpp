#include <Arduino.h>
#include <Preferences.h>
#include "TrakConfig.h"

namespace {
constexpr char PREF_NS[] = "trak_cfg";
constexpr char KEY_URL[] = "server_url";
constexpr char KEY_USER_PHONE[] = "user_phone";
constexpr char KEY_TRAK_PHONE[] = "trak_phone";
constexpr char KEY_API[] = "api_key";
constexpr char KEY_PROVISIONED[] = "provisioned";
constexpr char DEFAULT_WEB_APP_URL[] = "https://surlereservoir.fr/trak/";
constexpr size_t API_KEY_BYTES = 24;
Preferences prefs;
String webUrl;
String userPhone;
String storedTrakPhone;
String apiKey;
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

String generateApiKey() {
  const uint64_t mac = ESP.getEfuseMac();
  uint8_t raw[API_KEY_BYTES];
  uint32_t mix = (uint32_t)mac ^ (uint32_t)(mac >> 32) ^ micros();
  for (size_t i = 0; i < API_KEY_BYTES; ++i) {
    mix ^= esp_random();
    mix = (mix << 13) | (mix >> 19);
    mix ^= (uint32_t)(mac >> ((i % 6) * 8));
    raw[i] = (uint8_t)(mix ^ (esp_random() & 0xFF));
  }
  static const char hex[] = "0123456789abcdef";
  String out;
  out.reserve(API_KEY_BYTES * 2);
  for (uint8_t b : raw) { out += hex[b >> 4]; out += hex[b & 0x0F]; }
  return out;
}

void saveConfig() {
  prefs.putString(KEY_URL, webUrl);
  prefs.putString(KEY_USER_PHONE, userPhone);
  prefs.putString(KEY_TRAK_PHONE, storedTrakPhone);
  prefs.putString(KEY_API, apiKey);
  prefs.putBool(KEY_PROVISIONED, provisioned);
}

void printConfig() {
  Serial.println("[CONFIG] TRAK configuration:");
  Serial.print("[CONFIG] URL_WEB_APP = "); Serial.println(webUrl);
  Serial.print("[CONFIG] USER_PHONE  = "); Serial.println(userPhone.length() ? userPhone : "EMPTY");
  Serial.print("[CONFIG] TRAK_PHONE  = "); Serial.println(storedTrakPhone.length() ? storedTrakPhone : "EMPTY");
  Serial.print("[CONFIG] PROVISIONED = "); Serial.println(provisioned ? "YES" : "NO");
  Serial.println("[CONFIG] API_KEY     = ******** (SHOWKEY pour l'afficher)");
  Serial.println("[CONFIG] NVS: trak_cfg | Wi-Fi: trak_wifi (independant)");
  Serial.println("[CONFIG] Commandes: SETURL <url> | SHOWCONFIG | SHOWKEY | RESETCONFIG");
}

void resetConfig() {
  trakConfigResetProvisioning();
  Serial.println("[CONFIG] Provisioning efface; les profils Wi-Fi sont conserves.");
  Serial.print("[CONFIG] API_KEY apres reset = "); Serial.println(apiKey.length() ? apiKey : "EMPTY");
}
}

void trakConfigBegin() {
  if (ready) return;
  prefs.begin(PREF_NS, false);
  webUrl = normalizeUrl(prefs.getString(KEY_URL, ""));
  userPhone = normalizePhone(prefs.getString(KEY_USER_PHONE, ""));
  storedTrakPhone = normalizePhone(prefs.getString(KEY_TRAK_PHONE, ""));
  apiKey = prefs.getString(KEY_API, "");
  provisioned = prefs.getBool(KEY_PROVISIONED, false);

  if (webUrl.length() == 0) {
    webUrl = DEFAULT_WEB_APP_URL;
    prefs.putString(KEY_URL, webUrl);
    Serial.println("[CONFIG] Premiere utilisation: URL usine enregistree en NVS.");
  }
  if (apiKey.length() < 32) {
    apiKey = generateApiKey();
    prefs.putString(KEY_API, apiKey);
    Serial.println("[CONFIG] Premiere utilisation: API key generee et stockee en NVS.");
    Serial.print("[CONFIG] API_KEY a enregistrer cote serveur = "); Serial.println(apiKey);
  }
  ready = true;
  printConfig();
}

void trakConfigTask() {
  if (!ready || !Serial.available()) return;
  String command = Serial.readStringUntil('\n');
  command.trim();
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
  } else if (command == "SHOWKEY") {
    Serial.print("[CONFIG] API_KEY = "); Serial.println(apiKey);
  } else if (command == "RESETCONFIG") {
    resetConfig();
    printConfig();
  }
}

String trakWebAppUrl() { return webUrl; }
String trakApiKey() { return apiKey; }
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
  return true;
}

bool trakConfigSetTrakPhone(const String& phone) {
  storedTrakPhone = normalizePhone(phone);
  prefs.putString(KEY_TRAK_PHONE, storedTrakPhone);
  return true;
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
  apiKey = "";
  provisioned = false;
  prefs.remove(KEY_URL);
  prefs.remove(KEY_USER_PHONE);
  prefs.remove(KEY_TRAK_PHONE);
  prefs.remove(KEY_API);
  prefs.remove(KEY_PROVISIONED);
}
