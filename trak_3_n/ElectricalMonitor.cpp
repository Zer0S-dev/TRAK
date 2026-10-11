#include "ElectricalMonitor.h"

#include <Wire.h>
#include <math.h>
#include <Adafruit_INA219.h>

namespace {
constexpr uint32_t ELECTRICAL_MEASURE_INTERVAL_MS = 1000UL;
constexpr uint32_t INA219_RETRY_INTERVAL_MS = 30000UL;
constexpr uint8_t INA219_EXPECTED_ADDRESS = 0x40;

Adafruit_INA219 ina219(INA219_EXPECTED_ADDRESS);
ElectricalSample latestSample;
bool ina219Ready = false;
uint32_t lastMeasureMs = 0;
uint32_t lastInitAttemptMs = 0;

bool beginIna219() {
  // Wire est deja initialise par motionBegin() pour le LSM6DS3.
  // Ne jamais appeler Wire.begin() ni Wire.setClock() ici.
  lastInitAttemptMs = millis();

  if (!ina219.begin(&Wire)) {
    ina219Ready = false;
    latestSample.valid = false;
    Serial.println("[INA219] ERREUR : capteur introuvable a 0x40.");
    return false;
  }

  ina219.setCalibration_32V_2A();
  ina219Ready = true;
  Serial.println("[INA219] Detecte sur le bus I2C partage, calibration 32V/2A.");
  return true;
}
}

bool electricalMonitorBegin() {
  latestSample = ElectricalSample{};
  lastMeasureMs = 0;
  lastInitAttemptMs = 0;
  return beginIna219();
}

void electricalMonitorUpdate(uint32_t now) {
  if (!ina219Ready) {
    if (now - lastInitAttemptMs >= INA219_RETRY_INTERVAL_MS) {
      beginIna219();
    }
    return;
  }

  if (lastMeasureMs != 0 &&
      now - lastMeasureMs < ELECTRICAL_MEASURE_INTERVAL_MS) {
    return;
  }
  lastMeasureMs = now;

  const float busVoltage = ina219.getBusVoltage_V();
  const float shuntVoltageMv = ina219.getShuntVoltage_mV();
  const float currentMa = ina219.getCurrent_mA();

  const float volt = busVoltage + shuntVoltageMv / 1000.0f;
  const float amp = currentMa / 1000.0f;

  if (!isfinite(volt) || !isfinite(amp)) {
    latestSample.valid = false;
    Serial.println("[INA219] ERREUR : mesure non finie.");
    return;
  }

  latestSample.volt = volt;
  latestSample.amp = amp;
  latestSample.measuredAtMs = now;
  latestSample.valid = true;

  Serial.printf("[INA219] volt=%.3f V | amp=%.4f A\n", volt, amp);
}

ElectricalSample electricalMonitorSnapshot() {
  return latestSample;
}
