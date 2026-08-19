#include "MotionManager.h"
#include <math.h>

namespace {
float baselineXmv = 0.0f;
float baselineYmv = 0.0f;
float filteredMotionG = 0.0f;
MotionState state;
uint32_t lastSampleAt = 0;
uint32_t calibrationStart = 0;
uint32_t calibrationCount = 0;
uint32_t motionAboveThresholdSince = 0;
double sumX = 0.0;
double sumY = 0.0;
}

void motionBegin()
{
  analogReadResolution(12);
  analogSetPinAttenuation(ACC_PIN_X, ADC_11db);
  analogSetPinAttenuation(ACC_PIN_Y, ADC_11db);

  pinMode(ACC_PIN_X, INPUT);
  pinMode(ACC_PIN_Y, INPUT);

  state = MotionState{};
  filteredMotionG = 0.0f;
  calibrationStart = millis();
  calibrationCount = 0;
  motionAboveThresholdSince = 0;
  sumX = 0.0;
  sumY = 0.0;
  lastSampleAt = 0;

  Serial.println("[ACC] ADXL337 X/Y actif - Z ignore");
  Serial.println("[ACC] Calibration au repos...");
}

void motionUpdate(uint32_t now)
{
  if (lastSampleAt != 0 && now - lastSampleAt < ACC_SAMPLE_INTERVAL_MS) {
    return;
  }
  lastSampleAt = now;

  const float xMv = static_cast<float>(analogReadMilliVolts(ACC_PIN_X));
  const float yMv = static_cast<float>(analogReadMilliVolts(ACC_PIN_Y));

  if (!state.calibrated) {
    sumX += xMv;
    sumY += yMv;
    ++calibrationCount;

    if (now - calibrationStart >= ACC_CALIBRATION_MS && calibrationCount > 0) {
      baselineXmv = static_cast<float>(sumX / calibrationCount);
      baselineYmv = static_cast<float>(sumY / calibrationCount);
      state.calibrated = true;
      state.moving = false;
      state.stationaryConfirmed = false;
      state.lastMotionAt = now;
      state.stationarySince = now;
      motionAboveThresholdSince = 0;

      Serial.print("[ACC] Calibration X0=");
      Serial.print(baselineXmv, 1);
      Serial.print("mV Y0=");
      Serial.print(baselineYmv, 1);
      Serial.println("mV");
      Serial.println("[ACC] Etat initial : IMMOBILE - confirmation 30s");
    }
    return;
  }

  // X/Y uniquement. La composante Z n'intervient jamais dans la décision.
  const float dxG = (xMv - baselineXmv) / ACC_SENSITIVITY_MV_PER_G;
  const float dyG = (yMv - baselineYmv) / ACC_SENSITIVITY_MV_PER_G;
  const float magnitudeG = sqrtf(dxG * dxG + dyG * dyG);

  // Petit lissage pour éviter que le bruit ADC ne fasse basculer l'état.
  filteredMotionG = 0.75f * filteredMotionG + 0.25f * magnitudeG;

  state.xG = dxG;
  state.yG = dyG;
  state.motionG = filteredMotionG;

  if (filteredMotionG >= ACC_MOTION_THRESHOLD_G) {
    // En MOBILE, un pic bref ne doit pas empêcher le retour au stationnaire.
    // On ne considère le mouvement comme réel que s'il reste au-dessus du
    // seuil pendant ACC_MOTION_RESET_SEC secondes.
    if (motionAboveThresholdSince == 0) {
      motionAboveThresholdSince = now;
    }

    if (state.stationaryConfirmed ||
        now - motionAboveThresholdSince >= ACC_MOTION_RESET_SEC * 1000UL) {
      const bool wasMoving = state.moving;
      state.moving = true;
      state.stationaryConfirmed = false;
      state.lastMotionAt = now;
      state.stationarySince = 0;

      if (!wasMoving) {
        Serial.println("[MODE] MOUVEMENT DETECTE - rythme normal 15s");
      }
    }
  } else {
    // Sous le seuil mouvement : la condition de mouvement soutenu est annulée.
    motionAboveThresholdSince = 0;

    if (!state.stationaryConfirmed) {
      if (state.stationarySince == 0) {
        state.stationarySince = now;
        Serial.println("[MODE] RETOUR AU CALME - confirmation stationnaire demarree");
      }

      if (now - state.stationarySince >= MOTION_IDLE_CONFIRM_SEC * 1000UL) {
        state.moving = false;
        state.stationaryConfirmed = true;
        Serial.print("[MODE] STATIONNAIRE CONFIRME - envoi toutes les ");

        if (SEND_INTERVAL_STATIONARY_SEC == 0) {
          Serial.println("OFF");
        } else {
          Serial.print(SEND_INTERVAL_STATIONARY_SEC);
          Serial.println("s");
        }
      }
    }
  }
}

const MotionState& motionState()
{
  return state;
}

uint32_t currentSendIntervalMs()
{
  // Tant que l'immobilité n'est pas confirmée pendant 30 s,
  // on conserve le rythme mobile de 15 s.
  if (state.moving || !state.stationaryConfirmed) {
    return SEND_INTERVAL_MOVING_SEC * 1000UL;
  }

  return SEND_INTERVAL_STATIONARY_SEC * 1000UL;
}

const char* motionModeName()
{
  if (!state.calibrated) {
    return "CALIBRATION";
  }

  if (state.moving) {
    return "MOBILE";
  }

  return state.stationaryConfirmed ? "STATIONNAIRE" : "IMMOBILE_CONFIRMATION";
}
