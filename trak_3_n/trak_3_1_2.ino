#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "Config.h"
#include "TrakRuntime.h"
#include "MotionManager.h"
#include "WiFiManager.h"
#include "SmsConfigManager.h"

extern Adafruit_NeoPixel leds;
extern volatile bool cellularReady;
void trakCommunicationTaskFixed(void* parameter);
bool trakPositionBufferInit();
void updateLeds();
uint8_t trakActiveNetworkCode();

static TaskHandle_t communicationTaskHandle = nullptr;

static void networkLedTask(void*) {
  uint8_t rainbowOffset = 0;
  for (;;) {
    updateLeds();
    const uint8_t network = trakActiveNetworkCode();
    if (network == 1) leds.setPixelColor(0, leds.Color(0, 80, 0));
    else if (network == 2) leds.setPixelColor(0, leds.Color(80, 0, 80));
    else leds.setPixelColor(0, 0);
    leds.show();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

/* TRAK 3.0.5 / 3.1.0
   Core 0: modem + GNSS + SD FIFO + LSM6DS3 + network priority
   Core 1: single WS2812 status engine + Wi-Fi/4G network indication
*/
void setup() {
  Serial.begin(DEBUG_BAUD);
  trakRuntimeInit();
  smsConfigBegin();
  motionBegin();
  trakPositionBufferInit();

  TaskHandle_t networkLedTaskHandle = nullptr;
  const BaseType_t communicationCreated = xTaskCreatePinnedToCore(trakCommunicationTaskFixed, "TRAK_COM", 8192, nullptr, 2, &communicationTaskHandle, 0);
  const BaseType_t networkLedCreated = xTaskCreatePinnedToCore(networkLedTask, "TRAK_LED", 4096, nullptr, 1, &networkLedTaskHandle, 1);

  if (communicationCreated != pdPASS) Serial.println("[TRAK] ERREUR: tache communication non creee.");
  if (networkLedCreated != pdPASS) Serial.println("[TRAK] ERREUR: tache LED non creee.");
  if (communicationCreated == pdPASS && networkLedCreated == pdPASS) Serial.println("[TRAK] Dual-core runtime pret.");
}

void loop() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
  }
  vTaskDelay(pdMS_TO_TICKS(20));
}
