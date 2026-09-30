#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "Config.h"
#include "TrakConfig.h"
#include "TrakRuntime.h"
#include "MotionManager.h"
#include "WiFiManager.h"
#include "WizardManager.h"

extern Adafruit_NeoPixel leds;
extern volatile bool cellularReady;
void trakCommunicationTaskFixed(void* parameter);
bool trakPositionBufferInit();
void updateLeds();
uint8_t trakActiveNetworkCode();

static TaskHandle_t communicationTaskHandle = nullptr;

void trakCommunicationSuspendForWizard() {
  if (communicationTaskHandle) {
    vTaskSuspend(communicationTaskHandle);
    Serial.println("[WIZARD] Tache communication suspendue.");
  }
}

void trakCommunicationResumeAfterWizard() {
  if (communicationTaskHandle) {
    vTaskResume(communicationTaskHandle);
    Serial.println("[WIZARD] Tache communication reprise.");
  }
}

static uint32_t wizardWheel(uint8_t position) {
  position = 255 - position;
  if (position < 85) {
    return leds.Color(255 - position * 3, 0, position * 3);
  }
  if (position < 170) {
    position -= 85;
    return leds.Color(0, position * 3, 255 - position * 3);
  }
  position -= 170;
  return leds.Color(position * 3, 255 - position * 3, 0);
}

static void networkLedTask(void*) {
  uint8_t rainbowOffset = 0;
  for (;;) {
    if (trakWizardActive()) {
      // Pixel 0 remains the center; the six surrounding pixels form the animated ring.
      leds.setPixelColor(0, 0);
      for (uint16_t i = 0; i < WS2812_RING_COUNT; ++i) {
        const uint8_t hue = rainbowOffset + (uint8_t)((i * 256UL) / WS2812_RING_COUNT);
        leds.setPixelColor(WS2812_CENTER_COUNT + i, wizardWheel(hue));
      }
      leds.show();
      rainbowOffset += 4;
      vTaskDelay(pdMS_TO_TICKS(30));
      continue;
    }

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
   Core 0: modem + GNSS + SD FIFO + REST JSON + LSM6DS3 + network priority
   Core 1: single WS2812 status engine + Wi-Fi/4G network indication + Wizard
*/
void setup() {
  Serial.begin(DEBUG_BAUD);
  trakConfigBegin();
  trakRuntimeInit();
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
    trakWizardCommand(command);
    trakConfigHandleCommand(command);
  }
  trakWizardTask();
  vTaskDelay(pdMS_TO_TICKS(20));
}
