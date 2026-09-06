#pragma once

#include <Arduino.h>

// Etats persistants affiches par les 7 WS2812.
enum LedState : uint8_t {
  LED_STATE_STARTUP = 0,
  LED_STATE_GPS_SEARCH,
  LED_STATE_GPS_FIX,
  LED_STATE_WIFI_CONNECTING,
  LED_STATE_4G_CONNECTING,
  LED_STATE_WIFI,
  LED_STATE_4G,
  LED_STATE_OFFLINE
};

// Evenements courts, prioritaires sur l'etat normal.
enum LedEvent : uint8_t {
  LED_EVENT_NONE = 0,
  LED_EVENT_WIFI_SEND,
  LED_EVENT_4G_SEND,
  LED_EVENT_BUFFERING,
  LED_EVENT_WIFI_ERROR,
  LED_EVENT_4G_ERROR,
  LED_EVENT_SENTINEL_ALERT
};

// API de rendu utilisee par StatusLedManager.
void ledAnimationSetState(LedState state);
void ledAnimationSetGpsFix(bool hasFix);
void ledAnimationStartEvent(LedEvent event);
void ledAnimationStopEvent();
void ledAnimationService(uint32_t now);
void ledAnimationReset();
