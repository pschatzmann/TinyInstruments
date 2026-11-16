#pragma once
#include <math.h>

#include "Config.h"

TI_OPTIMIZE_BEGIN

namespace tinyinstruments {

/**
 * @brief Second order IIR filter (transposed direct form II) with the
 * designs of the RBJ audio EQ cookbook.
 */
class Biquad {
 public:
  void lowPass(float freq, float q, float rate) {
    float w = omega(freq, rate), c = cosf(w), a = sinf(w) / (2 * q);
    set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + a, -2 * c, 1 - a);
  }

  void highPass(float freq, float q, float rate) {
    float w = omega(freq, rate), c = cosf(w), a = sinf(w) / (2 * q);
    set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + a, -2 * c, 1 - a);
  }

  /// Peaking EQ: gain in dB
  void peak(float freq, float q, float gainDb, float rate) {
    float A = powf(10.0f, gainDb / 40.0f), w = omega(freq, rate), c = cosf(w);
    float a = sinf(w) / (2 * q);
    set(1 + a * A, -2 * c, 1 - a * A, 1 + a / A, -2 * c, 1 - a / A);
  }

  void lowShelf(float freq, float gainDb, float rate) {
    float A = powf(10.0f, gainDb / 40.0f), w = omega(freq, rate), c = cosf(w);
    float a = sinf(w) / 2 * sqrtf(2.0f), s = 2 * sqrtf(A) * a;
    set(A * ((A + 1) - (A - 1) * c + s), 2 * A * ((A - 1) - (A + 1) * c),
        A * ((A + 1) - (A - 1) * c - s), (A + 1) + (A - 1) * c + s, -2 * ((A - 1) + (A + 1) * c),
        (A + 1) + (A - 1) * c - s);
  }

  void highShelf(float freq, float gainDb, float rate) {
    float A = powf(10.0f, gainDb / 40.0f), w = omega(freq, rate), c = cosf(w);
    float a = sinf(w) / 2 * sqrtf(2.0f), s = 2 * sqrtf(A) * a;
    set(A * ((A + 1) + (A - 1) * c + s), -2 * A * ((A - 1) + (A + 1) * c),
        A * ((A + 1) + (A - 1) * c - s), (A + 1) - (A - 1) * c + s, 2 * ((A - 1) - (A + 1) * c),
        (A + 1) - (A - 1) * c - s);
  }

  inline float process(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }

  void reset() { z1 = z2 = 0.0f; }

 protected:
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  float z1 = 0, z2 = 0;

  static float omega(float freq, float rate) {
    float f = freq < 0.49f * rate ? freq : 0.49f * rate;
    return 6.2831853f * f / rate;
  }

  void set(float nb0, float nb1, float nb2, float na0, float na1, float na2) {
    b0 = nb0 / na0;
    b1 = nb1 / na0;
    b2 = nb2 / na0;
    a1 = na1 / na0;
    a2 = na2 / na0;
  }
};

}  // namespace tinyinstruments

TI_OPTIMIZE_END
