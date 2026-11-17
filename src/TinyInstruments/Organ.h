#pragma once
#include <math.h>
#include <stdint.h>

#include "Config.h"

TI_OPTIMIZE_BEGIN

namespace tinyinstruments {

/**
 * @brief Tonewheel organ (Hammond style): the registration (drawbars,
 * percussion, key click) which is shared by all notes of the instrument. The
 * sound is synthesized: no sample data is needed. Use it with
 * OrganInstrument() and a RotarySpeaker effect.
 *
 * The 9 drawbars (16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1') add
 * sine waves (tonewheels) at the equal tempered pitches of the harmonics;
 * the tonewheels above 5.9 kHz fold back an octave like on the original.
 * Each drawbar step is 3 dB.
 *
 * Percussion: a decaying 2nd (4') or 3rd (2 2/3') harmonic, single
 * triggered: only when no other key of the channel is held. As on the
 * original the 1' drawbar is silent while the percussion is on, and the normal
 * (loud) percussion reduces the drawbars by 3 dB.
 *
 * Midi control changes (on the channel of the organ):
 * | CC | |
 * |---|---|
 * | 12 - 20 | drawbars 16' ... 1' (0 - 127 -> 0 - 8) |
 * | 21 | percussion on (>= 64) / off |
 * | 22 | percussion harmonic: 3rd (>= 64) / 2nd |
 * | 23 | percussion decay: fast (>= 64) / slow |
 * | 24 | percussion volume: soft (>= 64) / normal |
 * | 25 | key click amount |
 *
 * The settings can be changed while notes are playing.
 */
class TonewheelOrgan {
 public:
  static constexpr int DRAWBARS = 9;

  /// Registration as 9 digits 0..8, e.g. "888000000" (default)
  explicit TonewheelOrgan(const char* drawbars = "888000000") { setDrawbars(drawbars); }

  /// Sets all drawbars from 9 digits 0..8, e.g. "888000000"
  void setDrawbars(const char* drawbars) {
    for (int j = 0; j < DRAWBARS; j++) {
      char c = drawbars != nullptr ? drawbars[j] : 0;
      if (c < '0' || c > '8') {
        for (int k = j; k < DRAWBARS; k++) setDrawbar(k, 0);
        return;
      }
      setDrawbar(j, c - '0');
    }
  }

  /// Sets one drawbar (0 = 16' ... 8 = 1') to a level 0..8
  void setDrawbar(int index, int level) {
    if (index < 0 || index >= DRAWBARS) return;
    level = level < 0 ? 0 : (level > 8 ? 8 : level);
    levels[index] = (uint8_t)level;
    version++;
  }

  int drawbar(int index) const { return index >= 0 && index < DRAWBARS ? levels[index] : 0; }

  /**
   * @brief Defines the percussion
   * @param on percussion on/off
   * @param third 3rd harmonic (2 2/3') or 2nd harmonic (4')
   * @param fast fast (~0.5 s) or slow (~1.5 s) decay
   * @param soft soft (-6 dB) or normal volume
   */
  void setPercussion(bool on, bool third = true, bool fast = true, bool soft = false) {
    perc_on = on;
    perc_third = third;
    perc_fast = fast;
    perc_soft = soft;
    version++;
  }

  bool percussion() const { return perc_on; }

  /// Key click: 0 (off) .. 1 (strong), default 0.4
  void setKeyClick(float amount) { click = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount); }
  float keyClick() const { return click; }

  /// Overall volume of the organ (default 0.25: room for chords)
  void setVolume(float value) {
    volume = value;
    version++;
  }

  /// Midi control change (see the class description)
  void controlChange(uint8_t controller, uint8_t value) {
    if (controller >= 12 && controller <= 20) {
      setDrawbar(controller - 12, (value * 8 + 63) / 127);
      return;
    }
    bool on = value >= 64;
    switch (controller) {
      case 21:
        setPercussion(on, perc_third, perc_fast, perc_soft);
        break;
      case 22:
        setPercussion(perc_on, on, perc_fast, perc_soft);
        break;
      case 23:
        setPercussion(perc_on, perc_third, on, perc_soft);
        break;
      case 24:
        setPercussion(perc_on, perc_third, perc_fast, on);
        break;
      case 25:
        setKeyClick(value / 127.0f);
        break;
      default:
        break;
    }
  }

  // ---- used by the OrganSource of the voices

  /// Pitch of the drawbars in semitones relative to the key (8')
  static int8_t offset(int index) {
    static const int8_t offsets[DRAWBARS] = {-12, 7, 0, 12, 19, 24, 28, 31, 36};
    return offsets[index];
  }

  /// Amplitude of a drawbar (3 dB per step) including the percussion rules
  float gain(int index) const {
    int level = levels[index];
    if (level == 0) return 0.0f;
    if (perc_on && index == DRAWBARS - 1) return 0.0f;  // 1' is used by the percussion
    static const float steps[9] = {0.0f, 0.0631f, 0.0891f, 0.1259f, 0.1778f, 0.2512f, 0.3548f, 0.5012f, 0.7079f};
    float g = steps[level] * volume;
    if (perc_on && !perc_soft) g *= 0.7079f;  // -3 dB
    return g;
  }

  /// Start amplitude of the percussion
  float percussionGain() const { return perc_on ? (perc_soft ? 0.5f : 1.0f) * volume : 0.0f; }
  /// Pitch of the percussion in semitones relative to the key
  int percussionOffset() const { return perc_third ? 19 : 12; }
  /// Time to -60 dB in seconds
  float percussionDecay() const { return perc_fast ? 0.5f : 1.5f; }

  /// Changes with each change of the registration
  uint32_t changes() const { return version; }

  /// Sine table with 1024 entries (+1 for the interpolation)
  static const float* sineTable() {
    static float table[1025];
    static bool initialized = false;
    if (!initialized) {
      for (int j = 0; j <= 1024; j++) table[j] = sinf(6.2831853f * j / 1024.0f);
      initialized = true;
    }
    return table;
  }

 protected:
  uint8_t levels[DRAWBARS] = {8, 8, 8, 0, 0, 0, 0, 0, 0};
  bool perc_on = false;
  bool perc_third = true;
  bool perc_fast = true;
  bool perc_soft = false;
  float click = 0.4f;
  float volume = 0.25f;
  volatile uint32_t version = 0;
};

/**
 * @brief The tone generator of one note of a TonewheelOrgan: the tonewheels of
 * the drawbars, the percussion and the key click. Used by the SamplerVoice.
 */
class OrganSource {
 public:
  /**
   * @brief Starts a note
   * @param organ registration
   * @param note midi note
   * @param sampleRate output sample rate
   * @param percussion trigger the percussion (no other key is held)
   * @param seed random start phases (the tonewheels are always turning)
   */
  void begin(const TonewheelOrgan* organ, uint8_t note, float sampleRate, bool percussion, uint32_t seed) {
    p_organ = organ;
    rate = sampleRate;
    random = seed * 2654435761u + 1u;
    table = TonewheelOrgan::sineTable();
    for (int j = 0; j < TonewheelOrgan::DRAWBARS; j++) {
      inc[j] = increment(note + TonewheelOrgan::offset(j));
      phase[j] = nextRandom();
    }
    perc_inc = increment(note + organ->percussionOffset());
    perc_phase = nextRandom();
    perc_env = percussion ? organ->percussionGain() : 0.0f;
    perc_mul = expf(-6.9077553f / (organ->percussionDecay() * sampleRate));
    // key click: a few ms of noise, louder for higher registrations
    click_env = organ->keyClick() * 0.15f;
    click_mul = expf(-1.0f / (0.0015f * sampleRate));
    click_lp = 0.0f;
    version = organ->changes() + 1;  // read the gains
    counter = 0;
    updateGains(true);
  }

  /// Next sample at the output sample rate
  inline float next() {
    if (counter == 0) {
      updateGains(false);
      counter = UPDATE;
    }
    counter--;
    float out = 0.0f;
    for (int k = 0; k < active_count; k++) {
      int j = active[k];
      out += gains[j] * sine(phase[j]);
      gains[j] += steps[j];
      phase[j] += inc[j];
    }
    if (perc_env > 0.00001f) {
      out += perc_env * sine(perc_phase);
      perc_phase += perc_inc;
      perc_env *= perc_mul;
    }
    if (click_env > 0.00001f) {
      // noise with a gentle lowpass
      float noise = (float)(int32_t)nextRandom() * (1.0f / 2147483648.0f);
      click_lp += 0.5f * (noise - click_lp);
      out += click_env * click_lp;
      click_env *= click_mul;
    }
    return out;
  }

 protected:
  static constexpr int UPDATE = 32;  // samples between the updates of the gains
  const TonewheelOrgan* p_organ = nullptr;
  const float* table = nullptr;
  float rate = 44100.0f;
  uint32_t phase[TonewheelOrgan::DRAWBARS];
  uint32_t inc[TonewheelOrgan::DRAWBARS];
  float gains[TonewheelOrgan::DRAWBARS];
  float steps[TonewheelOrgan::DRAWBARS];
  uint8_t active[TonewheelOrgan::DRAWBARS];
  int active_count = 0;
  uint32_t perc_phase = 0, perc_inc = 0;
  float perc_env = 0.0f, perc_mul = 1.0f;
  float click_env = 0.0f, click_mul = 1.0f, click_lp = 0.0f;
  uint32_t random = 1;
  uint32_t version = 0;
  int counter = 0;

  /// sine with linear interpolation: phase 0..2^32 = one period
  inline float sine(uint32_t p) const {
    uint32_t index = p >> 22;
    float frac = (float)(p & 0x3FFFFF) * (1.0f / 4194304.0f);
    float a = table[index];
    return a + (table[index + 1] - a) * frac;
  }

  inline uint32_t nextRandom() {
    random = random * 1664525u + 1013904223u;
    return random;
  }

  /// Phase increment of a tonewheel: equal tempered, the tonewheels above
  /// 5.9 kHz fold back an octave, below 32.7 Hz up an octave
  uint32_t increment(int note) const {
    float freq = 440.0f * powf(2.0f, (note - 69) / 12.0f);
    while (freq > 5920.0f) freq *= 0.5f;
    while (freq < 32.7f) freq *= 2.0f;
    double inc = (double)freq / rate * 4294967296.0;
    return (uint32_t)inc;
  }

  /// Reads the drawbar gains when the registration has changed: the gains
  /// ramp to the new values within UPDATE samples (no clicks)
  void updateGains(bool immediate) {
    uint32_t v = p_organ->changes();
    bool changed = v != version;
    version = v;
    active_count = 0;
    for (int j = 0; j < TonewheelOrgan::DRAWBARS; j++) {
      float target = p_organ->gain(j);
      if (immediate) {
        gains[j] = target;
        steps[j] = 0.0f;
      } else if (changed || steps[j] != 0.0f) {
        steps[j] = (target - gains[j]) / UPDATE;
        if (fabsf(steps[j]) < 1e-9f) {
          steps[j] = 0.0f;
          gains[j] = target;
        }
      }
      if (gains[j] != 0.0f || target != 0.0f) active[active_count++] = (uint8_t)j;
    }
  }
};

}  // namespace tinyinstruments

TI_OPTIMIZE_END
