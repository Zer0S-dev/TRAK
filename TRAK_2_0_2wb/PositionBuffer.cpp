#include "PositionBuffer.h"
#include <SD.h>
#include <SPI.h>
#include "Config.h"

namespace {

static constexpr char BUFFER_FILE[] = "/trak_buffer.bin";
static constexpr uint32_t BUFFER_MAGIC = 0x5452414BUL; // TRAK
static constexpr uint16_t BUFFER_VERSION = 1;
static constexpr uint32_t BUFFER_CAPACITY = POSITION_BUFFER_CAPACITY;
static constexpr uint32_t HEADER_SLOT_SIZE = 40;
static constexpr uint32_t HEADER_SIZE = HEADER_SLOT_SIZE * 2;
static constexpr uint32_t RECORD_MAGIC = 0x504F5331UL; // POS1

struct BufferHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t headerSize;
  uint32_t capacity;
  uint32_t head;
  uint32_t tail;
  uint32_t count;
  uint32_t sequence;
  uint32_t reserved;
  uint32_t crc;
};

struct BufferRecord {
  uint32_t magic;
  uint32_t sequence;
  BufferedPosition position;
  uint32_t crc;
};

static_assert(sizeof(BufferHeader) <= HEADER_SLOT_SIZE, "BufferHeader too large");

static bool ready = false;
static SPIClass sdSpi(VSPI);
static BufferHeader header{};

uint32_t crc32(const uint8_t* data, size_t length, uint32_t seed = 0xFFFFFFFFUL)
{
  uint32_t crc = seed;
  while (length--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; ++i) {
      const uint32_t mask = -(crc & 1UL);
      crc = (crc >> 1) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

uint32_t headerCrc(const BufferHeader& h)
{
  return crc32(reinterpret_cast<const uint8_t*>(&h), offsetof(BufferHeader, crc));
}

uint32_t recordCrc(const BufferRecord& r)
{
  return crc32(reinterpret_cast<const uint8_t*>(&r), offsetof(BufferRecord, crc));
}

uint32_t recordOffset(uint32_t index)
{
  return HEADER_SIZE + index * sizeof(BufferRecord);
}

bool writeHeader()
{
  header.crc = headerCrc(header);
  File file = SD.open(BUFFER_FILE, "r+");
  if (!file) return false;

  bool ok = file.seek(0);
  if (ok) ok = file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header);

  // Deuxième copie : le header reste récupérable si une coupure survient
  // pendant l'écriture de la première copie.
  if (ok) {
    ok = file.seek(HEADER_SLOT_SIZE);
    if (ok) ok = file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == sizeof(header);
  }

  file.flush();
  file.close();
  return ok;
}

bool readHeader()
{
  File file = SD.open(BUFFER_FILE, FILE_READ);
  if (!file) return false;

  if (file.size() < HEADER_SIZE) {
    file.close();
    return false;
  }

  BufferHeader candidate{};
  bool valid = false;

  for (uint8_t copy = 0; copy < 2 && !valid; ++copy) {
    if (!file.seek(copy * HEADER_SLOT_SIZE)) continue;
    if (file.read(reinterpret_cast<uint8_t*>(&candidate), sizeof(candidate)) != sizeof(candidate)) continue;

    if (candidate.magic == BUFFER_MAGIC &&
        candidate.version == BUFFER_VERSION &&
        candidate.headerSize == HEADER_SIZE &&
        candidate.capacity == BUFFER_CAPACITY &&
        candidate.head < candidate.capacity &&
        candidate.tail < candidate.capacity &&
        candidate.count <= candidate.capacity &&
        candidate.crc == headerCrc(candidate)) {
      header = candidate;
      valid = true;
    }
  }

  file.close();
  return valid;
}

bool initializeNewBuffer()
{
  header = {};
  header.magic = BUFFER_MAGIC;
  header.version = BUFFER_VERSION;
  header.headerSize = HEADER_SIZE;
  header.capacity = BUFFER_CAPACITY;
  header.head = 0;
  header.tail = 0;
  header.count = 0;
  header.sequence = 0;
  header.reserved = 0;

  SD.remove(BUFFER_FILE);
  File file = SD.open(BUFFER_FILE, FILE_WRITE);
  if (!file) return false;

  // Reserve the complete ring file once. This avoids fragmentation and
  // makes random-access slots deterministic after reboot.
  if (!file.seek(HEADER_SIZE + BUFFER_CAPACITY * sizeof(BufferRecord) - 1)) {
    file.close();
    return false;
  }
  const uint8_t zero = 0;
  if (file.write(&zero, 1) != 1) {
    file.close();
    return false;
  }
  file.flush();
  file.close();

  return writeHeader();
}

bool readRecord(uint32_t index, BufferRecord& record)
{
  File file = SD.open(BUFFER_FILE, FILE_READ);
  if (!file) return false;
  if (!file.seek(recordOffset(index))) {
    file.close();
    return false;
  }
  const size_t read = file.read(reinterpret_cast<uint8_t*>(&record), sizeof(record));
  file.close();
  if (read != sizeof(record)) return false;
  if (record.magic != RECORD_MAGIC) return false;
  return record.crc == recordCrc(record);
}

bool writeRecord(uint32_t index, const BufferRecord& record)
{
  File file = SD.open(BUFFER_FILE, "r+");
  if (!file) return false;
  if (!file.seek(recordOffset(index))) {
    file.close();
    return false;
  }
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record));
  file.flush();
  file.close();
  return written == sizeof(record);
}

} // namespace

bool positionBufferBegin()
{
  if (ready) return true;

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS, sdSpi, 20000000)) {
    Serial.println("[SD] ERREUR : initialisation impossible.");
    ready = false;
    return false;
  }

  const uint64_t cardSizeMb = SD.cardSize() / (1024ULL * 1024ULL);
  Serial.print("[SD] Carte OK : ");
  Serial.print((unsigned long long)cardSizeMb);
  Serial.println(" MB");

  if (!readHeader()) {
    Serial.println("[BUFFER] Création du buffer /TRAK_BUFFER.BIN");
    if (!initializeNewBuffer()) {
      Serial.println("[BUFFER] ERREUR : impossible de créer le buffer.");
      ready = false;
      return false;
    }
  }

  ready = true;
  Serial.print("[BUFFER] Prêt : ");
  Serial.print(header.count);
  Serial.print(" / ");
  Serial.print(header.capacity);
  Serial.println(" positions en attente.");
  return true;
}

bool positionBufferIsReady()
{
  return ready;
}

uint32_t positionBufferCount()
{
  return ready ? header.count : 0;
}

uint32_t positionBufferCapacity()
{
  return BUFFER_CAPACITY;
}

bool positionBufferPush(const BufferedPosition& position)
{
  if (!ready) return false;

  if (header.count >= header.capacity) {
    // Ne jamais écraser silencieusement les plus anciennes positions.
    Serial.println("[BUFFER] PLEIN : position ignorée.");
    return false;
  }

  BufferRecord record{};
  record.magic = RECORD_MAGIC;
  record.sequence = ++header.sequence;
  record.position = position;
  record.crc = recordCrc(record);

  if (!writeRecord(header.head, record)) {
    Serial.println("[BUFFER] Erreur écriture position.");
    return false;
  }

  header.head = (header.head + 1) % header.capacity;
  ++header.count;

  if (!writeHeader()) {
    Serial.println("[BUFFER] ERREUR : position écrite mais état header non confirmé.");
    return false;
  }

  return true;
}

bool positionBufferPeek(BufferedPosition& position)
{
  if (!ready || header.count == 0) return false;

  BufferRecord record{};
  if (!readRecord(header.tail, record)) {
    Serial.println("[BUFFER] Entrée invalide/corrompue : réinitialisation du buffer.");
    positionBufferClear();
    return false;
  }

  position = record.position;
  return true;
}

bool positionBufferPop()
{
  if (!ready || header.count == 0) return false;

  header.tail = (header.tail + 1) % header.capacity;
  --header.count;

  return writeHeader();
}

void positionBufferClear()
{
  if (!ready) return;
  header.head = 0;
  header.tail = 0;
  header.count = 0;
  if (!writeHeader()) {
    Serial.println("[BUFFER] Erreur lors de la remise à zéro.");
  }
}
