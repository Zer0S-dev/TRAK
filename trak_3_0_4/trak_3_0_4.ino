#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "Config.h"
#include "TrakRuntime.h"
#include "MotionManager.h"
#include "WiFiManager.h"

extern Adafruit_NeoPixel leds;
extern volatile bool cellularReady;
void trakCommunicationTaskFixed(void* parameter);
bool trakPositionBufferInit();

static void wifiLedOverrideTask(void*) {
  for (;;) {
    if (wifiIsActive()) leds.setPixelColor(0, leds.Color(0, 80, 0));
    else if (cellularReady) leds.setPixelColor(0, leds.Color(80, 0, 80));
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

/* TRAK 3.0.5
   Core 0: modem + GNSS + SD FIFO + REST JSON + LSM6DS3 + network priority
   Core 1: WS2812 status engine + Wi-Fi/4G network indication
*/
void setup() {
  trakRuntimeInit();
  motionBegin();
  trakPositionBufferInit();

  TaskHandle_t communicationTask = nullptr;
  TaskHandle_t ledTask = nullptr;
  TaskHandle_t networkLedTask = nullptr;

  const BaseType_t communicationCreated = xTaskCreatePinnedToCore(trakCommunicationTaskFixed, "TRAK_COM", 8192, nullptr, 2, &communicationTask, 0);
  const BaseType_t ledCreated = xTaskCreatePinnedToCore(trakLedTask, "TRAK_LED", 4096, nullptr, 1, &ledTask, 1);
  const BaseType_t networkLedCreated = xTaskCreatePinnedToCore(wifiLedOverrideTask, "TRAK_NET_LED", 2048, nullptr, 2, &networkLedTask, 1);

  if (communicationCreated != pdPASS) Serial.println("[TRAK] ERREUR: tache communication non creee.");
  if (ledCreated != pdPASS) Serial.println("[TRAK] ERREUR: tache LED non creee.");
  if (networkLedCreated != pdPASS) Serial.println("[TRAK] ERREUR: tache LED reseau non creee.");
  if (communicationCreated == pdPASS && ledCreated == pdPASS && networkLedCreated == pdPASS) Serial.println("[TRAK] Dual-core runtime pret.");
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }
