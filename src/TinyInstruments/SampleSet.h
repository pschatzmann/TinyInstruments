#pragma once
#include <stdint.h>

#if defined(ARDUINO) && !defined(PROGMEM) && __has_include(<pgmspace.h>)
#  include <pgmspace.h>
#endif

/// Marks constant sample data to be stored in flash. On 32 bit
/// microcontrollers like the ESP32 or RP2040 const data is placed in flash
/// anyway and can be accessed directly.
#if defined(PROGMEM)
#  define TI_PROGMEM PROGMEM
#else
#  define TI_PROGMEM
#endif

namespace tinyinstruments {

/**
 * @brief A single sample zone of a multisample instrument: IMA ADPCM
 * encoded (4 bits per sample, low nibble first) mono data with a sustain loop.
 * The decay inside of the loop has been removed by the converter and is
 * re-applied by the player with decayLn.
 */
struct SampleZone {
  const uint8_t* data;   ///< IMA ADPCM data
  uint32_t length;       ///< number of samples
  uint32_t loopStart;    ///< first sample of the loop
  uint32_t loopEnd;      ///< first sample after the loop (== length if looped)
  int16_t loopPredictor; ///< ADPCM decoder state before loopStart
  uint8_t loopIndex;     ///< ADPCM decoder state before loopStart
  uint32_t decayStart;   ///< sample from which the decay envelope is applied
  float decayLn;         ///< decay: ln(amplitude) per sample (negative)
  uint8_t rootKey;       ///< midi note of the recording
  uint8_t loKey;         ///< lowest midi note which uses this zone
  uint8_t hiKey;         ///< highest midi note which uses this zone
  uint8_t loVel;         ///< lowest velocity which uses this zone
  uint8_t hiVel;         ///< highest velocity which uses this zone
  uint8_t group;         ///< choke group of this zone (0 = none)
  uint8_t offBy;         ///< this zone is silenced when a zone of this group starts (0 = never)
  int8_t tune;           ///< fine tuning in cents (pitch correction of the recording)
  /// pitch marks for PSOLA pitch shifting (optional): start of each period,
  /// stored as distance to the previous mark (the first one: its position)
  const uint16_t* marks;
  uint16_t markCount;  ///< number of marks (0 = only resampling)

  bool isLooped() const { return loopEnd > loopStart; }
};

/**
 * @brief A complete multisample instrument
 */
struct SampleSet {
  const char* name;
  const SampleZone* zones;
  uint16_t zoneCount;
  uint32_t sampleRate;  ///< sample rate of the recorded data
  uint32_t dataSize;    ///< total bytes of sample data

  /// Finds the zone for the indicated note and velocity (nullptr if none).
  /// If exact is false, notes outside of the key ranges use the closest zone.
  const SampleZone* findZone(uint8_t note, uint8_t velocity, bool exact = false) const {
    const SampleZone* best = nullptr;
    int bestDistance = 1000;
    for (int j = 0; j < zoneCount; j++) {
      const SampleZone& z = zones[j];
      if (velocity < z.loVel || velocity > z.hiVel) continue;
      if (note >= z.loKey && note <= z.hiKey) return &z;
      if (exact) continue;
      // outside of the defined key range: use the closest zone
      int distance = note < z.loKey ? z.loKey - note : note - z.hiKey;
      if (distance < bestDistance) {
        bestDistance = distance;
        best = &z;
      }
    }
    return best;
  }
};

}  // namespace tinyinstruments
