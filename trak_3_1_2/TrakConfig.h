#include <Arduino.h>

bool trakConfigBegin();
String trakWebAppUrl();
String trakUserPhone();
String trakPhone();
bool trakConfigProvisioned();
void setTrakWebAppUrl(const String& url);
void setTrakUserPhone(const String& phone);
void setTrakPhone(const String& phone);
void setTrakConfigProvisioned(bool provisioned);
void resetTrakProvisioning();

// TRAK 3.2.0 encryption key. The key is generated once by the Wizard,
// stored in NVS and copied manually into TRAK Connect.
String trakEncryptionKey();
void setTrakEncryptionKey(const String& key);
void clearTrakEncryptionKey();
bool trakHasEncryptionKey();
String generateTrakEncryptionKey();
