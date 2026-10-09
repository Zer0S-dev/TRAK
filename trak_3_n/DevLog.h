#pragma once

#include <Arduino.h>
#include "Config.h"

#if DEV_LOG
#include <Print.h>

// Un seul point de sortie pour les diagnostics : Serial et une file RAM SD.
// write() ne fait jamais d'acces SD et n'attend jamais si la file est pleine.
class TrakDevLogger : public Print {
public:
  using Print::write;
  void begin(unsigned long baud);
  size_t write(uint8_t value) override;
  int printf(const char* format, ...);
  void flush() override;
};

extern TrakDevLogger DevSerial;
#else
class TrakDevLogger {
public:
  void begin(unsigned long) {}
  template <typename T> void print(const T&) {}
  template <typename T> void println(const T&) {}
  template <typename T, typename U> void print(const T&, U) {}
  template <typename T, typename U> void println(const T&, U) {}
  template <typename... Args> int printf(const char*, Args...) { return 0; }
  void flush() {}
};
static TrakDevLogger DevSerial;
#endif

// Initialise la file et la tache SD. Doit etre appele apres Serial.begin().
void devLogInit();

// Compatibilite avec les evenements deja journalises par devLog().
void devLog(const String& message);

// Statistiques de diagnostic, sans effet sur le fonctionnement du TRAK.
uint32_t devLogDroppedBytes();
