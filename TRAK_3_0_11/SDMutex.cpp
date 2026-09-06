#include "SDMutex.h"

namespace {
SemaphoreHandle_t g_sdMutex = nullptr;
}

bool sdMutexBegin()
{
  if (g_sdMutex != nullptr) return true;
  g_sdMutex = xSemaphoreCreateMutex();
  return g_sdMutex != nullptr;
}

bool sdMutexLock(TickType_t timeoutTicks)
{
  if (g_sdMutex == nullptr) return false;
  return xSemaphoreTake(g_sdMutex, timeoutTicks) == pdTRUE;
}

void sdMutexUnlock()
{
  if (g_sdMutex != nullptr) xSemaphoreGive(g_sdMutex);
}
