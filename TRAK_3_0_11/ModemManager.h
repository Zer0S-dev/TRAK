#ifndef MODEM_MANAGER_H
#define MODEM_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"

extern HardwareSerial modemSerial;

struct GnssData {
  bool  hasFix = false;
  int   satellites = 0;
  int   gpsSatellites = 0;
  int   glonassSatellites = 0;
  int   beidouSatellites = 0;
  int   galileoSatellites = 0;
  int   fixMode = 0;
  float latitude = 0.0f;
  float longitude = 0.0f;
  float altitude = 0.0f;
  float speedKmh = 0.0f;
  float rawSpeedKmh = 0.0f;
  float filteredSpeedKmh = 0.0f;
  float rawAltitude = 0.0f;
  float filteredAltitude = 0.0f;
  uint32_t satellitesUpdatedAt = 0;
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  uint16_t millisecond = 0;
};

String sendATCommand(const char* cmd, uint32_t timeoutMs = 2000);
void modemMutexBegin();
void powerOnModem();
void beginModemSerial();
void configureMultiGNSS();
String detectAutoAPN();
int getCellularSignalQuality();

bool sendToTrackserverWiFi(float lat, float lon, float altitude, float speedKmh,
                           uint16_t year, uint8_t month, uint8_t day,
                           uint8_t hour, uint8_t minute, uint8_t second, uint16_t millisecond,
                           uint32_t& bytesSent);
bool sendToTrackserverCellular(float lat, float lon, float altitude, float speedKmh,
                               uint16_t year, uint8_t month, uint8_t day,
                               uint8_t hour, uint8_t minute, uint8_t second, uint16_t millisecond,
                               uint32_t& bytesSent);
bool connectCellularNetwork();

// SMS utilisateur (A7670). Le texte est volontairement ASCII pour rester
// compatible avec le mode SMS texte/GSM 7-bit.
bool sendSMS(const char* phoneNumber, const char* message);
// Recupere un SMS entrant non lu. Le SMS est supprime apres lecture reussie.
bool readIncomingSMS(String& sender, String& message);
bool modemCommunicationReady();

// Lecture GNSS : logique conservée depuis le firmware fonctionnel 1.0.
bool readGNSS(GnssData& data);

#endif
