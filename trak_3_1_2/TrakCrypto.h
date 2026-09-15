#pragma once
#include <Arduino.h>

// Encrypts a complete JSON document with the TRAK provisioning key.
// The returned JSON keeps only trak_id/version/alg in clear; all payload
// data is inside the Base64 encoded AES-256-GCM envelope.
String trakEncryptJson(const String& plaintextJson, const String& trakId);
