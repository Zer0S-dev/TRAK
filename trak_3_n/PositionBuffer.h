#pragma once

#include <Arduino.h>
#include "Config.h"
#include "TrakRuntime.h"

class PositionBuffer {
public:
  PositionBuffer();
  bool begin();
  bool push(const GnssPosition& position);
  bool peek(GnssPosition& position) const;
  bool pop();
  size_t size() const;
  bool empty() const;
  bool full() const;

private:
  static constexpr uint32_t RECORD_MAGIC = 0x5452414BUL;
  static constexpr uint16_t RECORD_VERSION = 1;
  static constexpr size_t RECORD_SIZE = 32;

  struct DiskRecord {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t sequence;
    int32_t latitudeE6;
    int32_t longitudeE6;
    int32_t altitudeCm;
    uint32_t timestampEpoch;
    uint32_t crc32;
  };

  String path;
  uint32_t nextSequence;
  size_t head;
  size_t tail;
  size_t count;
  bool ready;

  bool ensureFile();
  bool readRecord(size_t index, DiskRecord& record) const;
  bool writeRecord(size_t index, const DiskRecord& record);
  bool validRecord(const DiskRecord& record) const;
  uint32_t calculateCrc(const DiskRecord& record) const;
  static bool parseIsoUtc(const String& value, int& year, int& month, int& day, int& hour, int& minute, int& second);
  static uint32_t isoToEpoch(const String& value);
  static void epochToIso(uint32_t epoch, String& output);
};
