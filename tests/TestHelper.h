#pragma once
// Minimal dependency free test helpers
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <vector>

static int test_failures = 0;
static int test_count = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    test_count++;                                                       \
    if (!(cond)) {                                                      \
      test_failures++;                                                  \
      printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                   \
  } while (0)

#define CHECK_NEAR(a, b, tolerance)                                     \
  do {                                                                  \
    test_count++;                                                       \
    double va = (a), vb = (b);                                          \
    if (!(fabs(va - vb) <= (tolerance))) {                              \
      test_failures++;                                                  \
      printf("FAILED %s:%d: %s = %g, expected %g +/- %g\n", __FILE__,   \
             __LINE__, #a, va, vb, (double)(tolerance));                \
    }                                                                   \
  } while (0)

inline int testResult(const char* name) {
  printf("%s: %d checks, %d failures\n", name, test_count, test_failures);
  return test_failures == 0 ? 0 : 1;
}

/// RMS of a signal
inline double rms(const std::vector<float>& x, size_t from = 0, size_t to = 0) {
  if (to == 0 || to > x.size()) to = x.size();
  double sum = 0;
  for (size_t j = from; j < to; j++) sum += (double)x[j] * x[j];
  return to > from ? sqrt(sum / (to - from)) : 0.0;
}

/// Fundamental frequency estimate with the autocorrelation in the range fmin..fmax
inline double estimateFrequency(const std::vector<float>& x, double rate,
                                double fmin, double fmax) {
  int min_lag = (int)(rate / fmax);
  int max_lag = (int)(rate / fmin);
  int n = (int)x.size() - max_lag - 1;
  if (n <= 0) return 0.0;
  std::vector<double> ac(max_lag + 2, 0.0);
  for (int lag = min_lag - 1; lag <= max_lag + 1; lag++) {
    double sum = 0;
    for (int j = 0; j < n; j++) sum += (double)x[j] * x[j + lag];
    ac[lag] = sum;
  }
  // first peak which reaches 90% of the max: avoids picking a multiple of the period
  double max_ac = 0;
  for (int lag = min_lag; lag <= max_lag; lag++) max_ac = std::max(max_ac, ac[lag]);
  int best = min_lag;
  for (int lag = min_lag; lag <= max_lag; lag++) {
    if (ac[lag] >= 0.9 * max_ac && ac[lag] >= ac[lag - 1] && ac[lag] >= ac[lag + 1]) {
      best = lag;
      break;
    }
  }
  // parabolic interpolation of the peak
  double y0 = ac[best - 1], y1 = ac[best], y2 = ac[best + 1];
  double denom = y0 - 2 * y1 + y2;
  double shift = denom != 0 ? 0.5 * (y0 - y2) / denom : 0.0;
  return rate / (best + shift);
}

/// Simple IMA ADPCM encoder (same algorithm as tools/sfz2header.py)
struct TestAdpcmEncoder {
  int pred = 0;
  int index = 0;
  std::vector<uint8_t> data;
  size_t count = 0;

  void add(int16_t sample) {
    static const int16_t* steps = tinyinstruments::ImaAdpcmDecoder::stepTable();
    static const int8_t* idx = tinyinstruments::ImaAdpcmDecoder::indexTable();
    int step = steps[index];
    int diff = sample - pred;
    int code = 0;
    if (diff < 0) {
      code = 8;
      diff = -diff;
    }
    int delta = step >> 3;
    if (diff >= step) { code |= 4; diff -= step; delta += step; }
    step >>= 1;
    if (diff >= step) { code |= 2; diff -= step; delta += step; }
    step >>= 1;
    if (diff >= step) { code |= 1; delta += step; }
    pred = (code & 8) ? pred - delta : pred + delta;
    if (pred > 32767) pred = 32767;
    if (pred < -32768) pred = -32768;
    index += idx[code];
    if (index < 0) index = 0;
    if (index > 88) index = 88;
    if (count & 1) {
      data.back() |= code << 4;
    } else {
      data.push_back(code);
    }
    count++;
  }
};

/// Test zone: 400 Hz sine at 32 kHz (80 samples per period) with a loop
struct SineZone {
  // requires TinyInstruments.h
  TestAdpcmEncoder enc;
  SampleZone zone;

  SineZone(float decay_db_per_s = 0.0f) {
    const int rate = 32000, length = 32000, loop_start = 16000;
    int16_t loop_pred = 0;
    uint8_t loop_index = 0;
    for (int j = 0; j < length; j++) {
      if (j == loop_start) {
        loop_pred = enc.pred;
        loop_index = enc.index;
      }
      // short fade in like a real recording: ADPCM needs a few samples to adapt
      double fade = j < 160 ? j / 160.0 : 1.0;
      enc.add((int16_t)(fade * 16000 * sin(2 * M_PI * 400.0 * j / rate)));
    }
    zone = SampleZone{enc.data.data(), (uint32_t)length, (uint32_t)loop_start,
                      (uint32_t)length, loop_pred, loop_index, (uint32_t)loop_start,
                      (float)(decay_db_per_s / 20.0 * log(10.0) / rate),
                      67, 0, 127, 1, 127, 0, 0, 0, nullptr, 0};
  }
};

