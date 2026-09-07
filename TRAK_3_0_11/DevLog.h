#pragma once

#include <Arduino.h>
#include "Config.h"

// Journal de développement sur carte SD.
// Préfixe SD : [YYYY-MM-DD HH:MM:SS.mmm] en heure locale France.
// La date/heure est synchronisée dès que le GNSS fournit une date/heure UTC.
void devLogBegin();
bool devLogReady();
void devLogFlush();

// Synchronise l'horloge système avec la date/heure UTC fournie par le GNSS.
void devLogSyncTime(uint16_t year, uint8_t month, uint8_t day,
                    uint8_t hour, uint8_t minute, uint8_t second, uint16_t millisPart);

class DevLogSerial : public Print {
public:
  void begin(unsigned long baud);

  size_t write(uint8_t byte) override;

  template <typename... Args>
  size_t printf(const char* format, Args... args) {
    char buffer[512];
    const int n = snprintf(buffer, sizeof(buffer), format, args...);
    if (n <= 0) return 0;
    return Print::write(reinterpret_cast<const uint8_t*>(buffer), (size_t)n);
  }
};

extern DevLogSerial DevSerial;
