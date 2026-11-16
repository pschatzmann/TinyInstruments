#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "Biquad.h"
#include "ChannelEffect.h"
#include "Config.h"
#include "DynamicSampleSet.h"

TI_OPTIMIZE_BEGIN

namespace tinyinstruments {

/**
 * @brief Amplifier for the direct (DI) signal of an electric guitar or bass:
 * drive with oversampled, slightly asymmetric soft clipping, tone stack (bass,
 * mid, treble), speaker cabinet simulation, stereo chorus and echo.
 *
 * Midi controllers of the channel:
 * - CC16: drive, CC17: tone (treble), CC18: level
 * - CC19: preset (0-42 clean, 43-85 crunch, 86-127 lead); the Bass preset is
 *   only available in code
 * - CC93: chorus depth, CC91: echo level
 */
class GuitarAmp : public ChannelEffect {
 public:
  enum Preset { Clean, Crunch, Lead, Bass };

  /// Parameters of the amplifier (call update() after changes)
  struct Settings {
    float drive = 0.2f;     ///< 0..1: input gain 0..48 dB
    float bass = 0.0f;      ///< dB
    float mid = 0.0f;       ///< dB
    float treble = 0.0f;    ///< dB
    float level = 0.7f;     ///< output level
    float asymmetry = 0.15f;///< even harmonics (tube character)
    float chorus = 0.0f;    ///< 0..1 chorus mix
    float echo = 0.0f;      ///< 0..1 echo level
    float echoTime = 0.38f; ///< seconds (max 0.5)
    float echoFeedback = 0.35f;
    float lowCut = 85.0f;     ///< Hz: speaker cabinet low cut
    float highCut = 5200.0f;  ///< Hz: speaker cabinet high cut
    float presence = 3.0f;    ///< dB: speaker cabinet presence peak (2.2 kHz)
  };

  explicit GuitarAmp(Preset preset = Crunch) { setPreset(preset); }
  ~GuitarAmp() { freeEcho(); }

  void setPreset(Preset preset) {
    Settings s;
    switch (preset) {
      case Clean:
        s.drive = 0.08f;
        s.bass = 2.0f;
        s.mid = -1.0f;
        s.treble = 2.0f;
        s.level = 0.9f;
        s.chorus = 0.35f;
        s.echo = 0.0f;
        break;
      case Crunch:
        s.drive = 0.45f;
        s.bass = 2.0f;
        s.mid = 2.0f;
        s.treble = 1.0f;
        s.level = 0.7f;
        s.chorus = 0.0f;
        s.echo = 0.0f;
        break;
      case Bass:
        // warm, slightly driven, full low end
        s.drive = 0.15f;
        s.bass = 3.0f;
        s.mid = -2.0f;
        s.treble = 1.0f;
        s.level = 0.9f;
        s.asymmetry = 0.1f;
        s.lowCut = 30.0f;
        s.highCut = 4000.0f;
        s.presence = 1.0f;
        break;
      case Lead:
        s.drive = 0.8f;
        s.bass = 1.0f;
        s.mid = 5.0f;
        s.treble = 0.0f;
        s.level = 0.6f;
        s.chorus = 0.0f;
        s.echo = 0.3f;
        break;
    }
    settings = s;
    update();
  }

  Settings& getSettings() { return settings; }

  /// Applies changed settings
  void update() {
    // 0..48 dB input gain
    pre_gain = powf(10.0f, settings.drive * 48.0f / 20.0f) * 2.0f;
    // the clipping limits the level: compensate the gain for low drive
    post_gain = settings.level / (pre_gain < 1.0f ? 1.0f : sqrtf(pre_gain));
    bias = settings.asymmetry;
    bias_offset = shape(bias);
    if (rate > 0) designFilters();
    if (settings.echo > 0.0f && rate > 0) allocateEcho();
  }

  void begin(uint32_t sampleRate) override {
    rate = sampleRate;
    designFilters();
    chorus_pos = 0;
    memset(chorus_buffer, 0, sizeof(chorus_buffer));
    lfo_phase = 0;
    lfo_inc = 6.2831853f * 0.8f / rate;
    chorus_base = 0.012f * rate;   // 12 ms
    chorus_depth = 0.003f * rate;  // +/- 3 ms
    freeEcho();
    if (settings.echo > 0.0f) allocateEcho();
  }

  void process(float* left, float* right, int frames) override {
    // the chorus lfo is slow: calculate it per block, interpolate linearly
    float mod = 0.0f, mod_inc = 0.0f;
    if (settings.chorus > 0.0f) {
      mod = sinf(lfo_phase);
      lfo_phase += lfo_inc * frames;
      if (lfo_phase > 6.2831853f) lfo_phase -= 6.2831853f;
      mod_inc = (sinf(lfo_phase) - mod) / frames;
    }
    for (int j = 0; j < frames; j++) {
      // mono input
      float x = (left[j] + right[j]) * 0.5f * pre_gain;
      x = input_hp.process(x);

      // 2x oversampled clipping: reduces the aliasing of the distortion
      float mid_sample = 0.5f * (x + last_input);
      last_input = x;
      aa_lp.process(shape(mid_sample + bias) - bias_offset);
      float y = aa_lp.process(shape(x + bias) - bias_offset);

      // tone stack (bands at 0 dB are skipped) and speaker cabinet; the
      // cabinet high pass also removes the DC of the asymmetric clipping
      if (use_bass) y = bass_eq.process(y);
      if (use_mid) y = mid_eq.process(y);
      if (use_treble) y = treble_eq.process(y);
      y = cab_hp.process(y);
      y = cab_peak.process(y);
      y = cab_lp1.process(y);
      y = cab_lp2.process(y) * post_gain;

      float out_l = y, out_r = y;

      // echo (mono, with feedback)
      if (echo_buffer != nullptr && settings.echo > 0.0f) {
        float delayed = echo_buffer[echo_pos] * (1.0f / 32767.0f);
        float fb = y + delayed * settings.echoFeedback;
        if (fb > 1.0f) fb = 1.0f;
        if (fb < -1.0f) fb = -1.0f;
        echo_buffer[echo_pos] = (int16_t)(fb * 32767.0f);
        if (++echo_pos >= echo_len) echo_pos = 0;
        out_l += delayed * settings.echo;
        out_r += delayed * settings.echo;
      }

      // stereo chorus: two modulated delays
      if (settings.chorus > 0.0f) {
        chorus_buffer[chorus_pos] = y;
        float dl = chorusTap(chorus_base + chorus_depth * mod);
        float dr = chorusTap(chorus_base - chorus_depth * mod);
        mod += mod_inc;
        out_l += settings.chorus * dl;
        out_r += settings.chorus * dr;
        chorus_pos = (chorus_pos + 1) & (CHORUS_SIZE - 1);
      }

      left[j] = out_l;
      right[j] = out_r;
    }
  }

  void controlChange(uint8_t controller, uint8_t value) override {
    float v = value / 127.0f;
    switch (controller) {
      case 16:
        settings.drive = v;
        break;
      case 17:
        settings.treble = -12.0f + 24.0f * v;
        break;
      case 18:
        settings.level = v;
        break;
      case 19:
        setPreset(value < 43 ? Clean : (value < 86 ? Crunch : Lead));
        return;
      case 91:
        settings.echo = v;
        break;
      case 93:
        settings.chorus = v;
        break;
      default:
        return;
    }
    update();
  }

  bool hasTail() const override { return echo_buffer != nullptr && settings.echo > 0.0f; }

  /// Soft clipping (tanh approximation): -1..1
  static inline float shape(float x) {
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

 protected:
  static constexpr int CHORUS_SIZE = 2048;  // > 15 ms at 96 kHz
  Settings settings;
  float rate = 0;
  float pre_gain = 1, post_gain = 1, bias = 0, bias_offset = 0;
  float last_input = 0;
  bool use_bass = true, use_mid = true, use_treble = true;
  Biquad input_hp, aa_lp, bass_eq, mid_eq, treble_eq, cab_hp, cab_peak, cab_lp1, cab_lp2;
  float chorus_buffer[CHORUS_SIZE];
  int chorus_pos = 0;
  float lfo_phase = 0, lfo_inc = 0;
  float chorus_base = 0, chorus_depth = 0;
  int16_t* echo_buffer = nullptr;
  int echo_len = 0, echo_pos = 0;

  void designFilters() {
    float r = rate;
    // a tight low end for high drive, but not above the cabinet low cut
    float input_cut = settings.drive > 0.3f ? 120.0f : 60.0f;
    if (input_cut > settings.lowCut) input_cut = settings.lowCut;
    input_hp.highPass(input_cut, 0.7f, r);
    aa_lp.lowPass(0.42f * r, 0.7f, 2.0f * r);  // runs at 2x the rate
    use_bass = settings.bass != 0.0f;
    use_mid = settings.mid != 0.0f;
    use_treble = settings.treble != 0.0f;
    bass_eq.lowShelf(150.0f, settings.bass, r);
    mid_eq.peak(800.0f, 0.8f, settings.mid, r);
    treble_eq.highShelf(3000.0f, settings.treble, r);
    // speaker cabinet: low cut, presence peak, steep high cut
    cab_hp.highPass(settings.lowCut, 0.8f, r);
    cab_peak.peak(2200.0f, 1.2f, settings.presence, r);
    cab_lp1.lowPass(settings.highCut, 0.7f, r);
    cab_lp2.lowPass(settings.highCut, 0.7f, r);
  }

  /// Reads the chorus delay line: delay in samples (> 1), interpolated
  inline float chorusTap(float delay) {
    int whole = (int)delay;
    float frac = delay - whole;
    int i0 = (chorus_pos - whole) & (CHORUS_SIZE - 1);
    int i1 = (i0 - 1) & (CHORUS_SIZE - 1);  // one sample older
    float a = chorus_buffer[i0];
    return a + (chorus_buffer[i1] - a) * frac;
  }

  void allocateEcho() {
    int len = (int)(settings.echoTime * rate);
    if (len < 1) len = 1;
    if (len > (int)(0.5f * rate)) len = (int)(0.5f * rate);
    if (echo_buffer != nullptr && len == echo_len) return;
    freeEcho();
    // 16 bit, in PSRAM if available
    echo_buffer = (int16_t*)SampleMemory::allocate(len * sizeof(int16_t));
    if (echo_buffer == nullptr) return;
    memset(echo_buffer, 0, len * sizeof(int16_t));
    echo_len = len;
    echo_pos = 0;
  }

  void freeEcho() {
    if (echo_buffer) SampleMemory::release(echo_buffer);
    echo_buffer = nullptr;
    echo_len = 0;
  }
};

}  // namespace tinyinstruments

TI_OPTIMIZE_END
