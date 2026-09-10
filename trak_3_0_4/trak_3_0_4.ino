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

static void networkLedTask(void*) {
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
