#include "DevLog.h"
#include "MotionManager.h"
#include "Config.h"

#include <Wire.h>
#include <Preferences.h>
#include <math.h>

namespace {
constexpr uint8_t LSM6DS3_ADDR_0 = 0x6A;
constexpr uint8_t LSM6DS3_ADDR_1 = 0x6B;
constexpr uint8_t LSM6DS3_WHO_AM_I = 0x0F;
constexpr uint8_t LSM6DS3_CTRL2_G = 0x11;
constexpr uint8_t LSM6DS3_CTRL3_C = 0x12;
constexpr uint8_t LSM6DS3_STATUS_REG = 0x1E;
constexpr uint8_t LSM6DS3_OUTX_L_G = 0x22;
constexpr uint8_t LSM6DS3_CTRL2_G_CONFIG = 0x40;
constexpr uint8_t LSM6DS3_CTRL3_C_CONFIG = 0x44;
constexpr float GYRO_SENSITIVITY_DPS_PER_LSB = 0.00875f;
constexpr uint32_t GYRO_SAMPLE_INTERVAL_MS = 40;
constexpr uint8_t GYRO_READ_RETRIES = 2;
constexpr float GYRO_BIAS_ADAPT_ALPHA = 0.01f;
constexpr float GYRO_THRESHOLD_1_DPS = 2.0f;
constexpr float GYRO_THRESHOLD_2_DPS = 4.0f;
constexpr float GYRO_THRESHOLD_3_DPS = 6.0f;
constexpr float GYRO_THRESHOLD_4_DPS = 10.0f;
constexpr float GYRO_THRESHOLD_5_DPS = 15.0f;
constexpr float GYRO_STILL_RATIO = 0.70f;
constexpr uint32_t DEFAULT_ACTIVE_INTERVAL_SEC = 10;
constexpr uint32_t DEFAULT_IDLE_INTERVAL_SEC = 60;

MotionState state;
Preferences prefs;
uint8_t gyroAddress = 0;
uint8_t sensitivityLevel = 3;
uint32_t activeIntervalSec = DEFAULT_ACTIVE_INTERVAL_SEC;
uint32_t idleIntervalSec = DEFAULT_IDLE_INTERVAL_SEC;
uint32_t lastSampleAt = 0;
uint32_t motionAboveThresholdSince = 0;
float biasXDps = 0.0f, biasYDps = 0.0f, biasZDps = 0.0f;
bool biasInitialized = false;

bool allowedActive(uint32_t v) { return v == 5 || v == 10 || v == 15 || v == 20; }
bool allowedIdle(uint32_t v) { return v == 30 || v == 60 || v == 900 || v == 1800 || v == 3600; }

bool writeRegister(uint8_t reg, uint8_t value) {
  if (!gyroAddress) return false;
  Wire.beginTransmission(gyroAddress); Wire.write(reg); Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegisters(uint8_t reg, uint8_t* data, size_t length) {
  if (!gyroAddress || !data || !length) return false;
  Wire.beginTransmission(gyroAddress); Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  const size_t received = Wire.requestFrom((int)gyroAddress, (int)length, (int)true);
  if (received != length) return false;
  for (size_t i = 0; i < length; ++i) data[i] = Wire.read();
  return true;
}

bool readRegister(uint8_t reg, uint8_t& value) { return readRegisters(reg, &value, 1); }

bool detectGyro() {
  const uint8_t addresses[] = {LSM6DS3_ADDR_0, LSM6DS3_ADDR_1};
  for (uint8_t address : addresses) {
    gyroAddress = address;
    uint8_t who = 0;
    if (readRegister(LSM6DS3_WHO_AM_I, who) && who == 0x69) {
      DevSerial.print("[GYRO] LSM6DS3 detecte a 0x");
      if (address < 0x10) DevSerial.print('0');
      DevSerial.println(address, HEX);
      DevSerial.println("[GYRO] WHO_AM_I=0x69");
      return true;
    }
  }
  gyroAddress = 0;
  return false;
}

bool configureGyro() {
  if (!writeRegister(LSM6DS3_CTRL3_C, LSM6DS3_CTRL3_C_CONFIG)) return false;
  if (!writeRegister(LSM6DS3_CTRL2_G, LSM6DS3_CTRL2_G_CONFIG)) return false;
  delay(20);
  uint8_t ctrl2 = 0, ctrl3 = 0, status = 0;
  const bool ok = readRegister(LSM6DS3_CTRL2_G, ctrl2) && readRegister(LSM6DS3_CTRL3_C, ctrl3) && readRegister(LSM6DS3_STATUS_REG, status);
  if (ok) {
    DevSerial.print("[GYRO] CTRL2_G=0x"); DevSerial.print(ctrl2, HEX);
    DevSerial.print(" CTRL3_C=0x"); DevSerial.print(ctrl3, HEX);
    DevSerial.print(" STATUS=0x"); DevSerial.println(status, HEX);
  }
  return ok && ctrl2 == LSM6DS3_CTRL2_G_CONFIG && ctrl3 == LSM6DS3_CTRL3_C_CONFIG;
}

bool readGyroDps(float& x, float& y, float& z) {
  uint8_t raw[6] = {};
  for (uint8_t attempt = 0; attempt < GYRO_READ_RETRIES; ++attempt) {
    if (!readRegisters(LSM6DS3_OUTX_L_G, raw, sizeof(raw))) continue;
    const int16_t rawX = (int16_t)((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
    const int16_t rawY = (int16_t)((uint16_t)raw[2] | ((uint16_t)raw[3] << 8));
    const int16_t rawZ = (int16_t)((uint16_t)raw[4] | ((uint16_t)raw[5] << 8));
    x = rawX * GYRO_SENSITIVITY_DPS_PER_LSB;
    y = rawY * GYRO_SENSITIVITY_DPS_PER_LSB;
    z = rawZ * GYRO_SENSITIVITY_DPS_PER_LSB;
    return true;
  }
  return false;
}

float thresholdForLevel(uint8_t level) {
  switch (level) {
    case 1: return GYRO_THRESHOLD_1_DPS;
    case 2: return GYRO_THRESHOLD_2_DPS;
    case 4: return GYRO_THRESHOLD_4_DPS;
    case 5: return GYRO_THRESHOLD_5_DPS;
    default: return GYRO_THRESHOLD_3_DPS;
  }
}
}

void motionBegin() {
  Wire.begin(LSM6DS3_SDA_PIN, LSM6DS3_SCL_PIN);
  Wire.setClock(400000);
  state = MotionState{};
  lastSampleAt = 0; motionAboveThresholdSince = 0;
  biasXDps = biasYDps = biasZDps = 0.0f; biasInitialized = false;

  prefs.begin("motion", false);
  sensitivityLevel = prefs.getUChar("sensitivity", 3);
  if (sensitivityLevel < 1 || sensitivityLevel > 5) { sensitivityLevel = 3; prefs.putUChar("sensitivity", sensitivityLevel); }
  activeIntervalSec = prefs.getUInt("active_sec", DEFAULT_ACTIVE_INTERVAL_SEC);
  idleIntervalSec = prefs.getUInt("idle_sec", DEFAULT_IDLE_INTERVAL_SEC);
  if (!allowedActive(activeIntervalSec)) activeIntervalSec = DEFAULT_ACTIVE_INTERVAL_SEC;
  if (!allowedIdle(idleIntervalSec)) idleIntervalSec = DEFAULT_IDLE_INTERVAL_SEC;
  prefs.putUInt("active_sec", activeIntervalSec);
  prefs.putUInt("idle_sec", idleIntervalSec);

  if (!detectGyro()) {
    DevSerial.println("[GYRO] ERREUR : LSM6DS3 introuvable.");
    DevSerial.println("[GYRO] Verification I2C SDA=21 SCL=22, adresse 0x6A/0x6B.");
    return;
  }
  if (!configureGyro()) {
    DevSerial.println("[GYRO] ERREUR : configuration LSM6DS3 impossible.");
    gyroAddress = 0; return;
  }
  state.calibrated = true;
  DevSerial.println("[GYRO] 104 Hz / +/-245 dps / trigger mouvement sur norme gyro");
  DevSerial.println("[GYRO] Demarrage sans calibration - zero automatique non bloquant");
  DevSerial.print("[GYRO] Sensibilite niveau "); DevSerial.print(sensitivityLevel);
  DevSerial.print(" - seuil mouvement "); DevSerial.print(thresholdForLevel(sensitivityLevel), 1); DevSerial.println(" deg/s");
  DevSerial.print("[MODE] Intervalles actif/repos : "); DevSerial.print(activeIntervalSec); DevSerial.print("s / "); DevSerial.print(idleIntervalSec); DevSerial.println("s");
}

void motionUpdate(uint32_t now) {
  if (!gyroAddress) return;
  if (lastSampleAt != 0 && now - lastSampleAt < GYRO_SAMPLE_INTERVAL_MS) return;
  lastSampleAt = now;
  float rawX = 0, rawY = 0, rawZ = 0;
  if (!readGyroDps(rawX, rawY, rawZ)) { DevSerial.println("[GYRO] Erreur lecture LSM6DS3."); return; }

  if (!biasInitialized) {
    biasXDps = rawX; biasYDps = rawY; biasZDps = rawZ; biasInitialized = true;
    state.moving = false; state.stationaryConfirmed = false;
    state.lastMotionAt = now; state.stationarySince = now; motionAboveThresholdSince = 0;
    DevSerial.print("[GYRO] Zero initial X="); DevSerial.print(biasXDps, 3);
    DevSerial.print(" Y="); DevSerial.print(biasYDps, 3); DevSerial.print(" Z="); DevSerial.print(biasZDps, 3); DevSerial.println(" deg/s");
    DevSerial.println("[MODE] Etat initial : IMMOBILE - confirmation 30s");
  }

  const float xDps = rawX - biasXDps, yDps = rawY - biasYDps, zDps = rawZ - biasZDps;
  const float magnitudeDps = sqrtf(xDps*xDps + yDps*yDps + zDps*zDps);
  const float motionThresholdDps = thresholdForLevel(sensitivityLevel);
  if (!state.moving && magnitudeDps < motionThresholdDps * 0.50f) {
    biasXDps += GYRO_BIAS_ADAPT_ALPHA * xDps;
    biasYDps += GYRO_BIAS_ADAPT_ALPHA * yDps;
    biasZDps += GYRO_BIAS_ADAPT_ALPHA * zDps;
  }
  state.xDps = xDps; state.yDps = yDps; state.zDps = zDps;
  state.motionDps = 0.75f * state.motionDps + 0.25f * magnitudeDps;
  const float stillThresholdDps = motionThresholdDps * GYRO_STILL_RATIO;

  if (state.motionDps >= motionThresholdDps) {
    if (motionAboveThresholdSince == 0) motionAboveThresholdSince = now;
    if (state.stationaryConfirmed || now - motionAboveThresholdSince >= GYRO_MOTION_CONFIRM_SEC * 1000UL) {
      const bool wasMoving = state.moving;
      state.moving = true; state.stationaryConfirmed = false; state.lastMotionAt = now; state.stationarySince = 0;
      if (!wasMoving) {
        DevSerial.print("[MODE] MOUVEMENT DETECTE - seuil "); DevSerial.print(motionThresholdDps, 1); DevSerial.println(" deg/s - rythme actif");
      }
    }
  } else if (state.moving && state.motionDps <= stillThresholdDps) {
    motionAboveThresholdSince = 0;
    if (state.stationarySince == 0) { state.stationarySince = now; DevSerial.println("[MODE] RETOUR AU CALME - confirmation repos 8s demarree"); }
    if (now - state.stationarySince >= MOTION_IDLE_CONFIRM_SEC * 1000UL) {
      state.moving = false; state.stationaryConfirmed = true;
      DevSerial.print("[MODE] REPOS CONFIRME - envoi toutes les "); DevSerial.print(idleIntervalSec); DevSerial.println("s");
    }
  } else if (!state.moving && state.motionDps < motionThresholdDps) {
    motionAboveThresholdSince = 0;
    if (!state.stationaryConfirmed) {
      if (state.stationarySince == 0) { state.stationarySince = now; DevSerial.println("[MODE] RETOUR AU CALME - confirmation initiale 30s demarree"); }
      if (now - state.stationarySince >= MOTION_INITIAL_CONFIRM_SEC * 1000UL) {
        state.moving = false; state.stationaryConfirmed = true;
        DevSerial.print("[MODE] REPOS CONFIRME - envoi toutes les "); DevSerial.print(idleIntervalSec); DevSerial.println("s");
      }
    }
  }
}

const MotionState& motionState() { return state; }

uint32_t motionStationaryConfirmationRemainingMs() {
  if (!gyroAddress || state.stationaryConfirmed || state.stationarySince == 0) return 0;
  const uint32_t total = state.moving ? MOTION_IDLE_CONFIRM_SEC * 1000UL : MOTION_INITIAL_CONFIRM_SEC * 1000UL;
  const uint32_t elapsed = millis() - state.stationarySince;
  return elapsed >= total ? 0 : total - elapsed;
}

uint32_t currentSendIntervalMs() {
  if (state.moving || !state.stationaryConfirmed) return activeIntervalSec * 1000UL;
  return idleIntervalSec * 1000UL;
}

uint8_t motionSensitivityLevel() { return sensitivityLevel; }
float motionSensitivityThresholdDps() { return thresholdForLevel(sensitivityLevel); }

bool setMotionSensitivityLevel(uint8_t level) {
  if (level < 1 || level > 5) return false;
  sensitivityLevel = level;
  if (!prefs.putUChar("sensitivity", sensitivityLevel)) return false;
  DevSerial.print("[GYRO] Sensibilite niveau "); DevSerial.print(sensitivityLevel);
  DevSerial.print(" - seuil "); DevSerial.print(thresholdForLevel(sensitivityLevel), 1); DevSerial.println(" deg/s");
  return true;
}

uint32_t motionActiveIntervalSec() { return activeIntervalSec; }
uint32_t motionIdleIntervalSec() { return idleIntervalSec; }

bool setMotionIntervals(uint32_t activeSeconds, uint32_t idleSeconds) {
  if (!allowedActive(activeSeconds) || !allowedIdle(idleSeconds)) return false;
  activeIntervalSec = activeSeconds; idleIntervalSec = idleSeconds;
  const bool ok = prefs.putUInt("active_sec", activeIntervalSec) && prefs.putUInt("idle_sec", idleIntervalSec);
  if (ok) {
    DevSerial.print("[MODE] Nouveaux intervalles actif/repos : "); DevSerial.print(activeIntervalSec); DevSerial.print("s / "); DevSerial.print(idleIntervalSec); DevSerial.println("s");
  }
  return ok;
}

const char* motionModeName() {
  return state.moving ? "MOBILE" : "IMMOBILE";
}

bool motionIsMobile() {
  return state.moving;
}
