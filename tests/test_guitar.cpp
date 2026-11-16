#include "TinyInstruments.h"
#include "TestHelper.h"
#include "samples/ElectricGuitar.h"
#include "samples/GrandPiano.h"

// Electric guitar (DI samples) with the GuitarAmp effect

static const uint32_t FLASH_BUDGET = 700000;

static std::vector<float> renderLeft(SampleSynthesizer& synth, size_t frames) {
  std::vector<float> left(frames), right(frames);
  synth.renderFloat(left.data(), right.data(), frames);
  return left;
}

/// Share of the energy which is not at the frequency f (harmonics): 0..1
static double harmonicShare(const std::vector<float>& x, double rate, double f) {
  // energy of the fundamental with a single bin DFT over full periods
  size_t n = (size_t)(floor(x.size() * f / rate) * rate / f);
  double re = 0, im = 0, total = 0;
  for (size_t j = 0; j < n; j++) {
    double phase = 2 * M_PI * f * j / rate;
    re += x[j] * cos(phase);
    im += x[j] * sin(phase);
    total += (double)x[j] * x[j];
  }
  double fundamental = 2.0 * (re * re + im * im) / n;
  return total > 0 ? 1.0 - fundamental / total : 0.0;
}

/// Runs a sine through the amplifier
static std::vector<float> ampSine(GuitarAmp& amp, double freq, double amplitude, size_t frames) {
  amp.setup(44100);
  std::vector<float> l(frames), r(frames);
  for (size_t j = 0; j < frames; j++) l[j] = r[j] = amplitude * sin(2 * M_PI * freq * j / 44100);
  for (size_t j = 0; j < frames; j += 64) amp.process(&l[j], &r[j], std::min<size_t>(64, frames - j));
  return std::vector<float>(l.begin() + frames / 2, l.end());  // after the settling
}

static void testFitsIntoFlash() {
  const SampleSet& set = ElectricGuitar();
  printf("%s: %u bytes in %d zones\n", set.name, (unsigned)set.dataSize, set.zoneCount);
  CHECK(set.dataSize == ELECTRICGUITAR_DATA_SIZE);
  CHECK(set.dataSize <= FLASH_BUDGET);
}

static void testPitch() {
  // without the amplifier: the pitch of the samples
  Instrument guitar = GuitarInstrument(ElectricGuitar());
  SampleSynthesizer synth;
  synth.setInstrument(-1, guitar);
  for (int note : {40, 45, 50, 55, 59, 64, 69, 76, 84}) {
    double freq = 440.0 * pow(2.0, (note - 69) / 12.0);
    synth.begin(44100);
    synth.noteOn(0, note, 100);
    renderLeft(synth, 4410);
    auto out = renderLeft(synth, 16384);
    double f = estimateFrequency(out, 44100, freq * 0.8, freq * 1.25);
    double cents = 1200.0 * log2(f / freq);
    printf("  note %d: %.2f Hz (%+.1f cents)\n", note, f, cents);
    CHECK_NEAR(cents, 0.0, 20.0);
  }
}

static void testAllKeysSound() {
  Instrument guitar = GuitarInstrument(ElectricGuitar());
  GuitarAmp amp(GuitarAmp::Lead);
  SampleSynthesizer synth;
  synth.setInstrument(-1, guitar);
  synth.setEffect(-1, &amp);
  bool ok = true;
  for (int note = 40; note <= 88; note++) {
    synth.begin(44100);
    synth.noteOn(0, note, 100);
    auto out = renderLeft(synth, 4410);
    double level = rms(out);
    bool finite = true;
    for (float v : out)
      if (!std::isfinite(v)) finite = false;
    if (!finite || level < 0.001 || level > 1.5) {
      printf("  note %d: rms %f\n", note, level);
      ok = false;
    }
  }
  CHECK(ok);
}

static void testAmpDistortion() {
  // the more drive, the more harmonics
  GuitarAmp clean(GuitarAmp::Clean), crunch(GuitarAmp::Crunch), lead(GuitarAmp::Lead);
  clean.getSettings().chorus = 0;
  clean.update();
  lead.getSettings().echo = 0;
  lead.update();
  double h_clean = harmonicShare(ampSine(clean, 220, 0.1, 44100), 44100, 220);
  double h_crunch = harmonicShare(ampSine(crunch, 220, 0.1, 44100), 44100, 220);
  double h_lead = harmonicShare(ampSine(lead, 220, 0.1, 44100), 44100, 220);
  printf("  harmonics: clean %.1f%%, crunch %.1f%%, lead %.1f%%\n", 100 * h_clean,
         100 * h_crunch, 100 * h_lead);
  CHECK(h_clean < 0.05);
  CHECK(h_crunch > h_clean);
  CHECK(h_lead > h_crunch);
  CHECK(h_lead > 0.2);
}

static void testAmpLevelAndSilence() {
  GuitarAmp lead(GuitarAmp::Lead);
  auto out = ampSine(lead, 110, 1.0, 44100);
  float peak = 0;
  for (float v : out) peak = std::max(peak, fabsf(v));
  CHECK(peak < 1.5f);  // the clipping limits the level
  CHECK(peak > 0.1f);

  // silence in: silence out (after the echo has decayed)
  GuitarAmp amp(GuitarAmp::Lead);
  ampSine(amp, 220, 0.5, 4410);
  std::vector<float> l(64), r(64);
  double tail = 0;
  for (int b = 0; b < 44100 * 6 / 64; b++) {
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(r.begin(), r.end(), 0.0f);
    amp.process(l.data(), r.data(), 64);
    if (b == 10) tail = fabs(l[0]) + fabs(l[32]);
  }
  CHECK(amp.hasTail());
  double end = 0;
  for (float v : l) end += fabs(v);
  CHECK(end < 0.001);
  (void)tail;
}

static void testEffectOnlyOnItsChannel() {
  // the piano on channel 1 must not be affected by the amp on channel 0
  Instrument guitar = GuitarInstrument(ElectricGuitar());
  Instrument piano = PianoInstrument(GrandPiano());
  GuitarAmp amp(GuitarAmp::Lead);
  SampleSynthesizer with_amp, without_amp;
  for (SampleSynthesizer* s : {&with_amp, &without_amp}) {
    s->setInstrument(0, guitar);
    s->setInstrument(1, piano);
    s->begin(44100);
    s->noteOn(1, 60, 100);
  }
  with_amp.setEffect(0, &amp);
  auto a = renderLeft(with_amp, 8820);
  auto b = renderLeft(without_amp, 8820);
  double diff = 0;
  for (size_t j = 0; j < a.size(); j++) diff += fabs(a[j] - b[j]);
  CHECK(diff < 1e-6);
}

static void testPalmMute() {
  Instrument guitar = GuitarInstrument(ElectricGuitar());
  SampleSynthesizer synth;
  synth.setInstrument(-1, guitar);
  synth.begin(44100);
  synth.noteOn(0, 45, 110);
  renderLeft(synth, 22050);
  double open = rms(renderLeft(synth, 4410));
  synth.begin(44100);
  synth.controlChange(0, 20, 127);  // palm mute on
  synth.noteOn(0, 45, 110);
  renderLeft(synth, 22050);
  double muted = rms(renderLeft(synth, 4410));
  printf("  palm mute after 0.5 s: %.1f dB\n", 20 * log10(muted / open));
  CHECK(muted < open * 0.25);
  // off again
  synth.controlChange(0, 20, 0);
  synth.noteOn(0, 45, 110);
  renderLeft(synth, 22050);
  double again = rms(renderLeft(synth, 4410));
  CHECK_NEAR(again / open, 1.0, 0.1);
}

static void testMidiControlsAmp() {
  Instrument guitar = GuitarInstrument(ElectricGuitar());
  GuitarAmp amp(GuitarAmp::Clean);
  SampleSynthesizer synth;
  synth.setInstrument(-1, guitar);
  synth.setEffect(0, &amp);
  synth.begin(44100);
  synth.controlChange(0, 16, 100);  // drive
  renderLeft(synth, 64);
  CHECK_NEAR(amp.getSettings().drive, 100 / 127.0, 0.01);
  synth.controlChange(0, 19, 127);  // preset: lead
  renderLeft(synth, 64);
  CHECK(amp.getSettings().drive > 0.7f);
  synth.controlChange(0, 93, 64);  // chorus
  renderLeft(synth, 64);
  CHECK_NEAR(amp.getSettings().chorus, 64 / 127.0, 0.01);
  // a controller of another channel does not change the amp
  synth.controlChange(1, 16, 0);
  renderLeft(synth, 64);
  CHECK(amp.getSettings().drive > 0.7f);
}

static void testSixStrings() {
  Instrument guitar = GuitarInstrument(ElectricGuitar());
  GuitarAmp amp(GuitarAmp::Crunch);
  SampleSynthesizer synth;
  synth.setInstrument(-1, guitar);
  synth.setEffect(-1, &amp);
  synth.begin(44100);
  for (int note : {40, 47, 52, 56, 59, 64, 67, 71}) synth.noteOn(0, note, 100);  // E major + 2
  renderLeft(synth, 64);
  CHECK(synth.activeNotes() == 6);
}

int main() {
  testFitsIntoFlash();
  testPitch();
  testAllKeysSound();
  testAmpDistortion();
  testAmpLevelAndSilence();
  testEffectOnlyOnItsChannel();
  testPalmMute();
  testMidiControlsAmp();
  testSixStrings();
  return testResult("test_guitar");
}
