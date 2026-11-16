#pragma once
#include <stdint.h>

namespace tinyinstruments {

/**
 * @brief An effect which processes the audio of the midi channels it is
 * assigned to (e.g. a guitar amplifier), before it is mixed with the other
 * channels. The methods are called in the audio task.
 */
class ChannelEffect {
 public:
  virtual ~ChannelEffect() = default;

  /// Called with the output sample rate before the first process() and when
  /// the sample rate changes
  virtual void begin(uint32_t sampleRate) = 0;

  /// Processes the audio of the channel(s) in place
  virtual void process(float* left, float* right, int frames) = 0;

  /// Midi control change of a channel which uses this effect
  virtual void controlChange(uint8_t controller, uint8_t value) {}

  /// true if the effect produces output without input (e.g. echo tail)
  virtual bool hasTail() const { return false; }

  /// Calls begin() if the sample rate has changed
  void setup(uint32_t sampleRate) {
    if (sampleRate != effect_rate) {
      effect_rate = sampleRate;
      begin(sampleRate);
    }
  }

 protected:
  uint32_t effect_rate = 0;
};

}  // namespace tinyinstruments
