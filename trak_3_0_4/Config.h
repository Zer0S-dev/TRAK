#pragma once

// TRAK version
#define TRAK_VERSION "3.0.4"

// Development logging:
// 0 = production: no debug log on Serial and no SD logging.
// 1 = development: full debug log on Serial + technical field events on SD:/dev.log.
#define DEV_LOG 1

// TRAK 3.0 prototype: 4G only, GNSS -> SD FIFO -> REST JSON -> TRAK Connect.
#ifndef MODEM_RX_PIN
#define MODEM_RX_PIN 25
#endif
#ifndef MODEM_TX_PIN
#define MODEM_TX_PIN 26
#endif
#ifndef MODEM_PWRKEY_PIN
#define MODEM_PWRKEY_PIN 4
#endif
#ifndef MODEM_RESET_PIN
#define MODEM_RESET_PIN 27
#endif
#ifndef MODEM_BAUD
#define MODEM_BAUD 115200
#endif

#ifndef WS2812_PIN
#define WS2812_PIN 12
#endif
#ifndef WS2812_RING_COUNT
#define WS2812_RING_COUNT 6
#endif
#ifndef WS2812_CENTER_COUNT
#define WS2812_CENTER_COUNT 1
#endif
#ifndef WS2812_BRIGHTNESS
#define WS2812_BRIGHTNESS 32
#endif

#ifndef SD_CS_PIN
#define SD_CS_PIN 13
#endif
#ifndef SD_SCK_PIN
#define SD_SCK_PIN 18
#endif
#ifndef SD_MISO_PIN
#define SD_MISO_PIN 19
#endif
#ifndef SD_MOSI_PIN
#define SD_MOSI_PIN 23
#endif
#ifndef LSM6DS3_SDA_PIN
#define LSM6DS3_SDA_PIN 21
#endif
#ifndef LSM6DS3_SCL_PIN
#define LSM6DS3_SCL_PIN 22
#endif

// Persistent SD FIFO capacity.
#ifndef POSITION_BUFFER_CAPACITY
#define POSITION_BUFFER_CAPACITY 8192
#endif

static inline String trackerSerialNumber() {
  const uint64_t chipId = ESP.getEfuseMac();
  char serial[20];
  snprintf(serial, sizeof(serial), "TRACK-%06X", (unsigned int)(chipId & 0xFFFFFFULL));
  return String(serial);
}

static constexpr const char* TRAK_CONNECT_URL = "https://surlereservoir.fr/trak/api/trak/position/";
static constexpr const char* TRAK_INTERVAL_URL = "https://surlereservoir.fr/trak/api/trak/interval/";
static constexpr const char* TRAK_API_KEY = "TRAK-PROTO-2026";
static constexpr const char* DEFAULT_APN = "orange";

static constexpr uint32_t GNSS_POLL_MS = 1000;
static constexpr uint32_t SEND_INTERVAL_MS = 5000;
static constexpr uint32_t REMOTE_INTERVAL_POLL_MS = 30000;
static constexpr uint32_t MODEM_TIMEOUT_MS = 2500;
static constexpr uint32_t HTTP_TIMEOUT_MS = 30000;
static constexpr uint32_t DEBUG_BAUD = 115200;

#if !DEV_LOG
class TrakNullSerial {
public:
  void begin(unsigned long) {}
  template <typename T> void print(const T&) {}
  template <typename T> void println(const T&) {}
  template <typename T, typename U> void print(const T&, U) {}
  template <typename T, typename U> void println(const T&, U) {}
  template <typename... Args> void printf(const char*, Args...) {}
};
static TrakNullSerial trakNullSerial;
#define Serial trakNullSerial
#endif
