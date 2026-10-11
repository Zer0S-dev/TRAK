#pragma once
#include <Arduino.h>
#include "TrakRuntime.h"
#include "ElectricalMonitor.h"

enum class DashboardResult : uint8_t {
  NotReady,
  Success,
  Failed
};

void dashboardBegin();
DashboardResult dashboardSend(const GnssPosition& position, const ElectricalSample& sample, const String& healthTimestamp);
