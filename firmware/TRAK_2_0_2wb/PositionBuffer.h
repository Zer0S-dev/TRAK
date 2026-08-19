#pragma once

#include <Arduino.h>

struct BufferedPosition {
  float latitude = 0.0f;
  float longitude = 0.0f;
  float altitude = 0.0f;
  float speedKmh = 0.0f;
  uint16_t year = 0;
  uint8_t month = 0;
  uint8_t day = 0;
  uint32_t capturedAtMs = 0;
};

bool positionBufferBegin();
bool positionBufferIsReady();
uint32_t positionBufferCount();
uint32_t positionBufferCapacity();
bool positionBufferPush(const BufferedPosition& position);
bool positionBufferPeek(BufferedPosition& position);
bool positionBufferPop();
void positionBufferClear();
