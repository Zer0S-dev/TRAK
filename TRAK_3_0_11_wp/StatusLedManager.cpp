#include "StatusLedManager.h"

#include "Config.h"

#include <FastLED.h>

namespace {

CRGB leds[STATUS_LED_COUNT];

bool wifiActive = false;
bool cellularActive = false;
bool gpsFix = false;
bool previousGpsFix = false;

}

// ============================================================
// INITIALISATION
// ============================================================

void statusLedBegin() {

  FastLED.addLeds<WS2812, STATUS_LED_DATA, GRB>(
      leds,
      STATUS_LED_COUNT
  );

  FastLED.clear(true);

  wifiActive = false;
  cellularActive = false;
  gpsFix = false;
  previousGpsFix = false;

  ledAnimationReset();

  Serial.println("[LED] CJMCU-2812-7 / 7x WS2812 initialise");
  Serial.println("[LED] DATA GPIO12 | Centre 25% | Ring 12.5%");
}


// ============================================================
// ETAT RESEAU
// ============================================================

void statusLedSetNetwork(bool wifi, bool cellular) {

  wifiActive = wifi;
  cellularActive = cellular;

  // Tant que le GPS n'est pas fixe,
  // la recherche GPS reste prioritaire.
  if (!gpsFix) {

    ledAnimationSetState(LED_STATE_GPS_SEARCH);

  } else if (wifiActive) {

    ledAnimationSetState(LED_STATE_WIFI);

  } else if (cellularActive) {

    ledAnimationSetState(LED_STATE_4G);

  } else {

    ledAnimationSetState(LED_STATE_OFFLINE);
  }
}


// ============================================================
// CONNEXION RESEAU EN COURS
// ============================================================

void statusLedSetNetworkConnecting(
    bool wifiConnecting,
    bool cellularConnecting) {

  // clignote bleu si perte wifi 

 // if (wifiConnecting) {

 //   ledAnimationSetState(LED_STATE_WIFI_CONNECTING);

 // } else if (cellularConnecting) {
  
  if (wifiConnecting) {

    // Si un réseau est déjà actif, la recherche Wi-Fi reste invisible
    // pour l'utilisateur : on conserve la LED du réseau actif.
    if (cellularActive) {
        ledAnimationSetState(LED_STATE_4G);
    } else if (wifiActive) {
        ledAnimationSetState(LED_STATE_WIFI);
    } else {
        ledAnimationSetState(LED_STATE_WIFI_CONNECTING);
    }

} else if (cellularConnecting) {

    ledAnimationSetState(LED_STATE_4G_CONNECTING);

  } else if (wifiActive) {

    ledAnimationSetState(LED_STATE_WIFI);

  } else if (cellularActive) {

    ledAnimationSetState(LED_STATE_4G);

  } else if (gpsFix) {

    ledAnimationSetState(LED_STATE_OFFLINE);

  } else {

    ledAnimationSetState(LED_STATE_GPS_SEARCH);
  }
}


// ============================================================
// GPS FIX
// ============================================================

void statusLedSetGpsFix(bool hasFix) {

  gpsFix = hasFix;

  ledAnimationSetGpsFix(hasFix);

  // Cette fonction est appelée à chaque lecture GNSS.
  // Ne pas redémarrer l'animation à chaque trame :
  // seuls les changements d'état comptent.

  if (gpsFix == previousGpsFix) {
    return;
  }

  if (gpsFix) {

    // Animation de confirmation du premier fix,
    // puis retour à l'état réseau.

    if (wifiActive) {

      ledAnimationSetState(LED_STATE_WIFI);

    } else if (cellularActive) {

      ledAnimationSetState(LED_STATE_4G);

    } else {

      ledAnimationSetState(LED_STATE_OFFLINE);
    }

    ledAnimationSetState(LED_STATE_GPS_FIX);

  } else {

    ledAnimationSetState(LED_STATE_GPS_SEARCH);
  }

  previousGpsFix = gpsFix;
}


// ============================================================
// EVENEMENTS HTTP
// ============================================================

void statusLedPulseWiFi() {

  ledAnimationStartEvent(LED_EVENT_WIFI_SEND);
}


void statusLedPulseCellular() {

  ledAnimationStartEvent(LED_EVENT_4G_SEND);
}


void statusLedHttpStartWiFi() {

  ledAnimationStartEvent(LED_EVENT_WIFI_SEND);
}


void statusLedHttpStartCellular() {

  ledAnimationStartEvent(LED_EVENT_4G_SEND);
}


void statusLedHttpStop() {

  ledAnimationStopEvent();
}


// ============================================================
// SERVICE
// ============================================================

void statusLedService() {

  ledAnimationService(millis());
}


// ============================================================
// PIXEL INDIVIDUEL
// ============================================================

void statusLedSetPixel(
    uint8_t index,
    const CRGB &color) {

  if (index >= STATUS_LED_COUNT) {
    return;
  }

  CRGB scaled = color;

  // LED 0 = centre
  if (index == 0) {

    scaled.nscale8(STATUS_LED_CENTER_BRIGHTNESS);

  }

  // LED 1 à 6 = anneau
  else {

    scaled.nscale8(STATUS_RING_BRIGHTNESS);
  }

  leds[index] = scaled;
}


// ============================================================
// TOUS LES PIXELS
// ============================================================

void statusLedSetAllPixels(
    const CRGB &color) {

  // ----------------------------------------------------------
  // LED 0 : CENTRE
  // ----------------------------------------------------------

  leds[0] = color;

  leds[0].nscale8(
      STATUS_LED_CENTER_BRIGHTNESS
  );


  // ----------------------------------------------------------
  // LED 1 à 6 : ANNEAU
  // ----------------------------------------------------------

  for (uint8_t i = 1; i < STATUS_LED_COUNT; i++) {

    leds[i] = color;

    leds[i].nscale8(
        STATUS_RING_BRIGHTNESS
    );
  }
}


// ============================================================
// EXTINCTION
// ============================================================

void statusLedClearPixels() {

  fill_solid(
      leds,
      STATUS_LED_COUNT,
      CRGB::Black
  );
}


// ============================================================
// AFFICHAGE
// ============================================================

void statusLedShowPixels() {

  FastLED.show();
}