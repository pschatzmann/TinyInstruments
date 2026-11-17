#pragma once
#include <math.h>
#include <stdint.h>

#include "ChannelEffect.h"
#include "Config.h"

TI_OPTIMIZE_BEGIN

namespace tinyinstruments {

/**
 * @brief Tremolo of an electric piano: the built-in "vibrato" of the
 * Wurlitzer (amplitude modulation), or the stereo auto pan of a Rhodes
 * suitcase piano. Also usable for guitars and organs.
 *
 * Midi control changes (on the channels which use the effect):
 * | CC | |
 * |---|---|
 * | 1 (modulation wheel) | depth |
 * | 76 | rate (1 - 10 Hz) |
 */
class TremoloEffect : public ChannelEffect {
 public:
  /**
   * @param rateHz speed of the modulation (Wurlitzer: ~5.5 Hz)
   * @param depth 0 (off) .. 1 (full)
   * @param stereo false: both channels together (tremolo), true: left and
   * right in opposite phase (auto pan)
   */
  explicit TremoloEffect(float rateHz = 5.5f, float depth = 0.4f, bool stereo = false)
      : rate_hz(rateHz), depth(depth), stereo(stereo) {}

  void setRate(float hz) { rate_hz = hz < 0.1f ? 0.1f : hz; }
  float rate() const { return rate_hz; }
  void setDepth(float value) { depth = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }
  float getDepth() const { return depth; }
  void setStereo(bool value) { stereo = value; }

  void begin(uint32_t sampleRate) override { sample_rate = (float)sampleRate; }

  void controlChange(uint8_t controller, uint8_t value) override {
    if (controller == 1) setDepth(value / 127.0f);
    if (controller == 76) setRate(1.0f + 9.0f * value / 127.0f);
  }

  void process(float* left, float* right, int frames) override {
    if (depth <= 0.0f) return;
    // the gain is interpolated linearly inside of the block: the lfo is slow
    float inc = 6.2831853f * rate_hz / sample_rate * frames;
    float next_phase = phase + inc;
    if (next_phase > 6.2831853f) next_phase -= 6.2831853f;
    float s0 = sinf(phase), s1 = sinf(next_phase);
    float l0 = gain(s0), l1 = gain(s1);
    float r0 = stereo ? gain(-s0) : l0, r1 = stereo ? gain(-s1) : l1;
    float dl = (l1 - l0) / frames, dr = (r1 - r0) / frames;
    for (int j = 0; j < frames; j++) {
      left[j] *= l0;
      right[j] *= r0;
      l0 += dl;
      r0 += dr;
    }
    phase = next_phase;
  }

 protected:
  float rate_hz;
  float depth;
  bool stereo;
  float sample_rate = 44100.0f;
  float phase = 0.0f;

  /// gain 1 - depth .. 1 for the lfo value -1 .. 1
  inline float gain(float lfo) const { return 1.0f - depth * 0.5f * (1.0f - lfo); }
};

}  // namespace tinyinstruments

TI_OPTIMIZE_END
