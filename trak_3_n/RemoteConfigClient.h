#pragma once
#include <Arduino.h>
#include "TrakRuntime.h"
#include "ElectricalMonitor.h"

void remoteConfigBegin();
void remoteConfigTick(const GnssPosition& gnss, bool gnssFix, const ElectricalSample& electrical);
