#include "TinyInstruments.h"
#include "TestHelper.h"
#include "samples/TenorSax.h"

// Tests with the real tenor saxophone samples: sustain, monophonic legato,
// vibrato and breath control

static const uint32_t FLASH_BUDGET = 700000;

static std::vector<float> renderLeft(SampleSynthesizer& synth, size_t frames) {
  std::vector<float> left(frames), right(frames);
  synth.renderFloat(left.data(), right.data(), frames);
  return left;
}

/// The sounding (not fading) voice
static const SamplerVoice* soundingVoice(SampleSynthesizer& synth) {
  for (int j = 0; j < SampleSynthesizer::voiceCount(); j++) {
    const SamplerVoice& v = synth.voice(j);
    if (v.isActive() && !v.fading) return &v;
  }
  return nullptr;
}

/// true if the autocorrelation at half the period is as strong as at the
/// period: the sound is an octave higher than expected
static bool octaveHigher(const std::vector<float>& x, double rate, double freq) {
  int lag = (int)(rate / freq + 0.5);
  auto ac = [&](int l) {
    double sum = 0;
    for (size_t j = 0; j + l < x.size(); j++) sum += (double)x[j] * x[j + l];
    return sum;
  };
  double full = 0, half = 0;
  for (int d = -2; d <= 2; d++) {
    full = std::max(full, ac(lag + d));
    half = std::max(half, ac(lag / 2 + d));
  }
  return half > 0.8 * full;
}

static void testFitsIntoFlash() {
  const SampleSet& set = TenorSax();
  uint32_t total = 0;
  for (int j = 0; j < set.zoneCount; j++) total += (set.zones[j].length + 1) / 2;
  printf("%s: %u bytes in %d zones\n", set.name, (unsigned)total, set.zoneCount);
  CHECK(total == TENORSAX_DATA_SIZE);
  CHECK(total <= FLASH_BUDGET);
}

static void testPitch() {
  Instrument sax = SaxophoneInstrument(TenorSax());
  sax.autoVibratoDepth = 0;  // stable pitch for the measurement
  SampleSynthesizer synth;
  synth.setInstrument(-1, sax);
  for (int note : {44, 45, 50, 57, 60, 67, 72, 76, 81}) {
    double freq = 440.0 * pow(2.0, (note - 69) / 12.0);
    synth.begin(44100);
    synth.noteOn(0, note, 100);
    renderLeft(synth, 13230);  // skip the attack
    auto out = renderLeft(synth, 16384);
    double f = estimateFrequency(out, 44100, freq * 0.8, freq * 1.25);
    double cents = 1200.0 * log2(f / freq);
    bool octave = octaveHigher(out, 44100, freq);
    printf("  note %d: %.2f Hz (%+.1f cents)%s\n", note, f, cents, octave ? " OCTAVE HIGHER" : "");
    CHECK_NEAR(cents, 0.0, 20.0);
    CHECK(!octave);
  }
}

static void testSustain() {
  // the tone holds its level as long as the key is pressed
  Instrument sax = SaxophoneInstrument(TenorSax());
  SampleSynthesizer synth;
  synth.setInstrument(-1, sax);
  synth.begin(44100);
  synth.noteOn(0, 60, 100);
  renderLeft(synth, 22050);
  double early = rms(renderLeft(synth, 22050));  // 0.5 .. 1.0 s
  renderLeft(synth, 44100 * 3);
  double late = rms(renderLeft(synth, 22050));  // 4.0 .. 4.5 s
  double db = 20 * log10(late / early);
  printf("  level after 4s: %+.1f dB\n", db);
  CHECK_NEAR(db, 0.0, 3.0);
  CHECK(synth.activeVoices() == 1);
  // release: quiet within 0.3 s
  synth.noteOff(0, 60);
  renderLeft(synth, 13230);
  CHECK(synth.activeVoices() == 0);
}

static void testMonophonic() {
  Instrument sax = SaxophoneInstrument(TenorSax());
  SampleSynthesizer synth;
  synth.setInstrument(-1, sax);
  synth.begin(44100);
  synth.noteOn(0, 60, 100);
  synth.noteOn(0, 64, 100);
  synth.noteOn(0, 67, 100);
  renderLeft(synth, 64);
  CHECK(synth.activeNotes() == 1);
  renderLeft(synth, 4410);
  CHECK(synth.activeVoices() == 1);
  CHECK(soundingVoice(synth) && soundingVoice(synth)->note == 67);
}

static void testLegato() {
  Instrument sax = SaxophoneInstrument(TenorSax());
  SampleSynthesizer synth;
  synth.setInstrument(-1, sax);
  synth.begin(44100);
  // detached note: full attack from the start of the sample
  synth.noteOn(0, 60, 100);
  renderLeft(synth, 64);
  CHECK(soundingVoice(synth)->level() > 0.99f);
  renderLeft(synth, 22050);
  // legato: the next note fades in without the attack
  synth.noteOn(0, 62, 100);
  renderLeft(synth, 64);
  const SamplerVoice* v = soundingVoice(synth);
  CHECK(v && v->note == 62);
  CHECK(v && v->level() < 0.2f);
  renderLeft(synth, 4410);
  CHECK(soundingVoice(synth)->level() > 0.99f);
  CHECK(synth.activeVoices() == 1);
  // releasing the upper note returns to the held one (trill)
  synth.noteOff(0, 62);
  renderLeft(synth, 4410);
  v = soundingVoice(synth);
  CHECK(v && v->note == 60 && v->key_down);
  // all released: silence
  synth.noteOff(0, 60);
  renderLeft(synth, 22050);
  CHECK(synth.activeVoices() == 0);
}

/// min / max of the pitch step of the sounding voice over the indicated time
static void stepRange(SampleSynthesizer& synth, int blocks, float& lo, float& hi) {
  lo = 1e9f;
  hi = 0.0f;
  for (int j = 0; j < blocks; j++) {
    renderLeft(synth, 64);
    const SamplerVoice* v = soundingVoice(synth);
    if (!v) return;
    lo = std::min(lo, v->currentStep());
    hi = std::max(hi, v->currentStep());
  }
}

static void testVibrato() {
  Instrument sax = SaxophoneInstrument(TenorSax());
  SampleSynthesizer synth;
  synth.setInstrument(-1, sax);
  synth.begin(44100);
  synth.noteOn(0, 60, 100);
  float lo, hi;
  // no vibrato at the beginning of the note
  stepRange(synth, 200, lo, hi);  // 0.29 s
  CHECK_NEAR(1200 * log2(hi / lo), 0.0, 0.5);
  // automatic vibrato on the long note: +/- 12 cents
  renderLeft(synth, 44100);
  stepRange(synth, 700, lo, hi);  // 1 s = 5 periods
  double cents = 1200 * log2(hi / lo) / 2;
  printf("  auto vibrato: +/- %.1f cents\n", cents);
  CHECK_NEAR(cents, sax.autoVibratoDepth, 2.0);
  // modulation wheel adds +/- 35 cents
  synth.controlChange(0, 1, 127);
  stepRange(synth, 700, lo, hi);
  cents = 1200 * log2(hi / lo) / 2;
  printf("  with modulation wheel: +/- %.1f cents\n", cents);
  CHECK_NEAR(cents, sax.autoVibratoDepth + sax.vibratoDepth, 3.0);
}

static void testBreathController() {
  Instrument sax = SaxophoneInstrument(TenorSax());
  SampleSynthesizer synth;
  synth.setInstrument(-1, sax);
  synth.begin(44100);
  synth.noteOn(0, 60, 100);
  renderLeft(synth, 22050);
  double full = rms(renderLeft(synth, 4410));
  synth.controlChange(0, 2, 32);  // less breath
  renderLeft(synth, 2205);
  double soft = rms(renderLeft(synth, 4410));
  CHECK_NEAR(soft / full, 32.0 / 127.0, 0.05);
  // smooth change: no step in the signal
  synth.controlChange(0, 2, 127);
  auto out = renderLeft(synth, 2205);
  float max_step = 0;
  for (size_t j = 1; j < out.size(); j++) max_step = std::max(max_step, fabsf(out[j] - out[j - 1]));
  CHECK(max_step < 0.2f);
}

int main() {
  testFitsIntoFlash();
  testPitch();
  testSustain();
  testMonophonic();
  testLegato();
  testVibrato();
  testBreathController();
  return testResult("test_sax");
}
