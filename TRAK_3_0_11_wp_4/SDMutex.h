#pragma once

#include <Arduino.h>

// Mutex global de la carte SD partagee entre Core 0 (buffer/logs) et Core 1 (Web).
bool sdMutexBegin();
bool sdMutexLock(TickType_t timeoutTicks = portMAX_DELAY);
void sdMutexUnlock();
