#include "TinyInstruments.h"
#include "TestHelper.h"
#include "samples/ElectricBass.h"

// Electric bass (DI samples of a Jazz bass) with the GuitarAmp Bass preset

static const uint32_t FLASH_BUDGET = 700000;

static std::vector<float> renderLeft(SampleSynthesizer& synth, size_t frames) {
  std::vector<float> left(frames), right(frames);
  synth.renderFloat(left.data(), right.data(), frames);
  return left;
}

/// Autocorrelation of x at the indicated lag (max of +/- 2 samples)
static double acAt(const std::vector<float>& x, int lag) {
  double best = -1e30;
  for (int d = -2; d <= 2; d++) {
    double sum = 0;
    for (size_t j = 0; j + lag + d < x.size(); j++) sum += (double)x[j] * x[j + lag + d];
    best = std::max(best, sum);
  }
  return best;
}

/// Gain of the amp for a sine of the indicated frequency
static double ampGain(GuitarAmp::Preset preset, double freq) {
  GuitarAmp amp(preset);
  amp.getSettings().drive = 0.0f;  // linear: only the filters
  amp.getSettings().echo = 0.0f;
  amp.getSettings().chorus = 0.0f;
  amp.update();
  amp.setup(44100);
  const int n = 44100;
  std::vector<float> l(n), r(n);
  for (int j = 0; j < n; j++) l[j] = r[j] = 0.01 * sin(2 * M_PI * freq * j / 44100);
  for (int j = 0; j < n; j += 64) amp.process(&l[j], &r[j], std::min(64, n - j));
  std::vector<float> in(n / 2), out(l.begin() + n / 2, l.end());
  for (int j = 0; j < n / 2; j++) in[j] = 0.01 * sin(2 * M_PI * freq * j / 44100);
  return rms(out) / rms(in);
}

static void testFitsIntoFlash() {
  const SampleSet& set = ElectricBass();
  printf("%s: %u bytes in %d zones\n", set.name, (unsigned)set.dataSize, set.zoneCount);
  CHECK(set.dataSize == ELECTRICBASS_DATA_SIZE);
  CHECK(set.dataSize <= FLASH_BUDGET);
}

static void testPitchAndOctave() {
  Instrument bass = BassInstrument(ElectricBass());
  SampleSynthesizer synth;
  synth.setInstrument(-1, bass);
  // low E string (E1 = 41 Hz) up to G3; 47 and 48 use the zone with the
  // corrected root key
  for (int note : {28, 33, 38, 43, 47, 48, 52, 55, 60, 67}) {
    double freq = 440.0 * pow(2.0, (note - 69) / 12.0);
    synth.begin(44100);
    synth.noteOn(0, note, 100);
    renderLeft(synth, 4410);
    auto out = renderLeft(synth, 22050);
    double f = estimateFrequency(out, 44100, freq * 0.8, freq * 1.25);
    double cents = 1200.0 * log2(f / freq);
    int lag = (int)(44100 / freq + 0.5);
    bool lower = acAt(out, 2 * lag) > 1.25 * acAt(out, lag);
    bool higher = acAt(out, lag / 2) > 0.8 * acAt(out, lag);
    printf("  note %d: %.2f Hz (%+.1f cents)%s%s\n", note, f, cents,
           lower ? " OCTAVE LOWER" : "", higher ? " OCTAVE HIGHER" : "");
    CHECK_NEAR(cents, 0.0, 20.0);
    CHECK(!lower);
    CHECK(!higher);
  }
}

static void testAllKeysSound() {
  Instrument bass = BassInstrument(ElectricBass());
  GuitarAmp amp(GuitarAmp::Bass);
  SampleSynthesizer synth;
  synth.setInstrument(-1, bass);
  synth.setEffect(-1, &amp);
  bool ok = true;
  for (int note = 28; note <= 72; note++) {
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

static void testBassAmpKeepsLowEnd() {
  // the Bass preset keeps the fundamental of the low E, the guitar presets cut it
  double bass_41 = ampGain(GuitarAmp::Bass, 41.2), bass_200 = ampGain(GuitarAmp::Bass, 200);
  double guitar_41 = ampGain(GuitarAmp::Crunch, 41.2), guitar_200 = ampGain(GuitarAmp::Crunch, 200);
  double bass_db = 20 * log10(bass_41 / bass_200), guitar_db = 20 * log10(guitar_41 / guitar_200);
  printf("  41 Hz relative to 200 Hz: bass preset %+.1f dB, crunch preset %+.1f dB\n", bass_db,
         guitar_db);
  CHECK(bass_db > -6.0);
  CHECK(guitar_db < -12.0);
}

static void testFourStringsAndMute() {
  Instrument bass = BassInstrument(ElectricBass());
  SampleSynthesizer synth;
  synth.setInstrument(-1, bass);
  synth.begin(44100);
  for (int note : {28, 33, 38, 43, 47, 52}) synth.noteOn(0, note, 100);
  renderLeft(synth, 64);
  CHECK(synth.activeNotes() == 4);

  synth.begin(44100);
  synth.noteOn(0, 33, 110);
  renderLeft(synth, 22050);
  double open = rms(renderLeft(synth, 4410));
  synth.begin(44100);
  synth.controlChange(0, 20, 127);  // palm mute
  synth.noteOn(0, 33, 110);
  renderLeft(synth, 22050);
  double muted = rms(renderLeft(synth, 4410));
  printf("  palm mute after 0.5 s: %.1f dB\n", 20 * log10(muted / open));
  CHECK(muted < open * 0.4);
}

int main() {
  testFitsIntoFlash();
  testPitchAndOctave();
  testAllKeysSound();
  testBassAmpKeepsLowEnd();
  testFourStringsAndMute();
  return testResult("test_bass");
}
