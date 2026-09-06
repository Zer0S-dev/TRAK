#pragma once

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
// Version firmware affichée par le Dashboard.
static constexpr char FIRMWARE_VERSION[] = "3.0.11wp";

// Numéro de série matériel du TRAK.
// Dérivé de l'identifiant eFuse unique de l'ESP32 : aucune valeur n'est stockée en NVS.
static inline String trackerSerialNumber() {
  const uint64_t chipId = ESP.getEfuseMac();
  char serial[20];
  snprintf(serial, sizeof(serial), "TRACK-%06X",
           (unsigned int)(chipId & 0xFFFFFFULL));
  return String(serial);
}

// 1 = FULL LOG : SD + Serial, 0 = silencieux
#define DEV_LOG 1  

// Efface toute la NVS au prochain démarrage si activé.
// IMPORTANT : remettre à false après avoir flashé le firmware une fois.
static constexpr bool RESET_NVS = false;

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
// --- CJMCU-2812-7 / WS2812 5050 RGB ---
// Module 7 pixels adressables, piloté par une seule ligne DATA.
// GPIO12 est désormais dédié au DATA du module (les anciennes LED
// discrètes sur GPIO21/12/22 sont supprimées).
#define STATUS_LED_DATA      12
#define STATUS_LED_COUNT      7
#define STATUS_LED_CENTER_BRIGHTNESS 70
#define STATUS_RING_BRIGHTNESS       30

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



// --- Sentinel ---
// Le numero est stocke dans la NVS et utilise pour les SMS utilisateur.
static constexpr size_t SENTINEL_PHONE_MAX_LEN = 20;
static constexpr uint32_t SENTINEL_CONFIRMATION_TIMEOUT_MS = 20000;

// --- Timers ---
// GNSS : 1 Hz en mouvement. En stationnaire, le récepteur reste actif
// et conserve son fix chaud ; on espace fortement les requêtes AT.
// Le dernier fix valide reste disponible immédiatement pour les pings.
// IMPORTANT : on ne coupe PAS le GNSS en stationnaire, afin de préserver
// un réveil mouvement quasi instantané sans réacquisition froide.
static constexpr uint32_t GNSS_INTERVAL_MS    = 1000;
static constexpr uint32_t GNSS_STATIONARY_INTERVAL_MS = 30000;
static constexpr uint32_t DEBUG_INTERVAL_MS   = 10000;
static constexpr uint32_t CONSTELLATION_DEBUG_INTERVAL_MS = 5000;
static constexpr uint32_t SIGNAL_INTERVAL_MS  = 5000;
// En stationnaire, inutile d'interroger le niveau réseau toutes les 5 s.
static constexpr uint32_t SIGNAL_STATIONARY_INTERVAL_MS = 60000;

// --- LSM6DS3 : detection de mouvement ---
// I2C matériel ESP32 utilisé par le LSM6DS3.
static constexpr uint8_t GYRO_I2C_SDA = 21;
static constexpr uint8_t GYRO_I2C_SCL = 22;

// Le LSM6DS3 est configuré à 104 Hz / +/-245 dps.
// La lecture applicative est effectuée à 25 Hz dans MotionManager.
// Il n'y a aucune calibration bloquante au démarrage : le zéro est établi
// automatiquement sur la première mesure valide puis corrigé lentement au repos.

// Temps de mouvement continu nécessaire pour confirmer MOBILE.
static constexpr uint32_t GYRO_MOTION_CONFIRM_SEC = 2;

// Temps sans mouvement avant passage en mode stationnaire.
static constexpr uint32_t MOTION_IDLE_CONFIRM_SEC = 10;

// Compatibilité avec les anciens chemins du firmware.
static constexpr float ACC_MOTION_THRESHOLD_G = 0.08f;
static constexpr float ACC_STILL_THRESHOLD_G  = 0.04f;

// --- Sensibilite gyro (deg/s) ---
// Niveau 3 = 6 deg/s, valeur de référence pour Sentinel.
static constexpr float sensibility_value_1 = 2.0f;
static constexpr float sensibility_value_2 = 4.0f;
static constexpr float sensibility_value_3 = 6.0f;
static constexpr float sensibility_value_4 = 10.0f;
static constexpr float sensibility_value_5 = 15.0f;

// Ancienne constante conservée pour compatibilité de compilation.
// Le nouveau MotionManager utilise les seuils gyro ci-dessus.
static constexpr float GYRO_MOTION_THRESHOLD_DPS = sensibility_value_3;

// Intervalle d'envoi mobile sélectionnable depuis le Dashboard.
// Niveau 1..5 : 5 / 10 / 15 / 20 / 25 secondes.
static constexpr uint32_t SEND_INTERVAL_MOVING_SEC_VALUE_1 = 5;
static constexpr uint32_t SEND_INTERVAL_MOVING_SEC_VALUE_2 = 10;
static constexpr uint32_t SEND_INTERVAL_MOVING_SEC_VALUE_3 = 15;
static constexpr uint32_t SEND_INTERVAL_MOVING_SEC_VALUE_4 = 20;
static constexpr uint32_t SEND_INTERVAL_MOVING_SEC_VALUE_5 = 25;
static constexpr uint8_t SEND_INTERVAL_MOVING_SEC_DEFAULT_LEVEL = 3;

// Valeur historique utilisée pour la compatibilité avec les anciens chemins.
static constexpr uint32_t SEND_INTERVAL_MOVING_SEC = SEND_INTERVAL_MOVING_SEC_VALUE_3;

// 30 = 30 s, 60 = 1 min, 300 = 5 min, 1800 = 30 min, 3600 = 1 h, 0 = aucun ping.
static constexpr uint32_t SEND_INTERVAL_STATIONARY_SEC = 30;

// Compatibilité avec le code v7.1 : intervalle normal.
static constexpr uint32_t SEND_INTERVAL_MS = SEND_INTERVAL_MOVING_SEC * 1000UL;

static constexpr uint32_t WIFI_SCAN_TIMEOUT_MS    = 4000;
// Cooldown minimum entre deux recherches du Wi-Fi préféré lorsque le
// transport actif n'est pas déjà le Wi-Fi. Évite les scans répétitifs.
static constexpr uint32_t WIFI_SCAN_COOLDOWN_MS   = 60000;
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 4000;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS  = 15000; // compatibilité / futur gestionnaire Wi-Fi
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
