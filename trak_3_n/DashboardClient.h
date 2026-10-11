#pragma once
#include <Arduino.h>
#include "TrakRuntime.h"

enum class DashboardResult : uint8_t {
  NotReady,
  Success,
  Failed
};

void dashboardBegin();
DashboardResult dashboardSend(const GnssPosition& position);
