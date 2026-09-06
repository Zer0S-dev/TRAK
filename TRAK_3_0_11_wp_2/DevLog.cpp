#include "DevLog.h"

#if DEV_LOG
#include <SD.h>
#include <time.h>
#include <sys/time.h>
#include "Config.h"
#include "SDMutex.h"
#endif

namespace {
#if DEV_LOG
static constexpr char DEV_LOG_FILE[] = "/trak_dev.log";
static constexpr char DEV_LOG_OLD_FILE[] = "/trak_dev.old.log";
static constexpr uint32_t DEV_LOG_MAX_SIZE = 4UL * 1024UL * 1024UL;

static SemaphoreHandle_t logMutex = nullptr;
static bool ready = false;
static bool lineStart = true;
static char sdLineBuffer[1024];
static size_t sdLineLength = 0;

static bool clockValid = false;

// Nombre de jours écoulés depuis 1970-01-01 (calendrier grégorien).
static int64_t daysFromCivil(int y, unsigned m, unsigned d)
{
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

static String timestampPrefix()
{
  if (clockValid) {
    struct timeval tv{};
    gettimeofday(&tv, nullptr);

    struct tm localTm{};
    localtime_r(&tv.tv_sec, &localTm);

    char prefix[40];
    snprintf(prefix, sizeof(prefix), "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] ",
             localTm.tm_year + 1900,
             localTm.tm_mon + 1,
             localTm.tm_mday,
             localTm.tm_hour,
             localTm.tm_min,
             localTm.tm_sec,
             tv.tv_usec / 1000L);
    return String(prefix);
  }

  // Avant la première synchronisation GNSS, on conserve un fallback monotone.
  const uint32_t ms = millis();
  const uint32_t hours = ms / 3600000UL;
  const uint32_t minutes = (ms / 60000UL) % 60UL;
  const uint32_t seconds = (ms / 1000UL) % 60UL;
  const uint32_t millisPart = ms % 1000UL;

  char prefix[32];
  snprintf(prefix, sizeof(prefix), "[BOOT+%02lu:%02lu:%02lu.%03lu] ",
           (unsigned long)hours,
           (unsigned long)minutes,
           (unsigned long)seconds,
           (unsigned long)millisPart);
  return String(prefix);
}

static void writeLogHeader()
{
  static bool headerWritten = false;
  if (headerWritten || !clockValid) return;

  struct timeval tv{};
  gettimeofday(&tv, nullptr);
  struct tm localTm{};
  localtime_r(&tv.tv_sec, &localTm);

  char header[80];
  snprintf(header, sizeof(header), "\n[DEV_LOG] ===== Log du %02d/%02d/%04d =====\n",
           localTm.tm_mday, localTm.tm_mon + 1, localTm.tm_year + 1900);

  // Header sans timestamp : il doit précéder le bandeau TRAK.
  if (!sdMutexLock(portMAX_DELAY)) return;
  File file = SD.open(DEV_LOG_FILE, FILE_APPEND);
  if (file) {
    file.print(header);
    file.close();
    headerWritten = true;
  }
  sdMutexUnlock();
}

bool lockLog()
{
  return logMutex != nullptr &&
         xSemaphoreTake(logMutex, pdMS_TO_TICKS(100)) == pdTRUE;
}

void unlockLog()
{
  if (logMutex) xSemaphoreGive(logMutex);
}

bool writeSdBuffer(bool forceFlush)
{
  if (sdLineLength == 0) return true;
  if (!forceFlush && sdLineBuffer[sdLineLength - 1] != '\n') return false;
  if (!sdMutexLock(portMAX_DELAY)) return false;

  File file = SD.open(DEV_LOG_FILE, FILE_APPEND);
  if (!file) {
    sdMutexUnlock();
    return false;
  }

  if (lineStart) {
    // Rotation et ecriture sont atomiques vis-a-vis des autres utilisateurs SD.
    File sizeFile = SD.open(DEV_LOG_FILE, FILE_READ);
    if (sizeFile) {
      const size_t size = sizeFile.size();
      sizeFile.close();
      if (size >= DEV_LOG_MAX_SIZE) {
        file.close();
        SD.remove(DEV_LOG_OLD_FILE);
        SD.rename(DEV_LOG_FILE, DEV_LOG_OLD_FILE);
        file = SD.open(DEV_LOG_FILE, FILE_APPEND);
        if (!file) {
          sdMutexUnlock();
          return false;
        }
      }
    }
    file.print(timestampPrefix());
  }

  file.write(reinterpret_cast<const uint8_t*>(sdLineBuffer), sdLineLength);
  file.flush();
  file.close();
  sdMutexUnlock();

  const bool endedLine = sdLineBuffer[sdLineLength - 1] == '\n';
  sdLineLength = 0;
  if (endedLine) lineStart = true;
  else lineStart = false;
  return true;
}

void writeSdByte(uint8_t byte)
{
  if (sdLineLength < sizeof(sdLineBuffer)) {
    sdLineBuffer[sdLineLength++] = static_cast<char>(byte);
  }

  // On ecrit une ligne complete sous le mutex SD. Si un message depasse
  // le buffer, on emet un bloc de continuation pour ne jamais perdre d'octets.
  if (byte == '\n' || sdLineLength == sizeof(sdLineBuffer)) {
    writeSdBuffer(true);
  }
}

#endif
} // namespace

DevLogSerial DevSerial;

void DevLogSerial::begin(unsigned long baud)
{
#if DEV_LOG
  // DEV_LOG=1 : sortie série + journal SD actifs.
  ::Serial.begin(baud);
#else
  // DEV_LOG=0 : mode silencieux, aucun log série.
  (void)baud;
#endif
}

size_t DevLogSerial::write(uint8_t byte)
{
#if DEV_LOG
  // Une seule source de log : chaque message est envoyé en parallèle
  // vers le moniteur série et vers le journal SD.
  const size_t serialWritten = ::Serial.write(byte);

  if (ready && lockLog()) {
    writeSdByte(byte);
    unlockLog();
  }

  return serialWritten;
#else
  // DEV_LOG=0 : aucun log série et aucun accès au journal SD.
  (void)byte;
  return 0;
#endif
}

void devLogBegin()
{
#if DEV_LOG
  if (logMutex == nullptr) {
    logMutex = xSemaphoreCreateMutex();
  }

  if (logMutex == nullptr) {
    ready = false;
    return;
  }

  // La carte SD a déjà été initialisée par positionBufferBegin().
  // On ne refait donc pas SD.begin() ici : on réutilise la même carte/SPI.
  if (!sdMutexLock(portMAX_DELAY)) {
    ready = false;
    return;
  }
  const bool cardOk = SD.cardType() && SD.cardType() != CARD_NONE;
  sdMutexUnlock();
  if (!cardOk) {
    ready = false;
    return;
  }

  ready = true;
  lineStart = true;

  // Le bandeau daté est écrit dès que l'heure GNSS est connue.
  writeLogHeader();
#else
  // DEV_LOG=0 : aucun accès SD supplémentaire.
#endif
}

void devLogSyncTime(uint16_t year, uint8_t month, uint8_t day,
                    uint8_t hour, uint8_t minute, uint8_t second, uint16_t millisPart)
{
#if DEV_LOG
  if (!ready || !lockLog()) return;
  if (year < 2020 || month < 1 || month > 12 || day < 1 || day > 31 ||
      hour > 23 || minute > 59 || second > 59 || millisPart > 999) {
    unlockLog();
    return;
  }

  // Europe/Paris : CET en hiver, CEST en été.
  setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
  tzset();

  const int64_t epoch = daysFromCivil(year, month, day) * 86400LL +
                        hour * 3600LL + minute * 60LL + second;
  struct timeval tv{};
  tv.tv_sec = static_cast<time_t>(epoch);
  tv.tv_usec = millisPart * 1000L;
  settimeofday(&tv, nullptr);
  clockValid = true;

  writeLogHeader();
  unlockLog();
#else
  (void)year; (void)month; (void)day; (void)hour; (void)minute; (void)second; (void)millisPart;
#endif
}

bool devLogReady()
{
#if DEV_LOG
  return ready;
#else
  return false;
#endif
}

void devLogFlush()
{
#if DEV_LOG
  if (!ready || !lockLog()) return;

  if (sdLineLength > 0) {
    writeSdBuffer(true);
  }

  if (sdMutexLock(portMAX_DELAY)) {
    File file = SD.open(DEV_LOG_FILE, FILE_APPEND);
    if (file) {
      file.flush();
      file.close();
    }
    sdMutexUnlock();
  }

  unlockLog();
#endif
}
