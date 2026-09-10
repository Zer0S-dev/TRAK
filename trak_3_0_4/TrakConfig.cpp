#include <Arduino.h>
#include <Preferences.h>
#include "TrakConfig.h"

namespace {
constexpr char PREF_NS[] = "trak_cfg";
constexpr char KEY_URL[] = "web_url";
constexpr char KEY_API[] = "api_key";
constexpr char DEFAULT_WEB_APP_URL[] = "https://surlereservoir.fr/trak/";
constexpr size_t API_KEY_BYTES = 24;
Preferences prefs;
String webUrl;
String apiKey;
bool ready = false;

String normalizeUrl(String url) {
  url.trim();
  if (url.length() == 0) return String();
  if (!url.startsWith("http://") && !url.startsWith("https://")) return String();
  while (url.endsWith("/")) url.remove(url.length() - 1);
  url += "/";
  return url;
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
  prefs.putString(KEY_API, apiKey);
}

void printConfig() {
  Serial.println("[CONFIG] TRAK configuration:");
  Serial.print("[CONFIG] URL_WEB_APP = "); Serial.println(webUrl);
  Serial.print("[CONFIG] API_KEY     = "); Serial.println(apiKey);
  Serial.println("[CONFIG] Commandes: SETURL <url> | SHOWCONFIG | RESETCONFIG");
}

void resetConfig() {
  prefs.clear();
  webUrl = DEFAULT_WEB_APP_URL;
  apiKey = generateApiKey();
  saveConfig();
  Serial.println("[CONFIG] Configuration reinitialisee; nouvelle API key generee.");
}
}

void trakConfigBegin() {
  if (ready) return;
  prefs.begin(PREF_NS, false);
  webUrl = normalizeUrl(prefs.getString(KEY_URL, ""));
  apiKey = prefs.getString(KEY_API, "");

  if (webUrl.length() == 0) {
    webUrl = DEFAULT_WEB_APP_URL;
    Serial.println("[CONFIG] URL absente -> URL usine utilisee. SETURL permet de la modifier.");
  }
  if (apiKey.length() < 32) {
    apiKey = generateApiKey();
    prefs.putString(KEY_API, apiKey);
    Serial.println("[CONFIG] Nouvelle API key generee et stockee en NVS.");
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
  } else if (command == "RESETCONFIG") {
    resetConfig();
    printConfig();
  }
}

String trakWebAppUrl() { return webUrl; }
String trakApiKey() { return apiKey; }
bool trakConfigReady() { return ready; }
