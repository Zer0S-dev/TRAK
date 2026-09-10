#include <Arduino.h>
#include "Config.h"
#include "TrakRuntime.h"
#include "MotionManager.h"

void trakCommunicationTaskFixed(void* parameter);
bool trakPositionBufferInit();

/* TRAK 3.0.4
   Core 0: modem + GNSS + SD FIFO + REST JSON + LSM6DS3
   Core 1: WS2812 status engine
*/
void setup() {
  trakRuntimeInit();

  // Initialize the gyro before the communication task so motion state is
  // available immediately without changing the SD FIFO startup sequence.
  motionBegin();

  // Restore the persistent FIFO before launching the communication task.
  // This prevents the 8192-slot SD scan from blocking TRAK_COM/IDLE0.
  trakPositionBufferInit();

  TaskHandle_t communicationTask = nullptr;
  TaskHandle_t ledTask = nullptr;

  const BaseType_t communicationCreated = xTaskCreatePinnedToCore(
    trakCommunicationTaskFixed, "TRAK_COM", 8192, nullptr, 2, &communicationTask, 0
  );
  const BaseType_t ledCreated = xTaskCreatePinnedToCore(
    trakLedTask, "TRAK_LED", 4096, nullptr, 1, &ledTask, 1
  );

  if (communicationCreated != pdPASS) Serial.println("[TRAK] ERREUR: tache communication non creee.");
  if (ledCreated != pdPASS) Serial.println("[TRAK] ERREUR: tache LED non creee.");
  if (communicationCreated == pdPASS && ledCreated == pdPASS) Serial.println("[TRAK] Dual-core runtime pret.");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
