#include <Arduino.h>
#include <esp_system.h>
#include <mbedtls/gcm.h>
#include <mbedtls/base64.h>
#include "TrakCrypto.h"
#include "TrakConfig.h"

namespace {
constexpr size_t KEY_BYTES = 32;
constexpr size_t NONCE_BYTES = 12;
constexpr size_t TAG_BYTES = 16;

bool hexByte(char c, uint8_t& value) {
  if (c >= '0' && c <= '9') { value = static_cast<uint8_t>(c - '0'); return true; }
  if (c >= 'A' && c <= 'F') { value = static_cast<uint8_t>(c - 'A' + 10); return true; }
  if (c >= 'a' && c <= 'f') { value = static_cast<uint8_t>(c - 'a' + 10); return true; }
  return false;
}

bool decodeKey(const String& text, uint8_t key[KEY_BYTES]) {
  if (text.length() != 64) return false;
  for (size_t i = 0; i < KEY_BYTES; ++i) {
    uint8_t hi = 0, lo = 0;
    if (!hexByte(text[i * 2], hi) || !hexByte(text[i * 2 + 1], lo)) return false;
    key[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

String base64Encode(const uint8_t* data, size_t length) {
  const size_t capacity = ((length + 2) / 3) * 4 + 1;
  String out;
  out.reserve(capacity);
  uint8_t* buffer = static_cast<uint8_t*>(malloc(capacity));
  if (!buffer) return String();
  size_t olen = 0;
  const int rc = mbedtls_base64_encode(buffer, capacity, &olen, data, length);
  if (rc == 0) {
    buffer[olen] = 0;
    out = reinterpret_cast<const char*>(buffer);
  }
  free(buffer);
  return out;
}
}

String trakEncryptJson(const String& plaintextJson, const String& trakId) {
  uint8_t key[KEY_BYTES];
  if (!decodeKey(trakEncryptionKey(), key) || trakId.isEmpty()) return String();

  uint8_t nonce[NONCE_BYTES];
  for (size_t i = 0; i < NONCE_BYTES; ++i) nonce[i] = static_cast<uint8_t>(esp_random() & 0xFF);

  uint8_t* ciphertext = static_cast<uint8_t*>(malloc(plaintextJson.length()));
  if (!ciphertext && plaintextJson.length() != 0) return String();
  uint8_t tag[TAG_BYTES];

  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (rc == 0) {
    rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                                   plaintextJson.length(), nonce, NONCE_BYTES,
                                   nullptr, 0,
                                   reinterpret_cast<const unsigned char*>(plaintextJson.c_str()),
                                   ciphertext, TAG_BYTES, tag);
  }
  mbedtls_gcm_free(&gcm);
  if (rc != 0) { free(ciphertext); return String(); }

  // One compact envelope: nonce || ciphertext || authentication tag.
  const size_t envelopeLen = NONCE_BYTES + plaintextJson.length() + TAG_BYTES;
  uint8_t* envelope = static_cast<uint8_t*>(malloc(envelopeLen));
  if (!envelope) { free(ciphertext); return String(); }
  memcpy(envelope, nonce, NONCE_BYTES);
  if (plaintextJson.length()) memcpy(envelope + NONCE_BYTES, ciphertext, plaintextJson.length());
  memcpy(envelope + NONCE_BYTES + plaintextJson.length(), tag, TAG_BYTES);

  const String encoded = base64Encode(envelope, envelopeLen);
  free(envelope);
  free(ciphertext);
  if (encoded.isEmpty()) return String();

  String json;
  json.reserve(encoded.length() + trakId.length() + 100);
  json += "{\"trak_id\":\"";
  json += trakId;
  json += "\",\"alg\":\"A256GCM\",\"data\":\"";
  json += encoded;
  json += "\"}";
  return json;
}
