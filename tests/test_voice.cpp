#include "TinyInstruments.h"
#include "TestHelper.h"

static std::vector<float> play(SamplerVoice& v, size_t frames) {
  std::vector<float> left(frames, 0.0f), right(frames, 0.0f);
  for (size_t j = 0; j < frames; j += 64) {
    size_t n = frames - j < 64 ? frames - j : 64;
    v.render(&left[j], &right[j], n);
  }
  return left;
}

static void testPitch() {
  SineZone sine;
  SamplerVoice v;
  // played at the recording pitch, output at 44100
  v.start(&sine.zone, 32000.0f / 44100.0f, 1.0f, 1.0f, 1.0f, 1.0f);
  auto out = play(v, 8192);
  CHECK_NEAR(estimateFrequency(out, 44100, 200, 1000), 400.0, 1.0);

  // one octave higher
  v.start(&sine.zone, 2.0f * 32000.0f / 44100.0f, 1.0f, 1.0f, 1.0f, 1.0f);
  out = play(v, 8192);
  CHECK_NEAR(estimateFrequency(out, 44100, 400, 2000), 800.0, 2.0);

  // pitch bend: +2 semitones
  v.start(&sine.zone, 32000.0f / 44100.0f, 1.0f, 1.0f, 1.0f, 1.0f);
  v.setPitchFactor(powf(2.0f, 2.0f / 12.0f));
  out = play(v, 8192);
  CHECK_NEAR(estimateFrequency(out, 44100, 200, 1000), 400.0 * pow(2.0, 2.0 / 12.0), 2.0);
}

static void testLoopKeepsSounding() {
  SineZone sine;  // no decay
  SamplerVoice v;
  v.start(&sine.zone, 32000.0f / 44100.0f, 1.0f, 1.0f, 1.0f, 1.0f);
  auto out = play(v, 44100 * 3);  // 3 seconds: much longer than the sample
  CHECK(v.isActive());
  double begin = rms(out, 4410, 8820);
  double end = rms(out, out.size() - 4410);
  CHECK_NEAR(end / begin, 1.0, 0.05);
  // no discontinuities: the max step is limited by the sine
  float max_step = 0;
  for (size_t j = 1; j < out.size(); j++) max_step = fmaxf(max_step, fabsf(out[j] - out[j - 1]));
  // max slope of the sine + some ADPCM quantization noise
  CHECK(max_step < 0.5f * 2 * M_PI * 400 / 44100 * 1.5);
}

static void testDecay() {
  SineZone sine(-20.0f);  // -20 dB per second after the loop start (0.5 s)
  SamplerVoice v;
  v.start(&sine.zone, 32000.0f / 44100.0f, 1.0f, 1.0f, 1.0f, 1.0f);
  auto out = play(v, 44100 * 2);
  double a = rms(out, 44100 * 0.9, 44100 * 1.1);  // around 1.0s
  double b = rms(out, 44100 * 1.9, 44100 * 2.0 - 1);  // around 2.0s
  double db = 20 * log10(b / a);
  CHECK_NEAR(db, -19.0, 2.0);
}

static void testRelease() {
  SineZone sine;
  SamplerVoice v;
  v.start(&sine.zone, 32000.0f / 44100.0f, 1.0f, 1.0f, 1.0f, 1.0f);
  play(v, 4410);
  CHECK(v.getState() == SamplerVoice::Playing);
  v.release(0.1f * 44100);  // -60 dB after 0.1 s
  play(v, 4410);
  CHECK(v.isActive());
  CHECK(v.level() < 0.0011f);
  play(v, 4410);  // -80 dB reached: voice ends
  CHECK(!v.isActive());
}

static void testStereo() {
  SineZone sine;
  SamplerVoice v;
  std::vector<float> left(1000, 0.0f), right(1000, 0.0f);
  v.start(&sine.zone, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f);
  v.render(left.data(), right.data(), 1000);
  CHECK(rms(left) > 0.1);
  CHECK(rms(right) == 0.0);
}

int main() {
  testPitch();
  testLoopKeepsSounding();
  testDecay();
  testRelease();
  testStereo();
  return testResult("test_voice");
}
