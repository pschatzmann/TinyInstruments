#pragma once
#include <stdint.h>

#include "ChannelEffect.h"
#include "Instrument.h"

namespace tinyinstruments {

/**
 * @brief Maps midi program numbers (0..127, e.g. General MIDI) to instruments
 * and optional channel effects. Assigned to a SampleSynthesizer, a program
 * change message selects the instrument of the channel.
 *
 * @code
 * InstrumentBank bank;
 * bank.add(0, 7, piano);                 // General MIDI pianos
 * bank.add(29, 29, guitar, &crunch_amp); // overdriven guitar
 * synth.setInstrumentBank(&bank);
 * @endcode
 */
class InstrumentBank {
 public:
  static constexpr int MAX_ENTRIES = 32;

  struct Entry {
    uint8_t loProgram;
    uint8_t hiProgram;
    const Instrument* instrument;
    ChannelEffect* effect;
  };

  /// Assigns an instrument (and an optional effect) to the programs
  /// loProgram..hiProgram. Later entries take precedence.
  bool add(uint8_t loProgram, uint8_t hiProgram, const Instrument& instrument,
           ChannelEffect* effect = nullptr) {
    if (count >= MAX_ENTRIES || loProgram > hiProgram) return false;
    entries[count++] = Entry{loProgram, hiProgram, &instrument, effect};
    return true;
  }

  /// The entry for the program (nullptr if the program is not defined)
  const Entry* find(uint8_t program) const {
    for (int j = count - 1; j >= 0; j--) {
      if (program >= entries[j].loProgram && program <= entries[j].hiProgram) return &entries[j];
    }
    return nullptr;
  }

  /// General MIDI: program changes on the drum channel (10) select this drum
  /// kit, whatever the program number is
  void setDrumKit(const Instrument& drums, ChannelEffect* effect = nullptr, uint8_t channel = 9) {
    drum_kit = Entry{0, 127, &drums, effect};
    drum_channel = channel;
  }

  /// The entry for a program change on the channel
  const Entry* find(uint8_t channel, uint8_t program) const {
    if (channel == drum_channel && drum_kit.instrument != nullptr) return &drum_kit;
    return find(program);
  }

  int size() const { return count; }
  void clear() {
    count = 0;
    drum_kit.instrument = nullptr;
  }

 protected:
  Entry entries[MAX_ENTRIES];
  int count = 0;
  Entry drum_kit{0, 127, nullptr, nullptr};
  uint8_t drum_channel = 9;
};

}  // namespace tinyinstruments
