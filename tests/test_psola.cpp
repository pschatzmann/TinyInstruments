#include "TinyInstruments.h"
#include "TestHelper.h"
#include "samples/Clarinet.h"

// PSOLA pitch shifting: pitch, length, continuity, loops

static const int RATE = 32000;
static const int PERIOD = 80;  // 400 Hz

/// Harmonic tone like an instrument: 400 Hz with 8 harmonics (amplitude 1/k)
static double wave(int j) {
  double v = 0;
  for (int k = 1; k <= 8; k++) v += sin(2 * M_PI * k * j / PERIOD) / k;
  return v / 1.75;  // peak about 1
}

/// Zone with a 400 Hz tone whose amplitude jumps from 0.2 to 0.8 at 0.5 s,
/// with pitch marks at every period
struct StepZone {
  TestAdpcmEncoder enc;
  std::vector<uint16_t> marks;
  SampleZone zone;

  StepZone(bool looped, int length = RATE) {
    const int loop_start = length / 2 + 4000;
    int16_t loop_pred = 0;
    uint8_t loop_index = 0;
    for (int j = 0; j < length; j++) {
      // pitch marks: distance to the previous one (the first one: position)
      if (j % PERIOD == 0 && j >= 2 * PERIOD) marks.push_back(marks.empty() ? j : PERIOD);
      if (j == loop_start) {
        loop_pred = enc.pred;
        loop_index = enc.index;
      }
      double t = (double)j / RATE;
      double amp = t < 0.5 ? 0.2 : 0.8;
      double fade = j < 160 ? j / 160.0 : 1.0;  // short attack
      enc.add((int16_t)(fade * amp * 30000 * wave(j)));
    }
    zone = SampleZone{enc.data.data(), (uint32_t)length, (uint32_t)(looped ? loop_start : 0),
                      (uint32_t)(looped ? length : 0), loop_pred, loop_index, (uint32_t)length,
                      0.0f, 67, 0, 127, 1, 127, 0, 0, 0, marks.data(), (uint16_t)marks.size()};
  }
};

static std::vector<float> run(PsolaSource& p, size_t n) {
  std::vector<float> out(n);
  for (size_t j = 0; j < n; j++) out[j] = p.next();
  return out;
}

/// Time (samples) at which the level crosses the middle between the level
/// before and after the step (low: 0.1 s .. 0.2 s of the signal)
static int stepTime(const std::vector<float>& x, size_t high_from, size_t high_to) {
  const int frame = 2 * PERIOD;
  size_t r = x.size();
  double low = rms(x, r / 10, r / 5), high = rms(x, high_from, high_to);
  for (size_t j = r / 10; j + frame < x.size(); j += 16) {
    if (rms(x, j, j + frame) > 0.5 * (low + high)) return (int)j + frame / 2;
  }
  return -1;
}

static void testUnityIsLossless() {
  StepZone sz(false);
  PsolaSource p;
  p.begin(&sz.zone, 1.0f);
  auto out = run(p, RATE - 2 * PERIOD);
  ImaAdpcmDecoder dec;
  dec.begin(sz.zone.data);
  double max_diff = 0;
  for (size_t j = 0; j < out.size(); j++) max_diff = std::max(max_diff, fabs(out[j] - dec.next() / 32768.0));
  printf("  ratio 1: max difference to the original %g\n", max_diff);
  CHECK(max_diff < 1e-5);
}

static void testPitch() {
  for (double semitones : {-4.0, -2.0, 2.0, 5.0, 7.0}) {
    StepZone sz(true);
    PsolaSource p;
    double ratio = pow(2.0, semitones / 12.0);
    p.begin(&sz.zone, (float)ratio);
    auto out = run(p, RATE * 2);
    std::vector<float> seg(out.begin() + RATE, out.begin() + RATE + 8192);
    double f = estimateFrequency(seg, RATE, 400 * ratio * 0.8, 400 * ratio * 1.25);
    double cents = 1200 * log2(f / (400 * ratio));
    printf("  %+.0f semitones: %.2f Hz (%+.1f cents)\n", semitones, f, cents);
    CHECK_NEAR(cents, 0.0, 5.0);
  }
}

static void testLengthIsKept() {
  // the amplitude step at 0.5 s stays at 0.5 s for any pitch
  for (double ratio : {0.75, 1.0, 1.5}) {
    StepZone sz(false);
    PsolaSource p;
    p.begin(&sz.zone, (float)ratio);
    int t = stepTime(run(p, RATE), RATE * 7 / 10, RATE * 8 / 10);
    printf("  ratio %.2f: step at %.3f s\n", ratio, (double)t / RATE);
    CHECK_NEAR(t, RATE / 2, 2 * PERIOD);
  }
}

static void testContinuity() {
  // no clicks: the steps between samples are not bigger than in the
  // original signal (scaled by the pitch)
  StepZone sz(true);
  PsolaSource orig, p;
  orig.begin(&sz.zone, 1.0f);
  auto x = run(orig, RATE);
  double ratio = pow(2.0, 3 / 12.0);
  p.begin(&sz.zone, (float)ratio);
  auto out = run(p, RATE * 2);
  float orig_step = 0, max_step = 0;
  for (size_t j = RATE / 2 + 400; j < x.size(); j++) orig_step = std::max(orig_step, fabsf(x[j] - x[j - 1]));
  for (size_t j = RATE / 2 + 400; j < out.size(); j++) max_step = std::max(max_step, fabsf(out[j] - out[j - 1]));
  printf("  max step %.4f (original %.4f, ratio %.3f)\n", max_step, orig_step, ratio);
  CHECK(max_step < orig_step * ratio * 1.3f);
}

static void testLevel() {
  // PSOLA keeps the spectral envelope: for this tone (harmonics 1/k) the level
  // changes about with 1/ratio; within +/- 2.5 semitones less than 1.5 dB
  StepZone sz(true);
  PsolaSource a, b;
  a.begin(&sz.zone, 1.0f);
  b.begin(&sz.zone, (float)pow(2.0, 2.5 / 12.0));
  auto x = run(a, RATE * 2), y = run(b, RATE * 2);
  double db = 20 * log10(rms(y, RATE, RATE * 2) / rms(x, RATE, RATE * 2));
  printf("  level at +2.5 semitones: %+.2f dB\n", db);
  CHECK_NEAR(db, 0.0, 1.5);
}

static void testLoopKeepsLevel() {
  StepZone sz(true);
  PsolaSource p;
  p.begin(&sz.zone, 1.3f);
  auto out = run(p, RATE * 4);
  double a = rms(out, RATE, RATE + 4000), b = rms(out, RATE * 4 - 4000, RATE * 4);
  printf("  level after 4 s: %+.2f dB\n", 20 * log10(b / a));
  CHECK_NEAR(20 * log10(b / a), 0.0, 1.0);
  CHECK(!p.finished());
}

static void testOneShotEnds() {
  // without loop: the sound ends after its length, not after length / ratio
  StepZone sz(false);
  PsolaSource p;
  p.begin(&sz.zone, 1.5f);
  size_t n = 0;
  while (!p.finished() && n < (size_t)RATE * 3) {
    p.next();
    n++;
  }
  printf("  one shot with ratio 1.5 ends after %.3f s (length 1 s)\n", (double)n / RATE);
  CHECK_NEAR((double)n / RATE, 1.0, 0.02);
}

static void testFromLoop() {
  StepZone sz(true);
  PsolaSource p;
  p.begin(&sz.zone, 1.2f, true);
  auto out = run(p, 8192);
  double f = estimateFrequency(out, RATE, 400, 600);
  CHECK_NEAR(f, 480.0, 3.0);
  CHECK(rms(out) > 0.3);  // the loop has the high level
}

static void testSynthesizerKeepsLength() {
  // through the synthesizer: Psola keeps the timing, Resample changes it
  StepZone sz(false);
  SampleSet set = {"step", &sz.zone, 1, RATE, 0};
  for (PitchShift mode : {PitchShift::Resample, PitchShift::Psola}) {
    Instrument inst(set);
    inst.pitchShift = mode;
    SampleSynthesizer synth;
    synth.setInstrument(-1, inst);
    synth.begin(44100);
    synth.noteOn(0, 67 + 7, 127);  // a fifth above the recording
    std::vector<float> left(44100), right(44100);
    synth.renderFloat(left.data(), right.data(), 44100);
    // step time in seconds: the high level is measured just before the end
    double expected = mode == PitchShift::Psola ? 0.5 : 0.5 / pow(2.0, 7 / 12.0);
    size_t high = (size_t)(expected * 1.25 * 44100);
    double t = stepTime(left, high, high + 2205) / 44100.0;
    printf("  %s: step at %.3f s\n", mode == PitchShift::Psola ? "Psola   " : "Resample", t);
    if (mode == PitchShift::Psola) CHECK_NEAR(t, 0.5, 0.01);
    else CHECK_NEAR(t, 0.5 / pow(2.0, 7 / 12.0), 0.01);
  }
}

/// Spectral centroid in Hz (DFT of a Hann windowed block)
static double centroid(const std::vector<float>& x, double rate) {
  const int n = 4096;
  double num = 0, den = 0;
  for (int k = 1; k < n / 2; k++) {
    double re = 0, im = 0;
    for (int j = 0; j < n; j++) {
      double w = 0.5 - 0.5 * cos(2 * M_PI * j / n);
      re += w * x[j] * cos(2 * M_PI * k * j / n);
      im -= w * x[j] * sin(2 * M_PI * k * j / n);
    }
    double mag = sqrt(re * re + im * im);
    num += mag * k * rate / n;
    den += mag;
  }
  return num / den;
}

static void testFormantsAreKept() {
  // clarinet one semitone above a recorded note: PSOLA keeps the timbre
  // (spectral centroid), resampling shifts it up with the pitch
  const SampleZone* zone = Clarinet().findZone(62, 90);
  CHECK(zone != nullptr && zone->rootKey == 62);
  CHECK(Clarinet().findZone(63, 90) == zone);  // same recording
  double c[3];
  int notes[3] = {62, 63, 63};
  PitchShift modes[3] = {PitchShift::Psola, PitchShift::Psola, PitchShift::Resample};
  for (int k = 0; k < 3; k++) {
    Instrument inst = ClarinetInstrument(Clarinet());
    inst.pitchShift = modes[k];
    inst.autoVibratoDepth = 0;
    inst.minCutoff = 20000;  // no velocity filter
    SampleSynthesizer synth;
    synth.setInstrument(-1, inst);
    synth.begin(44100);
    synth.noteOn(0, notes[k], 90);
    std::vector<float> l(44100), r(44100);
    synth.renderFloat(l.data(), r.data(), 22050);
    synth.renderFloat(l.data(), r.data(), 4096);
    c[k] = centroid(l, 44100);
  }
  printf("  spectral centroid: recorded %.0f Hz, +1 semitone Psola %.0f Hz (%+.1f%%), Resample %.0f Hz (%+.1f%%)\n",
         c[0], c[1], 100 * (c[1] / c[0] - 1), c[2], 100 * (c[2] / c[0] - 1));
  CHECK(fabs(c[1] / c[0] - 1) < fabs(c[2] / c[0] - 1));
  CHECK(c[2] / c[0] > 1.03);
}

int main() {
  testUnityIsLossless();
  testPitch();
  testLengthIsKept();
  testContinuity();
  testLevel();
  testLoopKeepsLevel();
  testOneShotEnds();
  testFromLoop();
  testSynthesizerKeepsLength();
  testFormantsAreKept();
  return testResult("test_psola");
}
