#include <Arduino.h>
#include <Preferences.h>
#include "TrakConfig.h"
#include "Config.h"

namespace {
Preferences prefs;
constexpr const char* NS = "trak_cfg";
}

bool trakConfigBegin() { return prefs.begin(NS, false); }
String trakWebAppUrl() { return prefs.getString("server_url", String(TRAK_CONNECT_BASE_URL)); }
String trakUserPhone() { return prefs.getString("user_phone", ""); }
String trakPhone() { return prefs.getString("trak_phone", ""); }
bool trakConfigProvisioned() { return prefs.getBool("provisioned", false); }
void setTrakWebAppUrl(const String& url) { prefs.putString("server_url", url); }
void setTrakUserPhone(const String& phone) { prefs.putString("user_phone", phone); }
void setTrakPhone(const String& phone) { prefs.putString("trak_phone", phone); }
void setTrakConfigProvisioned(bool provisioned) { prefs.putBool("provisioned", provisioned); }

String trakEncryptionKey() { return prefs.getString("enc_key", ""); }
void setTrakEncryptionKey(const String& key) { prefs.putString("enc_key", key); }
void clearTrakEncryptionKey() { prefs.remove("enc_key"); }
bool trakHasEncryptionKey() { return trakEncryptionKey().length() == 64; }

String generateTrakEncryptionKey() {
  // 32 random bytes = 256-bit key, represented as 64 hexadecimal characters.
  uint8_t key[32];
  for (size_t i = 0; i < sizeof(key); ++i) {
    uint32_t r = esp_random();
    key[i] = static_cast<uint8_t>(r & 0xFF);
  }
  static const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(64);
  for (uint8_t b : key) {
    out += hex[(b >> 4) & 0x0F];
    out += hex[b & 0x0F];
  }
  return out;
}

void resetTrakProvisioning() {
  prefs.remove("server_url");
  prefs.remove("user_phone");
  prefs.remove("trak_phone");
  prefs.remove("provisioned");
  clearTrakEncryptionKey();
}
