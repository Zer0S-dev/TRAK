#include "StatusLedAnimations.h"
#include "StatusLedManager.h"
#include "Config.h"
#include <FastLED.h>

namespace {

static LedState currentState = LED_STATE_STARTUP;
static LedState normalNetworkState = LED_STATE_OFFLINE;
static LedEvent currentEvent = LED_EVENT_NONE;
static uint32_t stateStart = 0;
static uint32_t eventStart = 0;
static uint32_t lastFrame = 0;
static uint8_t step = 0;
static bool gpsFixed = false;

static constexpr uint16_t FRAME_MS = 45;
static constexpr uint16_t STARTUP_MS = 1800;
static constexpr uint16_t GPS_FIX_MS = 1200;
static constexpr uint16_t SEND_MS = 220;
static constexpr uint16_t BUFFER_MS = 500;
static constexpr uint16_t ERROR_MS = 900;
static constexpr uint16_t ALERT_MS = 1800;
static constexpr uint16_t CONNECT_BLINK_MS = 500;
static constexpr uint16_t SEND_BLINK_MS = 110;

static CRGB scale(const CRGB &c, uint8_t amount) {
  CRGB out = c;
  out.nscale8_video(amount);
  return out;
}

static void clear() {
  statusLedClearPixels();
}

static void pixel(uint8_t index, const CRGB &color) {
  if (index < STATUS_LED_COUNT) statusLedSetPixel(index, color);
}

static void all(const CRGB &color) {
  statusLedSetAllPixels(color);
}

static uint8_t pulse(uint32_t elapsed, uint16_t period) {
  const uint16_t phase = elapsed % period;
  const uint16_t half = period / 2;
  const uint16_t value = phase < half ? phase : (period - phase);
  return (uint8_t)map(value, 0, half, 18, 255);
}

static void renderGpsRing(bool fixed) {
  if (!fixed) {
    // Recherche GPS : un point vert tourne sur l'anneau 1..6.
    const uint8_t p = 1 + (step % 6);
    const uint8_t previous = (p == 1) ? 6 : (p - 1);
    const uint8_t previous2 = (previous == 1) ? 6 : (previous - 1);
    pixel(p, CRGB(0, 0, 255));
    pixel(previous, CRGB(0, 0, 255));
    pixel(previous2, CRGB(0, 0, 255));
    return;
  }

  // GPS fixe : anneau vert fixe à 50 %.
  const CRGB gpsGreen = scale(CRGB(0, 0, 255), 128);
  for (uint8_t i = 1; i <= 6; ++i) pixel(i, gpsGreen);
}

static void renderNormalCenter() {
  switch (normalNetworkState) {
    case LED_STATE_WIFI:
      pixel(0, CRGB(0, 255, 30));
      break;
    case LED_STATE_4G:
      pixel(0, CRGB(170, 0, 255));
      break;
    case LED_STATE_OFFLINE:
    default:
      // Aucun réseau : centre éteint.
      break;
  }
}

static void renderNetworkConnecting(bool cellular, uint32_t elapsed) {
  // Connexion/recherche réseau : clignotement lent du centre.
  if (((elapsed / CONNECT_BLINK_MS) % 2) == 0) {
    pixel(0, cellular ? CRGB(170, 0, 255) : CRGB(0, 0, 255));
  }
}

static void animationStartup(uint32_t elapsed) {
  clear();
  const uint8_t p = step % STATUS_LED_COUNT;
  pixel(p, CRGB(255, 90, 0));
  pixel((p + STATUS_LED_COUNT - 1) % STATUS_LED_COUNT, CRGB(70, 25, 0));
  pixel((p + STATUS_LED_COUNT - 2) % STATUS_LED_COUNT, CRGB(18, 7, 0));
  (void)elapsed;
}

static void animationGpsSearch(uint32_t elapsed) {
  clear();
  renderGpsRing(false);
  // Le centre continue d'indiquer le réseau déjà actif, s'il existe.
  renderNormalCenter();
  (void)elapsed;
}

static void animationGpsFix(uint32_t elapsed) {
  clear();
  renderGpsRing(true);
  renderNormalCenter();
  (void)elapsed;
}

static void animationNormalWifi(uint32_t elapsed) {
  clear();
  renderGpsRing(true);
  pixel(0, CRGB(0, 255, 30));
  (void)elapsed;
}

static void animationNormal4G(uint32_t elapsed) {
  clear();
  renderGpsRing(true);
  pixel(0, CRGB(170, 0, 255));
  (void)elapsed;
}

static void animationNormalOffline(uint32_t elapsed) {
  clear();
  renderGpsRing(true);
  (void)elapsed;
}

static void animationWifiConnecting(uint32_t elapsed) {
  clear();
  renderGpsRing(gpsFixed);
  renderNetworkConnecting(false, elapsed);
}

static void animation4GConnecting(uint32_t elapsed) {
  clear();
  renderGpsRing(gpsFixed);
  renderNetworkConnecting(true, elapsed);
}

static void animationSend(bool cellular, uint32_t elapsed) {
  clear();
  renderGpsRing(gpsFixed);

  // Envoi HTTP : clignotement rapide du centre.
  const bool on = ((elapsed / SEND_BLINK_MS) % 2) == 0;
  if (on) {
    pixel(0, cellular ? CRGB(170, 0, 255) : CRGB(0, 0, 255));
  }
}

static void animationBuffering(uint32_t elapsed) {
  clear();
  const uint8_t p = step % 4 + 3;
  pixel(p, scale(CRGB(255, 120, 0), pulse(elapsed, 500)));
}

static void animationError(bool cellular, uint32_t elapsed) {
  clear();
  const CRGB c = cellular ? CRGB(255, 40, 0) : CRGB(0, 80, 255);
  const bool on = ((elapsed / 120) % 2) == 0;
  if (on) for (uint8_t i = 3; i < 7; ++i) pixel(i, c);
}

static void animationAlert(uint32_t elapsed) {
  const bool on = ((elapsed / 150) % 2) == 0;
  all(on ? CRGB(255, 0, 0) : CRGB::Black);
}

static void render(uint32_t now) {
  if (currentEvent != LED_EVENT_NONE) {
    const uint32_t e = now - eventStart;
    switch (currentEvent) {
      case LED_EVENT_WIFI_SEND:       animationSend(false, e); break;
      case LED_EVENT_4G_SEND:         animationSend(true, e); break;
      case LED_EVENT_BUFFERING:       animationBuffering(e); break;
      case LED_EVENT_WIFI_ERROR:      animationError(false, e); break;
      case LED_EVENT_4G_ERROR:        animationError(true, e); break;
      case LED_EVENT_SENTINEL_ALERT:  animationAlert(e); break;
      default: break;
    }

    uint16_t duration = SEND_MS;
    if (currentEvent == LED_EVENT_BUFFERING) duration = BUFFER_MS;
    if (currentEvent == LED_EVENT_WIFI_ERROR || currentEvent == LED_EVENT_4G_ERROR) duration = ERROR_MS;
    if (currentEvent == LED_EVENT_SENTINEL_ALERT) duration = ALERT_MS;

    if (e >= duration) currentEvent = LED_EVENT_NONE;
    statusLedShowPixels();
    ++step;
    return;
  }

  const uint32_t s = now - stateStart;
  switch (currentState) {
    case LED_STATE_STARTUP:
      animationStartup(s);
      if (s >= STARTUP_MS) {
        currentState = LED_STATE_GPS_SEARCH;
        stateStart = now;
      }
      break;

    case LED_STATE_GPS_SEARCH:
      animationGpsSearch(s);
      break;

    case LED_STATE_GPS_FIX:
      animationGpsFix(s);
      if (s >= GPS_FIX_MS) {
        currentState = normalNetworkState;
        stateStart = now;
      }
      break;

    case LED_STATE_WIFI_CONNECTING:
      animationWifiConnecting(s);
      break;

    case LED_STATE_4G_CONNECTING:
      animation4GConnecting(s);
      break;

    case LED_STATE_WIFI:
      animationNormalWifi(s);
      break;

    case LED_STATE_4G:
      animationNormal4G(s);
      break;

    case LED_STATE_OFFLINE:
      animationNormalOffline(s);
      break;
  }
  statusLedShowPixels();
  ++step;
}

} // namespace

void ledAnimationSetState(LedState state) {
  // Les états réseau normaux servent aussi de référence pendant la recherche
  // GPS et la phase de confirmation du fix. Les états de connexion mémorisent
  // également la couleur réseau qui devra rester affichée ensuite.
  if (state == LED_STATE_WIFI || state == LED_STATE_WIFI_CONNECTING) {
    normalNetworkState = LED_STATE_WIFI;
  } else if (state == LED_STATE_4G || state == LED_STATE_4G_CONNECTING) {
    normalNetworkState = LED_STATE_4G;
  } else if (state == LED_STATE_OFFLINE) {
    normalNetworkState = LED_STATE_OFFLINE;
  }

  if (state == currentState && currentEvent == LED_EVENT_NONE) return;

  // Un événement court garde la main jusqu'à sa fin.
  if (currentEvent != LED_EVENT_NONE) return;

  // STARTUP reste prioritaire jusqu'à la fin de son animation.
  if (currentState == LED_STATE_STARTUP && state != LED_STATE_STARTUP) return;

  // La confirmation du premier fix GPS reste prioritaire. Le réseau est
  // mémorisé dans normalNetworkState et sera affiché juste après.
  if (currentState == LED_STATE_GPS_FIX && state != LED_STATE_GPS_FIX) return;

  currentState = state;
  stateStart = millis();
  if (state == LED_STATE_GPS_FIX) step = 0;
}

void ledAnimationSetGpsFix(bool hasFix) {
  gpsFixed = hasFix;
}

void ledAnimationStartEvent(LedEvent event) {
  currentEvent = event;
  eventStart = millis();
  step = 0;
}

void ledAnimationStopEvent() {
  currentEvent = LED_EVENT_NONE;
  eventStart = 0;
}

void ledAnimationService(uint32_t now) {
  if ((uint32_t)(now - lastFrame) < FRAME_MS) return;
  lastFrame = now;
  render(now);
}

void ledAnimationReset() {
  currentState = LED_STATE_STARTUP;
  normalNetworkState = LED_STATE_OFFLINE;
  currentEvent = LED_EVENT_NONE;
  stateStart = millis();
  eventStart = 0;
  lastFrame = 0;
  step = 0;
  gpsFixed = false;
}
