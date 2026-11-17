#pragma once
#include <math.h>
#include <stdint.h>

#include "Config.h"
#include "ImaAdpcm.h"
#include "Organ.h"
#include "Psola.h"
#include "SampleSet.h"

TI_OPTIMIZE_BEGIN

namespace tinyinstruments {

/**
 * @brief Plays a single note from a SampleZone: ADPCM decoding, pitch
 * shifting with 4 point hermite interpolation, vibrato, decay envelope for the
 * looped part, attack and release envelopes, velocity dependent lowpass and
 * stereo panning. Optionally the transposition is done with PSOLA, which does
 * not change the length of the sound.
 */
class SamplerVoice {
 public:
  enum State : uint8_t { Idle, Playing, Released };

  /**
   * @brief Starts a note
   * @param zone sample zone
   * @param step source samples per output sample (pitch and rate conversion)
   * @param gain amplitude
   * @param lowpass one pole lowpass coefficient (1 = no filter)
   * @param pan_left, pan_right gains for the stereo panning
   * @param fromLoop start at the loop start (skips the attack, e.g. legato)
   * @param attackSamples fade in time in output samples (0 = none)
   * @param psolaRatio > 0: transposition with PSOLA by this factor (the zone
   * needs pitch marks); step then only contains the rate conversion
   */
  void start(const SampleZone* zone, float step, float gain, float lowpass,
             float pan_left, float pan_right, bool fromLoop = false,
             float attackSamples = 0.0f, float psolaRatio = 0.0f) {
    use_organ = false;
    startImpl(zone, step, gain, lowpass, pan_left, pan_right, fromLoop, attackSamples, psolaRatio);
  }

  /**
   * @brief Starts a note of a tonewheel organ: synthesized at the output
   * sample rate, sustains until it is released
   * @param organ registration
   * @param note midi note
   * @param sampleRate output sample rate
   * @param percussion trigger the percussion
   * @param gain amplitude
   * @param pan_left, pan_right gains for the stereo panning
   */
  void startOrgan(const TonewheelOrgan* organ, uint8_t note, float sampleRate, bool percussion,
                  float gain, float pan_left, float pan_right) {
    use_organ = true;
    organ_source.begin(organ, note, sampleRate, percussion, ++organ_seed + note);
    startImpl(&organZone(), 1.0f, gain, 1.0f, pan_left, pan_right, false, 0.0f, 0.0f);
  }

 protected:
  void startImpl(const SampleZone* zone, float step, float gain, float lowpass,
                 float pan_left, float pan_right, bool fromLoop, float attackSamples,
                 float psolaRatio) {
    p_zone = zone;
    base_step = step;
    pitch_factor = 1.0f;
    this->step = step;
    this->gain = gain;
    lp_coef = lowpass;
    lp_state = 0.0f;
    left = pan_left;
    right = pan_right;
    decay_env = 1.0f;
    extra_decay = 0.0f;
    decay_mul = expf(zone->decayLn * step);
    decay_mul_block = -1.0f;
    release_env = 1.0f;
    release_mul = 1.0f;
    release_mul_block = -1.0f;
    attack_env = attackSamples > 1.0f ? 0.0f : 1.0f;
    attack_inc = attackSamples > 1.0f ? 1.0f / attackSamples : 0.0f;
    elapsed = 0;
    lfo_phase = 0.0f;
    vib_depth = 0.0f;
    auto_depth = 0.0f;
    ended = 0;
    // PSOLA (falls back to resampling if the zone has no pitch marks or the
    // ring buffer can not be allocated)
    use_psola = !use_organ && psolaRatio > 0.0f && psola.begin(zone, psolaRatio, fromLoop);
    if (psolaRatio > 0.0f && !use_psola) {
      this->step = base_step = step * psolaRatio;
      decay_mul = expf(zone->decayLn * this->step);
    }
    if (use_organ) {
      index = 0;
      decay_active = false;
    } else if (fromLoop && zone->isLooped()) {
      index = zone->loopStart;
      decoder.begin(zone->data, zone->loopStart, zone->loopPredictor, zone->loopIndex);
      decay_active = zone->decayStart <= zone->loopStart;
    } else {
      index = 0;
      decoder.begin(zone->data);
      decay_active = false;
    }
    // history: x[-1], x[0], x[1], x[2]
    hist[0] = 0.0f;
    hist[1] = nextSample();
    hist[2] = nextSample();
    hist[3] = nextSample();
    frac = 0.0f;
    state = Playing;
  }

 public:

  /// Starts the release: time in output samples to reach -60dB
  void release(float samples) {
    if (state == Idle) return;
    if (samples < 1.0f) samples = 1.0f;
    float mul = expf(-6.9077553f / samples);  // ln(0.001)
    // a faster release overrides a slower one
    if (state != Released || mul < release_mul) {
      release_mul = mul;
      release_mul_block = -1.0f;
    }
    state = Released;
  }

  /// Channel gain (midi volume, expression, breath): multiplied with the note
  /// gain. Changes are smoothed (~10 ms) unless immediate is true.
  void setChannelGain(float value, bool immediate = false) {
    channel_gain_target = value;
    if (immediate) channel_gain = value;
  }

  /// Stops the voice immediately
  void stop() { state = Idle; }

  /// Additional decay from the start of the note (e.g. palm mute): ln of the
  /// amplitude per sample of the zone (negative)
  void setExtraDecay(float lnPerSample) {
    if (state == Idle) return;
    extra_decay = lnPerSample;
    decay_mul = expf((p_zone->decayLn + extra_decay) * step);
    decay_mul_block = -1.0f;
    if (extra_decay != 0.0f) decay_active = true;
  }

  /// Changes the pitch by the indicated factor (e.g. pitch bend)
  void setPitchFactor(float factor) {
    if (state == Idle) return;
    pitch_factor = factor;
    step = base_step * factor;
    decay_mul = expf((p_zone->decayLn + extra_decay) * step);
    decay_mul_block = -1.0f;
  }

  /**
   * @brief Defines the vibrato
   * @param rateHz speed of the vibrato
   * @param depthCents depth (e.g. from the modulation wheel)
   * @param autoDepthCents additional depth which fades in automatically
   * @param autoDelay output samples after the note start until the auto vibrato starts
   * @param autoRise output samples to reach the full auto vibrato depth
   * @param sampleRate output sample rate
   */
  void setVibrato(float rateHz, float depthCents, float autoDepthCents, float autoDelay,
                  float autoRise, float sampleRate) {
    lfo_inc = 6.2831853f * rateHz / sampleRate;
    vib_depth = depthCents;
    auto_depth = autoDepthCents;
    auto_delay = autoDelay;
    auto_rise = autoRise > 1.0f ? autoRise : 1.0f;
  }

  /// Changes the vibrato depth in cents (e.g. modulation wheel)
  void setVibratoDepth(float depthCents) { vib_depth = depthCents; }

  /// Adds the output to the provided buffers. The envelopes are calculated
  /// once per block and interpolated linearly inside of the block.
  void render(float* out_left, float* out_right, int frames) {
    if (state == Idle || frames <= 0) return;
    updateVibrato(frames);
    float amp = amplitude();
    advanceEnvelopes(frames);
    float amp_inc = (amplitude() - amp) / frames;

    // local copies: the compiler can keep them in registers (the output
    // buffers could otherwise alias the members)
    float f = frac, st = step, lp = lp_state, lc = lp_coef, l = left, r = right;
    float h0 = hist[0], h1 = hist[1], h2 = hist[2], h3 = hist[3];
    for (int j = 0; j < frames; j++) {
      // 4 point, 3rd order hermite interpolation
      float c1 = 0.5f * (h2 - h0);
      float c2 = h0 - 2.5f * h1 + 2.0f * h2 - 0.5f * h3;
      float c3 = 0.5f * (h3 - h0) + 1.5f * (h1 - h2);
      float sample = ((c3 * f + c2) * f + c1) * f + h1;

      lp += lc * (sample - lp);
      float out = lp * amp;
      amp += amp_inc;
      out_left[j] += out * l;
      out_right[j] += out * r;

      f += st;
      while (f >= 1.0f) {
        f -= 1.0f;
        h0 = h1;
        h1 = h2;
        h2 = h3;
        h3 = nextSample();
      }
    }
    frac = f;
    lp_state = lp;
    hist[0] = h0;
    hist[1] = h1;
    hist[2] = h2;
    hist[3] = h3;
    elapsed += frames;

    // end: silent or no more data
    if (decay_env * release_env < 0.0001f || ended > 4) state = Idle;
  }

  State getState() const { return state; }
  bool isActive() const { return state != Idle; }
  /// Current envelope level (without the velocity gain)
  float level() const { return decay_env * release_env * attack_env; }
  const SampleZone* zone() const { return p_zone; }
  /// Current pitch factor including the vibrato (e.g. for tests)
  float currentStep() const { return step; }
  /// true if the transposition is done with PSOLA
  bool isPsola() const { return use_psola; }
  /// true if the voice plays a tonewheel organ note
  bool isOrgan() const { return use_organ; }

  // management data used by the SampleSynthesizer
  uint8_t channel = 0;
  uint8_t note = 0;
  uint8_t velocity = 0;
  bool key_down = false;   ///< the key is still pressed
  bool sustained = false;  ///< note off received while the sustain pedal was down
  bool fading = false;     ///< stolen or re-struck: fast fade out, does not count as note
  uint32_t age = 0;

 protected:
  const SampleZone* p_zone = nullptr;
  ImaAdpcmDecoder decoder;
  State state = Idle;
  uint32_t index = 0;    // next sample index to decode
  uint32_t elapsed = 0;  // output samples since the start
  uint8_t ended = 0;
  float hist[4] = {0, 0, 0, 0};
  float frac = 0.0f;
  float step = 1.0f;
  float base_step = 1.0f;
  float pitch_factor = 1.0f;
  float gain = 1.0f;
  float channel_gain = 1.0f;
  float channel_gain_target = 1.0f;
  float left = 1.0f;
  float right = 1.0f;
  float lp_coef = 1.0f;
  float lp_state = 0.0f;
  float decay_env = 1.0f;
  float decay_mul = 1.0f;
  float extra_decay = 0.0f;
  float decay_mul_block = -1.0f;  // decay_mul ^ TI_BLOCK_SIZE (< 0: not calculated)
  bool decay_active = false;
  float release_env = 1.0f;
  float release_mul = 1.0f;
  float release_mul_block = -1.0f;
  float attack_env = 1.0f;
  float attack_inc = 0.0f;
  // vibrato
  float lfo_phase = 0.0f;
  float lfo_inc = 0.0f;
  float vib_depth = 0.0f;
  float auto_depth = 0.0f;
  float auto_delay = 0.0f;
  float auto_rise = 1.0f;
  // PSOLA pitch shifting
  PsolaSource psola;
  bool use_psola = false;
  // tonewheel organ
  OrganSource organ_source;
  bool use_organ = false;
  uint32_t organ_seed = 0;

  /// Zone of the organ notes: no data, no decay
  static const SampleZone& organZone() {
    static const SampleZone zone = {nullptr, 0xFFFFFFFFu, 0, 0, 0, 0, 0xFFFFFFFFu, 0.0f, 60, 0, 127,
                                    0, 127, 0, 0, 0, nullptr, 0};
    return zone;
  }

  /// Current output amplitude: envelopes and gains
  inline float amplitude() const {
    return decay_env * release_env * attack_env * gain * channel_gain;
  }

  /// Advances the envelopes by the indicated number of samples
  void advanceEnvelopes(int frames) {
    if (decay_active && decay_mul != 1.0f) decay_env *= blockPow(decay_mul, decay_mul_block, frames);
    if (release_mul != 1.0f) release_env *= blockPow(release_mul, release_mul_block, frames);
    if (attack_env < 1.0f) {
      attack_env += attack_inc * frames;
      if (attack_env > 1.0f) attack_env = 1.0f;
    }
    // smoothing of the channel gain: ~10 ms
    float k = 0.002f * frames;
    channel_gain += (k > 1.0f ? 1.0f : k) * (channel_gain_target - channel_gain);
  }

  /// mul ^ frames: cached for the standard block size
  static inline float blockPow(float mul, float& cache, int frames) {
    if (frames != TI_BLOCK_SIZE) return powf(mul, frames);
    if (cache < 0.0f) cache = powf(mul, TI_BLOCK_SIZE);
    return cache;
  }

  /// Pitch modulation for the next block (the lfo is slow)
  void updateVibrato(int frames) {
    float depth = vib_depth;
    if (auto_depth > 0.0f && elapsed > auto_delay) {
      float amount = (elapsed - auto_delay) / auto_rise;
      depth += auto_depth * (amount > 1.0f ? 1.0f : amount);
    }
    if (depth <= 0.0f) {
      step = base_step * pitch_factor;
      return;
    }
    lfo_phase += lfo_inc * frames;
    if (lfo_phase > 6.2831853f) lfo_phase -= 6.2831853f;
    // 2^(cents/1200) ~ 1 + cents * 0.000577623 for small values
    step = base_step * pitch_factor * (1.0f + depth * 0.000577623f * sinf(lfo_phase));
  }

  inline float nextSample() {
    if (use_organ) return organ_source.next();
    const SampleZone& z = *p_zone;
    if (use_psola) {
      float s = psola.next();
      if (!decay_active && psola.time() >= z.decayStart) decay_active = true;
      if (psola.finished()) ended++;
      return s;
    }
    if (index >= z.loopEnd && z.isLooped()) {
      index = z.loopStart;
      decoder.seek(z.loopStart, z.loopPredictor, z.loopIndex);
    }
    if (index >= z.length) {
      ended++;
      return 0.0f;
    }
    if (index == z.decayStart) decay_active = true;
    index++;
    return decoder.next() * (1.0f / 32768.0f);
  }
};

}  // namespace tinyinstruments

TI_OPTIMIZE_END
