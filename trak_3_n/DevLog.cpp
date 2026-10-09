#include "DevLog.h"

#if DEV_LOG
#include <SPI.h>
#include <SD.h>
#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <stdio.h>

namespace {
constexpr UBaseType_t LOG_QUEUE_BYTES = 2048;
constexpr size_t LOG_LINE_CAPACITY = 320;
QueueHandle_t logQueue = nullptr;
TaskHandle_t logTaskHandle = nullptr;
volatile uint32_t droppedBytes = 0;
bool sdReady = false;
SemaphoreHandle_t sdMutex = nullptr;

void writeLogLine(const char* line) {
  if (!sdReady || !line || !sdMutex) return;
  // Le logger ne prend jamais le verrou en attente : la FIFO reste prioritaire.
  if (xSemaphoreTakeRecursive(sdMutex, 0) != pdTRUE) { ++droppedBytes; return; }
  File file = SD.open("/dev.log", FILE_APPEND);
  if (!file) { ++droppedBytes; xSemaphoreGiveRecursive(sdMutex); return; }
  file.print(millis());
  file.print(' ');
  file.println(line);
  file.close();
  xSemaphoreGiveRecursive(sdMutex);
}

void devLogWriterTask(void*) {
  char line[LOG_LINE_CAPACITY];
  size_t used = 0;
  bool discarding = false;
  for (;;) {
    char ch = 0;
    if (xQueueReceive(logQueue, &ch, portMAX_DELAY) != pdTRUE) continue;
    if (ch == '\r') continue;
    if (ch == '\n') {
      if (!discarding) {
        line[used] = '\0';
        writeLogLine(line);
      }
      used = 0;
      discarding = false;
      continue;
    }
    if (discarding) continue;
    if (used + 1 < sizeof(line)) line[used++] = ch;
    else {
      // Ligne trop longue : on la tronque jusqu'au prochain saut de ligne.
      discarding = true;
      ++droppedBytes;
    }
  }
}
}

TrakDevLogger DevSerial;

void TrakDevLogger::begin(unsigned long baud) {
  Serial.begin(baud);
}

size_t TrakDevLogger::write(uint8_t value) {
  // Serial reste prioritaire : aucun acces SD et aucun attente dans ce chemin.
  const size_t written = Serial.write(value);
  if (logQueue && xQueueSend(logQueue, &value, 0) != pdTRUE) ++droppedBytes;
  return written;
}

int TrakDevLogger::printf(const char* format, ...) {
  if (!format) return 0;
  char buffer[LOG_LINE_CAPACITY];
  va_list args;
  va_start(args, format);
  const int count = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (count <= 0) return count;
  const size_t length = strnlen(buffer, sizeof(buffer));
  write(reinterpret_cast<const uint8_t*>(buffer), length);
  return count;
}

void TrakDevLogger::flush() {
  Serial.flush();
}

void devLogInit() {
  if (logTaskHandle) return;
  sdMutex = xSemaphoreCreateRecursiveMutex();
  logQueue = xQueueCreate(LOG_QUEUE_BYTES, sizeof(char));
  if (!logQueue) {
    Serial.println("[DEV-LOG] File RAM indisponible; logs SD desactives.");
    return;
  }

  if (sdMutex) xSemaphoreTakeRecursive(sdMutex, portMAX_DELAY);
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  sdReady = SD.begin(SD_CS_PIN, SPI, 10000000);
  if (sdMutex) xSemaphoreGiveRecursive(sdMutex);
  if (!sdReady) {
    Serial.println("[DEV-LOG] SD indisponible; logs Serial conserves.");
    vQueueDelete(logQueue);
    logQueue = nullptr;
    return;
  }

  if (xTaskCreatePinnedToCore(devLogWriterTask, "devLogSD", 3072, nullptr, 1,
                              &logTaskHandle, 0) != pdPASS) {
    Serial.println("[DEV-LOG] Tache SD impossible; logs Serial conserves.");
    SD.end();
    sdReady = false;
    vQueueDelete(logQueue);
    logQueue = nullptr;
    return;
  }
  devLog(String("=== TRAK ") + TRAK_VERSION + " | journal diagnostic ===");
  devLog("BOOT | logger centralise | SD asynchrone");
}

void devLog(const String& message) {
  if (!DEV_LOG) return;
  DevSerial.println(message);
}

uint32_t devLogDroppedBytes() { return droppedBytes; }
bool devLogSdLock(uint32_t timeoutMs) { return sdMutex && xSemaphoreTakeRecursive(sdMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE; }
void devLogSdUnlock() { if (sdMutex) xSemaphoreGiveRecursive(sdMutex); }
bool devLogSdReady() { return sdReady; }

#else

void devLogInit() {}
void devLog(const String&) {}
uint32_t devLogDroppedBytes() { return 0; }
bool devLogSdLock(uint32_t) { return true; }
void devLogSdUnlock() {}
bool devLogSdReady() { return false; }

#endif
