#include "TinyInstruments.h"
#include "TestHelper.h"

static std::vector<int16_t> testSignal(size_t n) {
  std::vector<int16_t> result(n);
  for (size_t j = 0; j < n; j++) {
    double t = j / 32000.0;
    double v = 0.5 * sin(2 * M_PI * 440 * t) + 0.2 * sin(2 * M_PI * 1320 * t) +
               0.05 * sin(2 * M_PI * 5000 * t);
    result[j] = (int16_t)(v * 20000 * exp(-t));
  }
  return result;
}

static void testRoundTrip() {
  auto pcm = testSignal(32000);
  TestAdpcmEncoder enc;
  for (auto s : pcm) enc.add(s);
  CHECK(enc.data.size() == pcm.size() / 2);

  ImaAdpcmDecoder dec;
  dec.begin(enc.data.data());
  double signal = 0, noise = 0;
  for (size_t j = 0; j < pcm.size(); j++) {
    int16_t v = dec.next();
    signal += (double)pcm[j] * pcm[j];
    noise += (double)(pcm[j] - v) * (pcm[j] - v);
  }
  double snr = 10 * log10(signal / noise);
  printf("ADPCM SNR: %.1f dB\n", snr);
  CHECK(snr > 25.0);
  CHECK(dec.position() == pcm.size());
}

static void testSeekWithState() {
  // decoding from a saved state must give exactly the same samples
  auto pcm = testSignal(4000);
  TestAdpcmEncoder enc;
  for (auto s : pcm) enc.add(s);

  ImaAdpcmDecoder dec;
  dec.begin(enc.data.data());
  std::vector<int16_t> full;
  int16_t pred = 0;
  uint8_t index = 0;
  for (size_t j = 0; j < pcm.size(); j++) {
    if (j == 1001) {  // odd position: high nibble
      pred = dec.predictor();
      index = dec.stepIndex();
    }
    full.push_back(dec.next());
  }

  ImaAdpcmDecoder dec2;
  dec2.begin(enc.data.data(), 1001, pred, index);
  bool same = true;
  for (size_t j = 1001; j < pcm.size(); j++) {
    if (dec2.next() != full[j]) same = false;
  }
  CHECK(same);
}

static void testClamping() {
  // full scale square wave must not overflow
  TestAdpcmEncoder enc;
  for (int j = 0; j < 2000; j++) enc.add((j / 50) % 2 ? 32767 : -32768);
  ImaAdpcmDecoder dec;
  dec.begin(enc.data.data());
  bool ok = true;
  for (int j = 0; j < 2000; j++) {
    int16_t v = dec.next();
    // end of each half wave: the full range must be reached without wrap around
    if (j > 200 && (j % 50) == 49 && abs(v) < 20000) ok = false;
  }
  CHECK(ok);
}

int main() {
  testRoundTrip();
  testSeekWithState();
  testClamping();
  return testResult("test_adpcm");
}
