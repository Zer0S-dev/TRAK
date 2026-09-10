#include "PositionBuffer.h"
#include <SPI.h>
#include <SD.h>
#include <time.h>
#include <math.h>
#include <string.h>

extern void devLog(const String& message);

PositionBuffer::PositionBuffer()
  : path("/buffer/positions.dat"),
    nextSequence(1),
    head(0),
    tail(0),
    count(0),
    ready(false) {}

bool PositionBuffer::ensureFile() {
  if (!SD.exists("/buffer")) {
    if (!SD.mkdir("/buffer")) {
      Serial.println("[BUFFER] Impossible de creer /buffer sur SD.");
      return false;
    }
  }

  File file = SD.open(path, FILE_READ);
  if (!file) {
    file = SD.open(path, FILE_WRITE);
    if (!file) {
      Serial.println("[BUFFER] Impossible de creer positions.dat.");
      return false;
    }
    const uint32_t totalSize = static_cast<uint32_t>(POSITION_BUFFER_CAPACITY * RECORD_SIZE);
    if (!file.seek(totalSize - 1)) {
      file.close();
      return false;
    }
    file.write((uint8_t)0);
    file.flush();
    file.close();
    return true;
  }

  const uint32_t expected = static_cast<uint32_t>(POSITION_BUFFER_CAPACITY * RECORD_SIZE);
  const uint32_t actual = file.size();
  file.close();
  if (actual != expected) {
    Serial.printf("[BUFFER] Taille positions.dat invalide: %u, attendu=%u. Reinitialisation.\n",
                  (unsigned)actual, (unsigned)expected);
    SD.remove(path);
    return ensureFile();
  }
  return true;
}

uint32_t PositionBuffer::calculateCrc(const DiskRecord& record) const {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(&record);
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0; i < offsetof(DiskRecord, crc32); ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1U)));
    }
  }
  return ~crc;
}

bool PositionBuffer::validRecord(const DiskRecord& record) const {
  return record.magic == RECORD_MAGIC &&
         record.version == RECORD_VERSION &&
         record.crc32 == calculateCrc(record) &&
         record.timestampEpoch != 0;
}

bool PositionBuffer::readRecord(size_t index, DiskRecord& record) const {
  if (index >= POSITION_BUFFER_CAPACITY) return false;
  File file = SD.open(path, FILE_READ);
  if (!file) return false;
  const uint32_t offset = static_cast<uint32_t>(index * RECORD_SIZE);
  if (!file.seek(offset)) {
    file.close();
    return false;
  }
  const size_t readCount = file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record));
  file.close();
  return readCount == sizeof(record);
}

bool PositionBuffer::writeRecord(size_t index, const DiskRecord& record) {
  if (index >= POSITION_BUFFER_CAPACITY) return false;
  File file = SD.open(path, FILE_WRITE);
  if (!file) return false;
  const uint32_t offset = static_cast<uint32_t>(index * RECORD_SIZE);
  if (!file.seek(offset)) {
    file.close();
    return false;
  }
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record));
  file.flush();
  file.close();
  return written == sizeof(record);
}

bool PositionBuffer::parseIsoUtc(const String& value, int& year, int& month, int& day, int& hour, int& minute, int& second) {
  if (value.length() < 20) return false;
  if (value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' || value[16] != ':') return false;
  year = value.substring(0, 4).toInt();
  month = value.substring(5, 7).toInt();
  day = value.substring(8, 10).toInt();
  hour = value.substring(11, 13).toInt();
  minute = value.substring(14, 16).toInt();
  second = value.substring(17, 19).toInt();
  return year >= 2020 && year <= 2099 && month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
         hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 60;
}

uint32_t PositionBuffer::isoToEpoch(const String& value) {
  int year, month, day, hour, minute, second;
  if (!parseIsoUtc(value, year, month, day, hour, minute, second)) return 0;

  uint32_t days = 0;
  for (int y = 1970; y < year; ++y) {
    days += ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 366U : 365U;
  }
  static const uint16_t before[] = {0,31,59,90,120,151,181,212,243,273,304,334};
  days += before[month - 1];
  if (month > 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) ++days;
  days += static_cast<uint32_t>(day - 1);
  return days * 86400UL + static_cast<uint32_t>(hour) * 3600UL + static_cast<uint32_t>(minute) * 60UL + static_cast<uint32_t>(second);
}

void PositionBuffer::epochToIso(uint32_t epoch, String& output) {
  time_t raw = static_cast<time_t>(epoch);
  struct tm utc;
  gmtime_r(&raw, &utc);
  char value[21];
  snprintf(value, sizeof(value), "%04d-%02d-%02dT%02d:%02d:%02dZ",
           utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
           utc.tm_hour, utc.tm_min, utc.tm_sec);
  output = value;
}

bool PositionBuffer::begin() {
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, SPI, 10000000)) {
    Serial.println("[BUFFER] SD indisponible: FIFO persistent inactive.");
    devLog("ERREUR buffer SD indisponible");
    return false;
  }
  if (!ensureFile()) {
    devLog("ERREUR creation positions.dat");
    return false;
  }

  // Scan the circular file with ONE open handle. The previous implementation
  // opened/closed the SD file 8192 times, which could starve the ESP32 watchdog.
  File file = SD.open(path, FILE_READ);
  if (!file) {
    Serial.println("[BUFFER] Impossible d'ouvrir positions.dat pour restauration.");
    devLog("ERREUR lecture positions.dat");
    return false;
  }

  uint32_t maxSequence = 0;
  size_t validCount = 0;
  size_t newestIndex = 0;
  DiskRecord record;

  for (size_t i = 0; i < POSITION_BUFFER_CAPACITY; ++i) {
    const uint32_t offset = static_cast<uint32_t>(i * RECORD_SIZE);
    bool validRead = file.seek(offset) && file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record)) == sizeof(record);
    if (validRead && validRecord(record)) {
      ++validCount;
      if (validCount == 1 || static_cast<int32_t>(record.sequence - maxSequence) > 0) {
        maxSequence = record.sequence;
        newestIndex = i;
      }
    }

    // Explicitly yield during the startup scan so IDLE0 remains schedulable.
    if ((i & 0x3F) == 0x3F) vTaskDelay(pdMS_TO_TICKS(1));
  }
  file.close();

  if (validCount == 0) {
    head = tail = count = 0;
    nextSequence = 1;
  } else {
    count = validCount > POSITION_BUFFER_CAPACITY ? POSITION_BUFFER_CAPACITY : validCount;
    head = (newestIndex + 1) % POSITION_BUFFER_CAPACITY;
    tail = (head + POSITION_BUFFER_CAPACITY - count) % POSITION_BUFFER_CAPACITY;
    nextSequence = maxSequence + 1;
  }

  ready = true;
  Serial.printf("[BUFFER] SD FIFO pret: %u position(s)\n", (unsigned)count);
  if (count > 0) devLog(String("Buffer restored: count=") + String((unsigned)count));
  return true;
}

bool PositionBuffer::push(const GnssPosition& position) {
  if (!ready || !position.valid) return false;

  DiskRecord record{};
  record.magic = RECORD_MAGIC;
  record.version = RECORD_VERSION;
  record.sequence = nextSequence++;
  record.latitudeE6 = static_cast<int32_t>(round(position.latitude * 1000000.0));
  record.longitudeE6 = static_cast<int32_t>(round(position.longitude * 1000000.0));
  record.altitudeCm = static_cast<int32_t>(round(position.altitude * 100.0));
  record.timestampEpoch = isoToEpoch(position.timestamp);
  if (record.timestampEpoch == 0) return false;
  record.crc32 = calculateCrc(record);

  if (!writeRecord(head, record)) {
    Serial.println("[BUFFER] Erreur ecriture SD.");
    devLog("ERREUR ecriture buffer SD");
    return false;
  }

  head = (head + 1) % POSITION_BUFFER_CAPACITY;
  if (count < POSITION_BUFFER_CAPACITY) {
    ++count;
  } else {
    tail = (tail + 1) % POSITION_BUFFER_CAPACITY;
    devLog(String("Buffer full: oldest overwritten, count=") + String((unsigned)count));
  }
  return true;
}

bool PositionBuffer::peek(GnssPosition& position) const {
  if (!ready || count == 0) return false;
  DiskRecord record;
  if (!readRecord(tail, record) || !validRecord(record)) return false;
  position.valid = true;
  position.latitude = static_cast<double>(record.latitudeE6) / 1000000.0;
  position.longitude = static_cast<double>(record.longitudeE6) / 1000000.0;
  position.altitude = static_cast<double>(record.altitudeCm) / 100.0;
  epochToIso(record.timestampEpoch, position.timestamp);
  return true;
}

bool PositionBuffer::pop() {
  if (!ready || count == 0) return false;
  tail = (tail + 1) % POSITION_BUFFER_CAPACITY;
  --count;
  return true;
}

size_t PositionBuffer::size() const { return count; }
bool PositionBuffer::empty() const { return count == 0; }
bool PositionBuffer::full() const { return count == POSITION_BUFFER_CAPACITY; }
