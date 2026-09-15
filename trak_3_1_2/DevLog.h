#pragma once

#include <Arduino.h>
#include "Config.h"

#if DEV_LOG
#define DevSerial Serial
#else
class TrakDevNullSerial {
public:
  template <typename T> void print(const T&) {}
  template <typename T> void println(const T&) {}
  template <typename T, typename U> void print(const T&, U) {}
  template <typename T, typename U> void println(const T&, U) {}
  template <typename... Args> void printf(const char*, Args...) {}
};
static TrakDevNullSerial trakDevNullSerial;
#define DevSerial trakDevNullSerial
#endif

// Terrain SD logger implemented in TrakRuntime.cpp.
void devLog(const String& message);
