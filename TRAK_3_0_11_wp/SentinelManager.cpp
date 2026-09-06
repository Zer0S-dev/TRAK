#include "SentinelManager.h"
#include "Config.h"
#include "DevLog.h"
#include <Preferences.h>

namespace {
Preferences prefs;
SemaphoreHandle_t mutex = nullptr;

SentinelState state = SentinelState::OFF;
String userPhone;
String trakPhone;
uint32_t stateStartedAt = 0;
bool motionObservedStationary = false;
bool previousMoving = false;
bool alarmSentForCurrentMovement = false;

static bool lock(uint32_t timeoutMs = 100) {
  return mutex == nullptr || xSemaphoreTake(mutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

static void unlock() {
  if (mutex != nullptr) xSemaphoreGive(mutex);
}

static bool validPhone(const char* phone) {
  if (phone == nullptr) return false;
  size_t len = 0;
  bool hasDigit = false;
  for (const char* p = phone; *p; ++p) {
    if (++len > SENTINEL_PHONE_MAX_LEN) return false;
    if (*p >= '0' && *p <= '9') hasDigit = true;
    else if (*p != '+' && *p != ' ' && *p != '-' && *p != '(' && *p != ')')
      return false;
  }
  return hasDigit && len >= 8;
}

static String normalizePhone(const char* phone) {
  String out;
  if (phone == nullptr) return out;
  for (const char* p = phone; *p; ++p) {
    if ((*p >= '0' && *p <= '9') || *p == '+') out += *p;
  }
  return out;
}

static bool sendSentinelSms(const String& text) {
  String phone;
  if (!lock()) return false;
  phone = userPhone;
  unlock();

  if (phone.length() == 0) return false;
  DevSerial.print("[SENTINEL] SMS longueur : ");
  DevSerial.print(text.length());
  DevSerial.println(" caracteres.");
  return sendSMS(phone.c_str(), text.c_str());
}

static void setStateUnlocked(SentinelState newState) {
  state = newState;
  stateStartedAt = millis();
  prefs.putUChar("sentstate", static_cast<uint8_t>(state));
}

static void timeoutCheck(uint32_t now) {
  if (state == SentinelState::ARMING &&
      now - stateStartedAt >= SENTINEL_CONFIRMATION_TIMEOUT_MS) {
    const SentinelState previous = SentinelState::OFF;
    state = previous;
    stateStartedAt = now;
    prefs.putUChar("sentstate", static_cast<uint8_t>(state));
    DevSerial.println("[SENTINEL] Confirmation expiree : retour a l'etat precedent.");
  }
}
}

void sentinelBegin() {
  mutex = xSemaphoreCreateMutex();
  prefs.begin("sentinel", false);

  char stored[SENTINEL_PHONE_MAX_LEN + 1] = {};
  prefs.getString("phone", "").toCharArray(stored, sizeof(stored));
  userPhone = normalizePhone(stored);

  char storedTrakPhone[SENTINEL_PHONE_MAX_LEN + 1] = {};
  prefs.getString("trakphone", "").toCharArray(storedTrakPhone, sizeof(storedTrakPhone));
  trakPhone = normalizePhone(storedTrakPhone);

  const uint8_t storedState = prefs.getUChar("sentstate", 0);
  state = storedState <= static_cast<uint8_t>(SentinelState::DISARMING)
      ? static_cast<SentinelState>(storedState)
      : SentinelState::OFF;

  // Les etats intermediaires ne survivent pas a un reboot.
  if (state == SentinelState::ARMING || state == SentinelState::DISARMING) {
    state = (state == SentinelState::ARMING) ? SentinelState::OFF : SentinelState::ON;
    prefs.putUChar("sentstate", static_cast<uint8_t>(state));
  }

  stateStartedAt = millis();
  motionObservedStationary = false;
  previousMoving = false;
  alarmSentForCurrentMovement = false;

  DevSerial.print("[SENTINEL] Numero utilisateur : ");
  DevSerial.println(userPhone.length() ? "configure" : "absent");
  DevSerial.print("[SENTINEL] Numero TRAK : ");
  DevSerial.println(trakPhone.length() ? "configure" : "absent");
  DevSerial.print("[SENTINEL] Etat : ");
  DevSerial.println(sentinelStateName());
}

bool sentinelSetUserPhone(const char* phone) {
  if (!validPhone(phone)) return false;
  const String normalized = normalizePhone(phone);
  if (normalized.length() < 8 || normalized.length() > SENTINEL_PHONE_MAX_LEN) return false;

  if (!lock()) return false;
  if (state != SentinelState::OFF) {
    unlock();
    return false;
  }
  userPhone = normalized;
  const size_t written = prefs.putString("phone", userPhone);
  String verify = prefs.getString("phone", "");
  const bool ok = written > 0 && normalizePhone(verify.c_str()) == userPhone;
  DevSerial.print("[SENTINEL] NVS phone: ");
  DevSerial.println(ok ? "OK" : "ECHEC");
  unlock();

  if (!ok) return false;

  const String msg =
      "TRAK : votre numero utilisateur a ete enregistre. Sentinel est pret.";
  const bool smsOk = sendSMS(userPhone.c_str(), msg.c_str());
  DevSerial.println(smsOk
      ? "[SENTINEL] SMS confirmation numero envoye."
      : "[SENTINEL] Echec SMS confirmation numero.");
  return smsOk;
}

bool sentinelClearUserPhone() {
  if (!lock()) return false;
  if (state != SentinelState::OFF) {
    unlock();
    DevSerial.println("[SENTINEL] Suppression refusee : Sentinel doit etre OFF.");
    return false;
  }

  const bool ok = prefs.remove("phone");
  userPhone = "";
  DevSerial.println(ok ? "[SENTINEL] Numero utilisateur supprime de la NVS."
                        : "[SENTINEL] Echec suppression numero utilisateur NVS.");
  unlock();
  return ok;
}

bool sentinelGetUserPhone(char* out, size_t outSize) {
  if (out == nullptr || outSize == 0 || !lock()) return false;
  userPhone.toCharArray(out, outSize);
  unlock();
  return true;
}

bool sentinelHasUserPhone() {
  if (!lock()) return false;
  const bool ok = userPhone.length() > 0;
  unlock();
  return ok;
}

bool sentinelSetTrakPhone(const char* phone) {
  if (!validPhone(phone)) return false;
  const String normalized = normalizePhone(phone);
  if (normalized.length() < 8 || normalized.length() > SENTINEL_PHONE_MAX_LEN) return false;

  if (!lock()) return false;
  if (state != SentinelState::OFF) {
    unlock();
    DevSerial.println("[SENTINEL] Modification numero TRAK refusee : Sentinel doit etre OFF.");
    return false;
  }
  trakPhone = normalized;
  const size_t written = prefs.putString("trakphone", trakPhone);
  String verify = prefs.getString("trakphone", "");
  const bool ok = written > 0 && normalizePhone(verify.c_str()) == trakPhone;
  DevSerial.print("[SENTINEL] NVS phone TRAK: ");
  DevSerial.println(ok ? "OK" : "ECHEC");
  unlock();

  return ok;
}

bool sentinelClearTrakPhone() {
  if (!lock()) return false;
  if (state != SentinelState::OFF) {
    unlock();
    DevSerial.println("[SENTINEL] Suppression numero TRAK refusee : Sentinel doit etre OFF.");
    return false;
  }
  const bool ok = prefs.remove("trakphone");
  trakPhone = "";
  DevSerial.println(ok ? "[SENTINEL] Numero TRAK supprime de la NVS."
                        : "[SENTINEL] Echec suppression numero TRAK NVS.");
  unlock();
  return ok;
}

bool sentinelGetTrakPhone(char* out, size_t outSize) {
  if (out == nullptr || outSize == 0 || !lock()) return false;
  trakPhone.toCharArray(out, outSize);
  unlock();
  return true;
}

bool sentinelHasTrakPhone() {
  if (!lock()) return false;
  const bool ok = trakPhone.length() > 0;
  unlock();
  return ok;
}

SentinelState sentinelState() {
  if (!lock()) return SentinelState::OFF;
  const SentinelState result = state;
  unlock();
  return result;
}

const char* sentinelStateName() {
  switch (sentinelState()) {
    case SentinelState::ARMING: return "ARMING";
    case SentinelState::ON: return "ON";
    case SentinelState::DISARMING: return "DISARMING";
    default: return "OFF";
  }
}

uint32_t sentinelConfirmationRemainingMs() {
  if (!lock()) return 0;
  const SentinelState current = state;
  const uint32_t started = stateStartedAt;
  unlock();

  if (current != SentinelState::ARMING)
    return 0;

  const uint32_t elapsed = millis() - started;
  return elapsed >= SENTINEL_CONFIRMATION_TIMEOUT_MS
      ? 0
      : SENTINEL_CONFIRMATION_TIMEOUT_MS - elapsed;
}

bool sentinelAdvance() {
  if (!sentinelHasUserPhone()) {
    DevSerial.println("[SENTINEL] Numero utilisateur absent.");
    return false;
  }

  const SentinelState current = sentinelState();

  if (current == SentinelState::OFF) {
    const MotionState& motion = motionState();
    if (!motion.calibrated || !motion.stationaryConfirmed || motion.moving) {
      DevSerial.println("[SENTINEL] Activation refusee : LSM6DS3 non confirme immobile.");
      return false;
    }

    const String msg =
        "TRAK SENTINEL : activation demandee. Verifiez la position et le reseau, "
        "puis confirmez dans l application sous 20 secondes.";
    DevSerial.print("[SENTINEL] SMS activation longueur : ");
    DevSerial.print(msg.length());
    DevSerial.println(" caracteres.");
    if (!sendSentinelSms(msg)) {
      DevSerial.println("[SENTINEL] Activation refusee : SMS non envoye.");
      return false;
    }

    if (!lock()) return false;
    setStateUnlocked(SentinelState::ARMING);
    unlock();
    DevSerial.println("[SENTINEL] Etat OFF -> ARMING.");
    return true;
  }

  if (current == SentinelState::ARMING) {
    const uint32_t remaining = sentinelConfirmationRemainingMs();
    if (remaining == 0) {
      timeoutCheck(millis());
      return false;
    }

    const MotionState& motion = motionState();
    if (!motion.calibrated || !motion.stationaryConfirmed || motion.moving) {
      DevSerial.println("[SENTINEL] Activation refusee : LSM6DS3 non confirme immobile.");
      return false;
    }

    const String msg =
        "TRAK SENTINEL : Sentinel est maintenant ACTIVE. "
        "La TRAK Box est sous surveillance.";
    if (!sendSentinelSms(msg)) {
      DevSerial.println("[SENTINEL] Activation refusee : SMS de confirmation non envoye.");
      return false;
    }

    if (!lock()) return false;
    setStateUnlocked(SentinelState::ON);
    unlock();

    motionObservedStationary = true;
    previousMoving = false;
    alarmSentForCurrentMovement = false;

    DevSerial.println("[SENTINEL] Etat ARMING -> ON.");
    return true;
  }

  if (current == SentinelState::ON) {
    const String msg =
        "TRAK SENTINEL : vous allez desactiver le mode Sentinel. "
        "Confirmez la desactivation dans l'application dans les 20 secondes.";
    if (!sendSentinelSms(msg)) {
      DevSerial.println("[SENTINEL] Desactivation refusee : SMS non envoye.");
      return false;
    }

    if (!lock()) return false;
    setStateUnlocked(SentinelState::DISARMING);
    unlock();
    DevSerial.println("[SENTINEL] Etat ON -> DISARMING.");
    return true;
  }

  if (current == SentinelState::DISARMING) {
    // La desactivation ne depend d'aucun compte a rebours.
    // Une fois DISARMING affiche, l'utilisateur peut confirmer quand il le souhaite.
    const String msg =
        "TRAK SENTINEL : Sentinel est maintenant DESACTIVE.";
    if (!sendSentinelSms(msg)) {
      DevSerial.println("[SENTINEL] Desactivation refusee : SMS de confirmation non envoye.");
      return false;
    }

    if (!lock()) return false;
    setStateUnlocked(SentinelState::OFF);
    unlock();

    motionObservedStationary = false;
    previousMoving = false;
    alarmSentForCurrentMovement = false;

    DevSerial.println("[SENTINEL] Etat DISARMING -> OFF.");
    return true;
  }

  return false;
}

bool sentinelSendPositionSms(const GnssData& gps)
{
  String msg = "ALERTE SENTINEL TRAK : mouvement detecte.";
  if (gps.hasFix) {
    msg += " Position : ";
    msg += String(gps.latitude, 6);
    msg += ",";
    msg += String(gps.longitude, 6);
    msg += " Carte : https://maps.google.com/?q=";
    msg += String(gps.latitude, 6);
    msg += ",";
    msg += String(gps.longitude, 6);
    msg += " Vitesse : ";
    msg += String(gps.speedKmh, 1);
    msg += " km/h.";
  } else {
    msg = "TRAK: Position indisponible, pas de fix GPS";
  }

  return sendSentinelSms(msg);
}

void sentinelUpdate(uint32_t now, const GnssData& gps, const MotionState& motion) {
  timeoutCheck(now);

  if (sentinelState() != SentinelState::ON) return;
  if (!motion.calibrated) return;

  if (!motion.moving) {
    motionObservedStationary = true;
    previousMoving = false;
    alarmSentForCurrentMovement = false;
    return;
  }

  if (!motionObservedStationary) {
    previousMoving = true;
    return;
  }

  const bool risingMovement = !previousMoving && motion.moving;
  previousMoving = motion.moving;

  if (!risingMovement || alarmSentForCurrentMovement) return;

  alarmSentForCurrentMovement = true;

  if (!sentinelSendPositionSms(gps)) {
    alarmSentForCurrentMovement = false;
    DevSerial.println("[SENTINEL] Echec SMS alerte mouvement.");
  } else {
    DevSerial.println("[SENTINEL] ALERTE mouvement envoyee.");
  }
}
