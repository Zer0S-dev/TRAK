#pragma once
#include <Arduino.h>
#include "TrakRuntime.h"

enum class TrackserverResult : uint8_t {
  NotReady,
  Success,
  Failed
};

void trackserverBegin();
TrackserverResult trackserverSend(const GnssPosition& position);
TrackserverResult trackserverSend(const GnssPosition& position, const String& apiKey);
