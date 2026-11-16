#pragma once
#include <math.h>
#include <stdint.h>

#include "Config.h"
#include "DynamicSampleSet.h"
#include "ImaAdpcm.h"
#include "SampleSet.h"

/// Ring buffer (16 bit samples, power of 2) of a voice which uses PSOLA: it
/// must hold about 4 periods of the lowest note (4096: 32 Hz at 32 kHz)
#ifndef TI_PSOLA_BUFFER
#  define TI_PSOLA_BUFFER 4096
#endif

TI_OPTIMIZE_BEGIN

namespace tinyinstruments {

/**
 * @brief Pitch shifting which does not change the length of the sound
 * (TD-PSOLA: time domain pitch synchronous overlap and add).
 *
 * The zone contains pitch marks (the start of each period). A grain covers the
 * two periods around a mark. The grains are placed at the distance of the new
 * period, while the time advances normally, so the pitch changes but the length
 * and the spectral envelope (formants) stay the same.
 *
 * Because the time is not changed, all grains are close to the current time:
 * the zone is decoded sequentially into a small ring buffer from which the
 * grains are read. The attack (before the first pitch mark) is played unchanged
 * and crossfaded into the grains, so that the onset stays crisp. The output is
 * at the sample rate of the zone: the voice converts it to the output rate.
 */
class PsolaSource {
 public:
  ~PsolaSource() {
    if (ring) SampleMemory::release(ring);
  }

  /// true if the zone can be pitch shifted with PSOLA
  static bool isSupported(const SampleZone* zone) {
    return zone != nullptr && zone->marks != nullptr && zone->markCount >= 4;
  }

  /**
   * @brief Starts the playback: returns false if the ring buffer can not be
   * allocated or the zone has no pitch marks
   * @param zone sample zone with pitch marks
   * @param ratio pitch factor (> 1 higher), 0.5 .. 2
   * @param fromLoop start at the loop (e.g. legato): no attack
   */
  bool begin(const SampleZone* zone, float ratio, bool fromLoop = false) {
    if (!isSupported(zone)) return false;
    if (ring == nullptr) {
      ring = (int16_t*)SampleMemory::allocate(TI_PSOLA_BUFFER * sizeof(int16_t));
      if (ring == nullptr) return false;
    }
    z = zone;
    this->ratio = ratio < 0.5f ? 0.5f : (ratio > 2.0f ? 2.0f : ratio);
    grain_count = 0;
    done = false;
    loop_len = z->isLooped() ? (int32_t)(z->loopEnd - z->loopStart) : 0;

    // absolute positions of the first marks and of the first mark in the loop
    first_pos = z->marks[0];
    loop_mark = 1;
    loop_mark_pos = first_pos + z->marks[1];
    if (z->isLooped()) {
      // the whole grain of the first loop mark must be in the loop
      while (loop_mark + 1 < z->markCount && loop_mark_pos - z->marks[loop_mark] < (int32_t)z->loopStart) {
        loop_mark++;
        loop_mark_pos += z->marks[loop_mark];
      }
    }

    if (fromLoop && z->isLooped()) {
      // no attack: the first grain is at the loop
      raw_active = false;
      cursor = loop_mark;
      cursor_pos = loop_mark_pos;
      next_syn = (double)loop_mark_pos;
      pos = loop_mark_pos - leftOf(cursor);
      decoder.begin(z->data, z->loopStart, z->loopPredictor, z->loopIndex);
      decoded = (int32_t)z->loopStart;
      decode_idx = z->loopStart;
      decode_begin = decoded;
    } else {
      // attack unchanged, crossfaded into the grains between mark 0 and 1
      raw_active = true;
      raw_fade_start = first_pos;
      raw_fade_inv = 1.0f / z->marks[1];
      cursor = 1;
      cursor_pos = first_pos + z->marks[1];
      next_syn = (double)cursor_pos;
      pos = 0;
      decoder.begin(z->data);
      decoded = 0;
      decode_idx = 0;
      decode_begin = 0;
    }
    next_mark = cursor;
    next_mark_u = cursor_pos;
    next_start = (int32_t)(next_syn + 0.5) - leftOf(next_mark);
    return true;
  }

  /// Next sample at the sample rate of the zone (-1..1)
  inline float next() {
    // start the grains which begin at this position
    while (!done && pos >= next_start) startGrain();

    float acc = 0.0f, weight = 0.0f;
    if (raw_active) {
      float w = 1.0f;
      if (pos >= raw_fade_start) {
        float x = (float)(pos - raw_fade_start) * raw_fade_inv;
        if (x >= 1.0f) {
          raw_active = false;
          w = 0.0f;
        } else {
          w = 1.0f - smoothstep(x);
        }
      }
      if (raw_active) {
        decodeTo(pos);
        acc += w * ring[pos & MASK];
        weight += w;
      }
    }
    // the grains (the ring buffer is already decoded up to their end)
    for (int j = 0; j < grain_count;) {
      Grain& g = grains[j];
      float sm = smoothstep(g.x);
      float w = g.rising ? sm : 1.0f - sm;
      acc += w * ring[g.u & MASK];
      weight += w;
      g.u++;
      g.x += g.inc;
      if (--g.remaining <= 0) {
        if (g.rising) {
          // second half: falling window
          g.rising = false;
          g.x = 0.0f;
          g.inc = g.inc_right;
          g.remaining = g.right;
        } else {
          grains[j] = grains[--grain_count];  // finished: remove
          continue;
        }
      }
      j++;
    }
    pos++;
    // normalized overlap add: constant level for any grain distance
    return weight > 0.001f ? acc * (1.0f / 32768.0f) / weight : 0.0f;
  }

  /// All grains are played (only for zones without loop)
  bool finished() const { return done && !raw_active && grain_count == 0; }

  /// Played time in samples of the zone (the time is not changed by the pitch)
  uint32_t time() const { return (uint32_t)pos; }

 protected:
  static constexpr int MAX_GRAINS = 6;
  static constexpr int32_t MASK = TI_PSOLA_BUFFER - 1;  // positions: 32 bit = 18 h at 32 kHz
  struct Grain {
    int32_t u = 0;          // next read position on the time line
    int32_t remaining = 0;  // samples left in the current half
    int32_t right = 1;      // samples after the mark
    float x = 0.0f;         // window position 0..1 in the current half
    float inc = 1.0f;       // window step of the current half
    float inc_right = 1.0f; // window step of the second half
    bool rising = true;     // first half: rising window
  };

  const SampleZone* z = nullptr;
  float ratio = 1.0f;
  Grain grains[MAX_GRAINS];  // the active grains: 0..grain_count-1
  int grain_count = 0;
  int16_t* ring = nullptr;
  // sequential decoding into the ring buffer: time line position "decoded"
  ImaAdpcmDecoder decoder;
  int32_t decoded = 0;         // next time line position to decode
  uint32_t decode_idx = 0;     // zone index of this position
  int32_t decode_begin = 0;    // first decoded time line position
  bool raw_active = false;
  int32_t raw_fade_start = 0;
  float raw_fade_inv = 1.0f;
  int32_t pos = 0;          // synthesis position (time line, samples of the zone)
  double next_syn = 0;      // next synthesis mark (double: precise also on long notes)
  int next_mark = 1;        // analysis mark of the next synthesis mark
  int32_t next_mark_u = 0;  // its position on the time line
  int32_t next_start = 0;   // start of the next grain
  // marks are stored as distances: cursor for the absolute positions
  int cursor = 1;
  int32_t cursor_pos = 0;
  int32_t first_pos = 0;
  int loop_mark = 1;
  int32_t loop_mark_pos = 0;
  int32_t loop_len = 0;
  bool done = false;

  /// cubic fade 0..1 with smoothstep(x) + smoothstep(1-x) = 1
  static inline float smoothstep(float x) { return x * x * (3.0f - 2.0f * x); }

  int32_t leftOf(int m) const { return z->marks[m] > 0 ? z->marks[m] : 1; }

  int32_t rightOf(int m) const {
    if (m + 1 >= z->markCount) return leftOf(m);
    return z->marks[m + 1] > 0 ? z->marks[m + 1] : 1;
  }

  /// Decodes the time line (the loop is repeated) into the ring buffer up to
  /// the position u
  inline void decodeTo(int32_t u) {
    while (decoded <= u) {
      int16_t v = 0;
      if (decode_idx >= z->loopEnd && z->isLooped()) {
        decode_idx = z->loopStart;
        decoder.seek(z->loopStart, z->loopPredictor, z->loopIndex);
      }
      if (decode_idx < z->length) {
        v = decoder.next();
        decode_idx++;
      }
      ring[decoded & MASK] = v;
      decoded++;
    }
  }

  /// Starts the grain of next_mark at next_syn and schedules the next one
  void startGrain() {
    if (grain_count < MAX_GRAINS) {
      Grain& g = grains[grain_count++];
      int32_t left = leftOf(next_mark);
      g.right = rightOf(next_mark);
      // the grain may start a little later than planned (rounding)
      int32_t late = pos - next_start;
      late = late > 0 ? (late < left ? late : left - 1) : 0;
      g.u = next_mark_u - left + late;
      g.remaining = left - late;
      g.inc = 1.0f / left;
      g.inc_right = 1.0f / g.right;
      g.x = late * g.inc;
      g.rising = true;
      // decode the whole grain: no checks are needed when reading it
      decodeTo(next_mark_u + g.right);
    }
    // next synthesis mark: one period of the new pitch later (the period
    // after the mark: for a ratio of 1 the marks are met exactly, also if the
    // periods of the recording vary)
    next_syn += rightOf(next_mark) / ratio;
    if (!findMark(next_syn)) {
      done = true;  // end of the zone (no loop)
      return;
    }
    next_start = (int32_t)(next_syn + 0.5) - leftOf(next_mark);
  }

  /// Finds the mark closest to the time t (on the time line, the loop is
  /// repeated): sets next_mark and next_mark_u
  bool findMark(double t) {
    // offset of the loop repetition which contains t
    int32_t offset = 0;
    double local = t;
    if (loop_len > 0 && t >= z->loopEnd) {
      int32_t rep = (int32_t)((t - z->loopStart) / loop_len);
      offset = rep * loop_len;
      local = t - (double)offset;
    }
    // the time jumped back into the loop: restart at the first loop mark
    if ((double)cursor_pos > local + leftOf(cursor)) {
      cursor = loop_mark;
      cursor_pos = loop_mark_pos;
    }
    while (cursor + 1 < z->markCount &&
           fabs((double)(cursor_pos + z->marks[cursor + 1]) - local) <= fabs((double)cursor_pos - local)) {
      cursor++;
      cursor_pos += z->marks[cursor];
    }
    if (loop_len == 0 && cursor + 1 >= z->markCount && local > cursor_pos + rightOf(cursor)) return false;
    next_mark = cursor;
    next_mark_u = cursor_pos + offset;
    return true;
  }
};

}  // namespace tinyinstruments

TI_OPTIMIZE_END
