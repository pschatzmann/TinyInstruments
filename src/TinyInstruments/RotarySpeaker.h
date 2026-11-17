#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "Biquad.h"
#include "ChannelEffect.h"
#include "Config.h"

TI_OPTIMIZE_BEGIN

namespace tinyinstruments {

/**
 * @brief Rotary speaker (Leslie) effect for the TonewheelOrgan, also usable
 * for other instruments (e.g. a guitar or an electric piano).
 *
 * The signal is split at 800 Hz: the treble goes to a rotating horn, the bass
 * to a rotating drum. Each rotor modulates the pitch (Doppler: modulated
 * delay) and the amplitude; two microphones at 90 degrees give the stereo
 * image. Slow (chorale) and fast (tremolo) speed: the horn accelerates within
 * about 1 s, the heavier drum takes a few seconds, which gives the typical
 * speed change. An optional overdrive (tube preamplifier) is in front.
 *
 * Midi control changes (on the channels which use the effect):
 * | CC | |
 * |---|---|
 * | 1 (modulation wheel) | fast (>= 64) / slow |
 * | 26 | drive 0 - 127 |
 * | 27 | rotor stop (brake, >= 64) / run |
 */
class RotarySpeaker : public ChannelEffect {
 public:
  /// drive: overdrive 0 (clean) .. 1
  explicit RotarySpeaker(float drive = 0.0f) { setDrive(drive); }

  /// Slow (false) or fast (true) rotation
  void setFast(bool fast) { is_fast = fast; }
  bool isFast() const { return is_fast; }

  /// Stops the rotors (brake) or lets them run again
  void setStopped(bool stopped) { is_stopped = stopped; }

  /// Overdrive 0 (clean) .. 1
  void setDrive(float value) { drive = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }
  float getDrive() const { return drive; }

  /// Current rotation frequency of the horn and the drum in Hz (e.g. tests)
  float hornSpeed() const { return horn.freq; }
  float drumSpeed() const { return drum.freq; }

  void begin(uint32_t sampleRate) override {
    rate = (float)sampleRate;
    lp1.lowPass(800.0f, 0.7071f, rate);
    lp2.lowPass(800.0f, 0.7071f, rate);
    hp1.highPass(800.0f, 0.7071f, rate);
    hp2.highPass(800.0f, 0.7071f, rate);
    lp1.reset();
    lp2.reset();
    hp1.reset();
    hp2.reset();
    memset(buffer_hi, 0, sizeof(buffer_hi));
    memset(buffer_lo, 0, sizeof(buffer_lo));
    pos = 0;
    // horn: Doppler +/- ~0.35 ms, strong amplitude modulation
    horn.setup(0.8f, 6.8f, 0.9f, 0.6f, rate);
    // drum: less Doppler, softer amplitude modulation, slow acceleration
    drum.setup(0.7f, 5.9f, 4.0f, 5.0f, rate);
    if (horn.freq == 0.0f && !is_stopped) {
      horn.freq = horn.slow;
      drum.freq = drum.slow;
    }
  }

  void controlChange(uint8_t controller, uint8_t value) override {
    if (controller == 1) setFast(value >= 64);
    if (controller == 26) setDrive(value / 127.0f);
    if (controller == 27) setStopped(value >= 64);
  }

  void process(float* left, float* right, int frames) override {
    // rotor speeds: updated once per block
    horn.update(is_stopped ? 0.0f : (is_fast ? horn.fast : horn.slow), frames);
    drum.update(is_stopped ? 0.0f : (is_fast ? drum.fast : drum.slow), frames);
    const float horn_depth = 0.00035f * rate, drum_depth = 0.00012f * rate;
    const float base = 0.001f * rate;  // delay in the middle of the modulation
    float k = drive > 0.0f ? 1.0f + 12.0f * drive : 0.0f;
    float makeup = k > 0.0f ? 1.0f / softClip(k * 0.5f) * 0.5f : 1.0f;

    for (int j = 0; j < frames; j++) {
      float x = 0.5f * (left[j] + right[j]);
      if (k > 0.0f) x = softClip(k * x) * makeup;
      // crossover: 4th order (2 x 2nd order) Linkwitz-Riley
      float lo = lp2.process(lp1.process(x));
      float hi = hp2.process(hp1.process(x));
      buffer_hi[pos] = hi;
      buffer_lo[pos] = lo;

      // horn: two microphones at 90 degrees (sin / cos of the rotor angle)
      float hl = tap(buffer_hi, base + horn_depth * horn.s);
      float hr = tap(buffer_hi, base + horn_depth * horn.c);
      float horn_am_l = 0.7f + 0.3f * horn.c;
      float horn_am_r = 0.7f - 0.3f * horn.s;
      // drum
      float dl = tap(buffer_lo, base + drum_depth * drum.s);
      float dr = tap(buffer_lo, base + drum_depth * drum.c);
      float drum_am_l = 0.85f + 0.15f * drum.c;
      float drum_am_r = 0.85f - 0.15f * drum.s;

      left[j] = hl * horn_am_l + dl * drum_am_l;
      right[j] = hr * horn_am_r + dr * drum_am_r;

      horn.rotate();
      drum.rotate();
      pos = (pos + 1) & MASK;
    }
    horn.normalize();
    drum.normalize();
  }

 protected:
  static constexpr int SIZE = 256;  // delay line: > 2 ms at 96 kHz
  static constexpr int MASK = SIZE - 1;

  /// A rotor: current speed with acceleration, angle as rotating phasor
  struct Rotor {
    float slow = 0.8f, fast = 6.8f;  // Hz
    float freq = 0.0f;               // current speed
    float up = 1.0f, down = 1.0f;    // time constants in seconds
    float rate = 44100.0f;
    float c = 1.0f, s = 0.0f;        // cos / sin of the angle
    float cr = 1.0f, sr = 0.0f;      // rotation per sample

    void setup(float slowHz, float fastHz, float upSeconds, float downSeconds, float sampleRate) {
      slow = slowHz;
      fast = fastHz;
      up = upSeconds;
      down = downSeconds;
      rate = sampleRate;
    }

    void update(float target, int frames) {
      float tau = target > freq ? up : down;
      float k = 1.0f - expf(-(float)frames / (tau * rate));
      freq += k * (target - freq);
      float w = 6.2831853f * freq / rate;
      cr = cosf(w);
      sr = sinf(w);
    }

    inline void rotate() {
      float nc = c * cr - s * sr;
      s = s * cr + c * sr;
      c = nc;
    }

    void normalize() {
      float n = 1.0f / sqrtf(c * c + s * s);
      c *= n;
      s *= n;
    }
  };

  float rate = 44100.0f;
  float drive = 0.0f;
  bool is_fast = false;
  bool is_stopped = false;
  Rotor horn, drum;
  Biquad lp1, lp2, hp1, hp2;
  float buffer_hi[SIZE];
  float buffer_lo[SIZE];
  int pos = 0;

  /// Reads the delay line with linear interpolation (delay in samples)
  inline float tap(const float* buffer, float delay) const {
    int d = (int)delay;
    float frac = delay - d;
    float a = buffer[(pos - d) & MASK];
    float b = buffer[(pos - d - 1) & MASK];
    return a + (b - a) * frac;
  }

  /// Tube like soft clipping (rational tanh approximation)
  static inline float softClip(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }
};

}  // namespace tinyinstruments

TI_OPTIMIZE_END
