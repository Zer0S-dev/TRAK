#include <Arduino.h>
#include <nvs_flash.h>
#include "Config.h"
#include "DevLog.h"
#include "RuntimeConfig.h"
#include "PositionBuffer.h"
#include "SentinelManager.h"
#include "WebInterface.h"
#include "TrackerState.h"
#include "NetworkManager.h"
#include "CommunicationManager.h"
#include "TrackerRuntime.h"
#include "StatusLedManager.h"
#include "SDMutex.h"

void setup()
{
  DevSerial.begin(115200);
  delay(1000);

  if (RESET_NVS) {
    DevSerial.println();
    DevSerial.println("[NVS] RESET_NVS = true : effacement complet de la NVS...");

    esp_err_t err = nvs_flash_deinit();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_INITIALIZED) {
      DevSerial.printf("[NVS] nvs_flash_deinit() erreur: %s\\n", esp_err_to_name(err));
    }

    err = nvs_flash_erase();
    if (err != ESP_OK && err != ESP_ERR_NVS_NO_FREE_PAGES) {
      DevSerial.printf("[NVS] nvs_flash_erase() erreur: %s\\n", esp_err_to_name(err));
    }

    err = nvs_flash_init();
    if (err != ESP_OK) {
      DevSerial.printf("[NVS] nvs_flash_init() erreur: %s\\n", esp_err_to_name(err));
    } else {
      DevSerial.println("[NVS] Effacement termine. Remettre RESET_NVS = false puis reflasher.");
    }
  }

  if (!sdMutexBegin()) {
    DevSerial.println("[FATAL] Impossible de créer le mutex SD.");
    while (true) delay(1000);
  }

  runtimeConfigBegin();
  sentinelBegin();

  // Initialisation du CJMCU-2812-7 : animation de démarrage non bloquante.
  statusLedBegin();

  if (positionBufferBegin()) {
    // TrackerState is initialized just below; the snapshot is refreshed once
    // the state mutex exists.
  } else {
    DevSerial.println("[BUFFER] Buffer SD indisponible : le tracking continue sans stockage offline.");
  }

  if (!trackerStateBegin()) {
    DevSerial.println("[FATAL] Impossible de créer les mutex d'état/réseau.");
    while (true) delay(1000);
  }

  updateBufferSnapshot();

  DevSerial.print("[MODE] Configuration persistante : ");
  DevSerial.println(activeNetworkName());

  modemMutexBegin();

  DevSerial.println();
  DevSerial.println("======================================");
  DevSerial.println(" TRAK - 3.0.91osm / TrackingEngine v2");
  DevSerial.println(" Architecture modulaire");
  DevSerial.println(" Core 0 : A7670 / GNSS / communication");
  DevSerial.println(" Core 1 : réseau / Web / WS2812 status / UI");
  DevSerial.println("======================================");

  networkManagerBegin();
  trackerRuntimeBegin();

  webBegin(getWebTrackerData);
  xTaskCreatePinnedToCore(
      webTask, "WebInterface", 8192, nullptr, 1, nullptr, 1);

  xTaskCreatePinnedToCore(
      communicationTask, "Communication", 8192, nullptr, 2, nullptr, 0);

  xTaskCreatePinnedToCore(
      networkTask, "NetworkManager", 6144, nullptr, 2, nullptr, 1);

}

void loop()
{
  vTaskDelay(pdMS_TO_TICKS(1000));
}
