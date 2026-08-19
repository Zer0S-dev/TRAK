#pragma once

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
// Version firmware affichée par le Dashboard.
static constexpr char FIRMWARE_VERSION[] = "2.0.2wb";

// Mode réseau par défaut au premier démarrage : 1 = Wi-Fi / 0 = 4G.
// Le mode réel est désormais stocké en NVS et modifiable depuis l'interface Web.
#define DEV_MODE 1

// Accès à l'interface Web locale.
#define WEB_USER     "admin"
#define WEB_PASSWORD "trak@dmin"

// --- LilyGO T-Call A7670 V1.0 ---
#define MODEM_RX        25
#define MODEM_TX        26
#define MODEM_PWRKEY     4
#define MODEM_RESET     27
#define BOARD_LED       12

// --- OLED SH1106 ---
#define OLED_SDA        21
#define OLED_SCL        22

// --- SD SLOT ---
#define SD_SCK   18
#define SD_MISO  19
#define SD_MOSI  23
#define SD_CS    13

// --- Position buffer ---
// 8192 positions maximum. Une entrée occupe 32 octets environ.
static constexpr uint32_t POSITION_BUFFER_CAPACITY = 8192;
static constexpr uint32_t BUFFER_FLUSH_INTERVAL_MS = 1000;

struct NetworkAPN {
  const char* mccmnc;
  const char* apn;
};

static const NetworkAPN apnDatabase[] = {
  {"20810", "sl2sfr"},
  {"20809", "sl2sfr"},
  {"20815", "free"},
  {"20801", "orange"},
  {"20802", "orange"},
  {"20820", "ebouygtel.com"}
};

static constexpr size_t apnDatabaseSize =
    sizeof(apnDatabase) / sizeof(apnDatabase[0]);

static constexpr char DEFAULT_APN[] = "orange";

// Destination Trackserver par défaut.
// L'URL réellement utilisée peut être modifiée depuis le Dashboard
// et est conservée en NVS. Le protocole supporté reste HTTP sur port 80.
static constexpr char TRACKSERVER_DEFAULT_BASE[] =
    "http://surlereservoir.fr/trackserver/surledoud/eb5a96a7/";

static constexpr size_t TRACKSERVER_URL_MAX_LEN = 159;
static constexpr size_t TRACKSERVER_HOST_MAX_LEN = 63;
static constexpr size_t TRACKSERVER_PATH_MAX_LEN = 159;

// --- Compteur mensuel de données ---
// Coefficient par défaut pour estimer la consommation opérateur 4G.
static constexpr uint32_t DATA_PLAN_DEFAULT_MB = 150;
static constexpr float DATA_OPERATOR_COEF_DEFAULT = 1.20f;


// --- Timers ---
// GNSS : 1 Hz en mouvement. En stationnaire, le récepteur reste actif
// et conserve son fix chaud ; on espace fortement les requêtes AT.
// Le dernier fix valide reste disponible immédiatement pour les pings.
// IMPORTANT : on ne coupe PAS le GNSS en stationnaire, afin de préserver
// un réveil mouvement quasi instantané sans réacquisition froide.
static constexpr uint32_t GNSS_INTERVAL_MS    = 1000;
static constexpr uint32_t GNSS_STATIONARY_INTERVAL_MS = 30000;
static constexpr uint32_t DISPLAY_INTERVAL_MS = 500;
static constexpr uint32_t DEBUG_INTERVAL_MS   = 10000;
static constexpr uint32_t CONSTELLATION_DEBUG_INTERVAL_MS = 5000;
static constexpr uint32_t SIGNAL_INTERVAL_MS  = 5000;
// En stationnaire, inutile d'interroger le niveau réseau toutes les 5 s.
static constexpr uint32_t SIGNAL_STATIONARY_INTERVAL_MS = 60000;

// --- ADXL337 GY-61 : détection de mouvement X/Y ---
// Montage à plat : Z n'est volontairement PAS utilisé.
static constexpr int ACC_PIN_X = 34;
static constexpr int ACC_PIN_Y = 32;
static constexpr int ACC_PIN_Z = 33; // réservé / diagnostic, non utilisé

// Lecture ADC du mouvement. L'ADXL337 est analogique ; analogReadMilliVolts()
// est utilisé sur ESP32 pour bénéficier de la calibration ADC du core.
static constexpr uint32_t ACC_SAMPLE_INTERVAL_MS = 40; // 25 Hz
static constexpr uint32_t ACC_CALIBRATION_MS = 2000;
static constexpr float ACC_SENSITIVITY_MV_PER_G = 300.0f;

// Hystérésis : seuil d'entrée mouvement > 0.08 g, retour immobile < 0.04 g.
static constexpr float ACC_MOTION_THRESHOLD_G = 0.08f;
static constexpr float ACC_STILL_THRESHOLD_G  = 0.04f;

// Temps sans mouvement avant passage en mode stationnaire.
static constexpr uint32_t MOTION_IDLE_CONFIRM_SEC = 30;

// Une pointe de bruit > seuil mouvement ne suffit pas à annuler la
// confirmation d'immobilité. Le dépassement doit durer ce temps.
static constexpr uint32_t ACC_MOTION_RESET_SEC = 2;

// Envoi normal / stationnaire.
static constexpr uint32_t SEND_INTERVAL_MOVING_SEC = 15;
// 30 = 30 s, 60 = 1 min, 300 = 5 min, 1800 = 30 min, 3600 = 1 h, 0 = aucun ping.
static constexpr uint32_t SEND_INTERVAL_STATIONARY_SEC = 30;

// Compatibilité avec le code v7.1 : intervalle normal.
static constexpr uint32_t SEND_INTERVAL_MS = SEND_INTERVAL_MOVING_SEC * 1000UL;

static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS  = 15000;
// Si le Wi-Fi tombe alors que le tracker est immobile, on évite les
// reconnexions répétées. Un mouvement force de toute façon une vérification.
static constexpr uint32_t WIFI_RETRY_STATIONARY_INTERVAL_MS = 60000;

// v7 - filtres GNSS conservateurs
// alpha élevé = réaction rapide ; alpha faible = lissage plus fort.
static constexpr float SPEED_FILTER_ALPHA = 0.25f;
static constexpr float ALTITUDE_FILTER_ALPHA = 0.20f;

// Seuil sous lequel une vitesse GNSS est considérée comme quasi nulle.
// La vitesse affichée et transmise est alors stabilisée à 0.
static constexpr float SPEED_ZERO_THRESHOLD_KMH = 1.5f;

#endif
