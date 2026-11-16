#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SampleSet.h"

#if defined(ESP32) && __has_include(<esp_heap_caps.h>)
#  include <esp_heap_caps.h>
#  define TI_HAS_PSRAM_ALLOC 1
#else
#  define TI_HAS_PSRAM_ALLOC 0
#endif

namespace tinyinstruments {

/**
 * @brief Memory used for sample data which is loaded at runtime: PSRAM if
 * available (ESP32), otherwise the normal heap.
 */
struct SampleMemory {
  static void* allocate(size_t size) {
#if TI_HAS_PSRAM_ALLOC
    void* result = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (result != nullptr) return result;
#endif
    return malloc(size);
  }
  static void release(void* ptr) {
    // heap_caps_malloc memory can be released with free()
    free(ptr);
  }
};

/**
 * @brief A sample set which is loaded at runtime from a binary file (see
 * tools/sfz2header.py --binary or tools/header2bin.py and the data folder).
 *
 * The data is read from any object which provides
 * size_t readBytes(char* data, size_t len), e.g. an Arduino Stream or File
 * (SD, LittleFS, network client ...). The memory is allocated in PSRAM if
 * available.
 *
 * @code
 * File file = LittleFS.open("/GrandPiano.bin");
 * DynamicSampleSet grand;
 * if (grand.begin(file)) piano = PianoInstrument(grand.sampleSet());
 * @endcode
 */
class DynamicSampleSet {
 public:
  enum Error { Ok, ReadError, InvalidFormat, UnsupportedVersion, OutOfMemory };

  DynamicSampleSet() = default;
  ~DynamicSampleSet() { end(); }
  DynamicSampleSet(const DynamicSampleSet&) = delete;
  DynamicSampleSet& operator=(const DynamicSampleSet&) = delete;

  /// Loads the sample set from the stream: returns false on error (see error())
  template <class STREAM>
  bool begin(STREAM& in) {
    end();
    uint8_t header[17];
    if (!readFully(in, header, sizeof(header))) return fail(ReadError);
    if (memcmp(header, "TISS", 4) != 0) return fail(InvalidFormat);
    uint16_t version = u16(header + 4);
    if (version != 1 && version != 2) return fail(UnsupportedVersion);
    uint16_t zone_count = u16(header + 6);
    uint32_t sample_rate = u32(header + 8);
    uint32_t data_size = u32(header + 12);
    uint8_t name_len = header[16];
    if (zone_count == 0) return fail(InvalidFormat);
    if (!readFully(in, (uint8_t*)name, name_len)) return fail(ReadError);
    name[name_len] = 0;

    p_zones = (SampleZone*)SampleMemory::allocate(sizeof(SampleZone) * zone_count);
    p_data = (uint8_t*)SampleMemory::allocate(data_size);
    if (p_zones == nullptr || p_data == nullptr) return fail(OutOfMemory);

    uint32_t offset = 0;
    for (int j = 0; j < zone_count; j++) {
      uint8_t rec[32];
      if (!readFully(in, rec, sizeof(rec))) return fail(ReadError);
      SampleZone& z = p_zones[j];
      z.length = u32(rec);
      z.loopStart = u32(rec + 4);
      z.loopEnd = u32(rec + 8);
      z.loopPredictor = (int16_t)u16(rec + 12);
      z.loopIndex = rec[14];
      z.rootKey = rec[15];
      z.decayStart = u32(rec + 16);
      uint32_t bits = u32(rec + 20);
      memcpy(&z.decayLn, &bits, sizeof(float));
      z.loKey = rec[24];
      z.hiKey = rec[25];
      z.loVel = rec[26];
      z.hiVel = rec[27];
      z.group = rec[28];
      z.offBy = rec[29];
      z.tune = (int8_t)rec[30];
      z.marks = nullptr;
      z.markCount = 0;
      // the data of the zones follows in the same order
      z.data = p_data + offset;
      offset += (z.length + 1) / 2;
      if (z.loopEnd > z.length || z.loopStart > z.loopEnd) return fail(InvalidFormat);
    }
    if (offset != data_size) return fail(InvalidFormat);
    // version 2: pitch marks for PSOLA
    if (version == 2) {
      uint32_t total = 0;
      for (int j = 0; j < zone_count; j++) {
        uint8_t count[2];
        if (!readFully(in, count, 2)) return fail(ReadError);
        p_zones[j].markCount = u16(count);
        total += p_zones[j].markCount;
      }
      if (total > 0) {
        p_marks = (uint16_t*)SampleMemory::allocate(sizeof(uint16_t) * total);
        if (p_marks == nullptr) return fail(OutOfMemory);
        uint32_t m = 0;
        for (int j = 0; j < zone_count; j++) {
          p_zones[j].marks = p_zones[j].markCount > 0 ? p_marks + m : nullptr;
          uint32_t position = 0;
          for (int k = 0; k < p_zones[j].markCount; k++, m++) {
            uint8_t rec[2];
            if (!readFully(in, rec, sizeof(rec))) return fail(ReadError);
            p_marks[m] = u16(rec);
            position += p_marks[m];
            if (position >= p_zones[j].length) return fail(InvalidFormat);
          }
        }
      }
    }
    if (!readFully(in, p_data, data_size)) return fail(ReadError);

    set.name = name;
    set.zones = p_zones;
    set.zoneCount = zone_count;
    set.sampleRate = sample_rate;
    set.dataSize = data_size;
    error_code = Ok;
    return true;
  }

  /// Copies a sample set (e.g. from flash) into RAM / PSRAM
  bool copyFrom(const SampleSet& source) {
    end();
    if (source.zoneCount == 0) return fail(InvalidFormat);
    p_zones = (SampleZone*)SampleMemory::allocate(sizeof(SampleZone) * source.zoneCount);
    p_data = (uint8_t*)SampleMemory::allocate(source.dataSize);
    uint32_t total_marks = 0;
    for (int j = 0; j < source.zoneCount; j++) total_marks += source.zones[j].markCount;
    if (total_marks > 0) p_marks = (uint16_t*)SampleMemory::allocate(sizeof(uint16_t) * total_marks);
    if (p_zones == nullptr || p_data == nullptr || (total_marks > 0 && p_marks == nullptr))
      return fail(OutOfMemory);
    uint32_t offset = 0, mark_offset = 0;
    for (int j = 0; j < source.zoneCount; j++) {
      p_zones[j] = source.zones[j];
      uint32_t size = (source.zones[j].length + 1) / 2;
      if (offset + size > source.dataSize) return fail(InvalidFormat);
      memcpy(p_data + offset, source.zones[j].data, size);
      p_zones[j].data = p_data + offset;
      offset += size;
      if (source.zones[j].markCount > 0) {
        memcpy(p_marks + mark_offset, source.zones[j].marks, sizeof(uint16_t) * source.zones[j].markCount);
        p_zones[j].marks = p_marks + mark_offset;
        mark_offset += source.zones[j].markCount;
      }
    }
    strncpy(name, source.name, sizeof(name) - 1);
    set = SampleSet{name, p_zones, source.zoneCount, source.sampleRate, source.dataSize};
    error_code = Ok;
    return true;
  }

  /// Releases the memory: instruments using this sample set must not play any more
  void end() {
    if (p_zones) SampleMemory::release(p_zones);
    if (p_data) SampleMemory::release(p_data);
    if (p_marks) SampleMemory::release(p_marks);
    p_zones = nullptr;
    p_data = nullptr;
    p_marks = nullptr;
    set = SampleSet{name, nullptr, 0, 0, 0};
  }

  /// The loaded sample set (to be used with an Instrument)
  const SampleSet& sampleSet() const { return set; }

  bool isLoaded() const { return set.zoneCount > 0; }

  Error error() const { return error_code; }

  const char* errorText() const {
    switch (error_code) {
      case Ok: return "Ok";
      case ReadError: return "ReadError";
      case InvalidFormat: return "InvalidFormat";
      case UnsupportedVersion: return "UnsupportedVersion";
      case OutOfMemory: return "OutOfMemory";
    }
    return "?";
  }

 protected:
  SampleSet set{"", nullptr, 0, 0, 0};
  SampleZone* p_zones = nullptr;
  uint16_t* p_marks = nullptr;
  uint8_t* p_data = nullptr;
  char name[256] = {0};
  Error error_code = Ok;

  bool fail(Error err) {
    end();
    error_code = err;
    return false;
  }

  /// readBytes can return less data (e.g. timeout of a network stream)
  template <class STREAM>
  static bool readFully(STREAM& in, uint8_t* data, size_t len) {
    size_t done = 0;
    int retries = 0;
    while (done < len) {
      // char* is supported by all Arduino streams (e.g. fs::File hides the uint8_t* variant)
      size_t n = in.readBytes((char*)(data + done), len - done);
      if (n == 0) {
        if (++retries > 10) return false;
        continue;
      }
      retries = 0;
      done += n;
    }
    return true;
  }

  static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
  static uint32_t u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  }
};

/**
 * @brief Minimal stream which reads a file with the C standard library, so
 * that DynamicSampleSet can also be used on the desktop.
 */
class StdioFileStream {
 public:
  explicit StdioFileStream(const char* path) { file = fopen(path, "rb"); }
  ~StdioFileStream() {
    if (file) fclose(file);
  }
  operator bool() const { return file != nullptr; }
  size_t readBytes(char* data, size_t len) { return file ? fread(data, 1, len, file) : 0; }

 protected:
  FILE* file = nullptr;
};

}  // namespace tinyinstruments
