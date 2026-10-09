#pragma once
#define TRAK_VERSION "3.n"
#define DEV_LOG 1

// Persistent SD FIFO capacity (number of position records).
#ifndef POSITION_BUFFER_CAPACITY
#define POSITION_BUFFER_CAPACITY 8192
#endif
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
#ifndef POSITION_BUFFER_CAPACITY
#define POSITION_BUFFER_CAPACITY 8192
#endif
static inline String trackerSerialNumber() {
  const uint64_t chipId = ESP.getEfuseMac();
  char serial[20];
  snprintf(serial, sizeof(serial), "TRACK-%06X", (unsigned int)(chipId & 0xFFFFFFULL));
  return String(serial);
}

static constexpr const char* DEFAULT_APN = "orange";
static constexpr uint32_t GNSS_POLL_MS = 1000;
static constexpr uint32_t SEND_INTERVAL_MS = 5000;
static constexpr uint32_t MODEM_TIMEOUT_MS = 2500;
static constexpr uint32_t DEBUG_BAUD = 115200;
