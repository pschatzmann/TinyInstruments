#include "TinyInstruments.h"
#include "TestHelper.h"
#include "samples/Clarinet.h"

// Clarinet (VSCO 2 CE sustained samples): pitch, sustain, legato, vibrato

static const uint32_t FLASH_BUDGET = 700000;

static std::vector<float> renderLeft(SampleSynthesizer& synth, size_t frames) {
  std::vector<float> left(frames), right(frames);
  synth.renderFloat(left.data(), right.data(), frames);
  return left;
}

static double acAt(const std::vector<float>& x, int lag) {
  double best = -1e30;
  for (int d = -2; d <= 2; d++) {
    double sum = 0;
    for (size_t j = 0; j + lag + d < x.size(); j++) sum += (double)x[j] * x[j + lag + d];
    best = std::max(best, sum);
  }
  return best;
}

static const SamplerVoice* soundingVoice(SampleSynthesizer& synth) {
  for (int j = 0; j < SampleSynthesizer::voiceCount(); j++) {
    const SamplerVoice& v = synth.voice(j);
    if (v.isActive() && !v.fading) return &v;
  }
  return nullptr;
}

static void testFitsIntoFlash() {
  const SampleSet& set = Clarinet();
  printf("%s: %u bytes in %d zones\n", set.name, (unsigned)set.dataSize, set.zoneCount);
  CHECK(set.dataSize == CLARINET_DATA_SIZE);
  CHECK(set.dataSize <= FLASH_BUDGET);
}

static void testPitchAndOctave() {
  Instrument clarinet = ClarinetInstrument(Clarinet());
  SampleSynthesizer synth;
  synth.setInstrument(-1, clarinet);
  // lowest note D3 up to the zone with the corrected root key (89/90)
  for (int note : {50, 52, 55, 60, 64, 67, 72, 79, 84, 89, 90}) {
    double freq = 440.0 * pow(2.0, (note - 69) / 12.0);
    synth.begin(44100);
    synth.noteOn(0, note, 90);
    renderLeft(synth, 13230);
    auto out = renderLeft(synth, 16384);
    double f = estimateFrequency(out, 44100, freq * 0.8, freq * 1.25);
    double cents = 1200.0 * log2(f / freq);
    int lag = (int)(44100 / freq + 0.5);
    bool lower = acAt(out, 2 * lag) > 1.25 * acAt(out, lag);
    printf("  note %d: %.2f Hz (%+.1f cents)%s\n", note, f, cents, lower ? " OCTAVE LOWER" : "");
    CHECK_NEAR(cents, 0.0, 20.0);
    CHECK(!lower);
  }
}

static void testSustainAndRelease() {
  Instrument clarinet = ClarinetInstrument(Clarinet());
  SampleSynthesizer synth;
  synth.setInstrument(-1, clarinet);
  synth.begin(44100);
  synth.noteOn(0, 67, 90);
  renderLeft(synth, 22050);
  double early = rms(renderLeft(synth, 22050));
  renderLeft(synth, 44100 * 3);
  double late = rms(renderLeft(synth, 22050));
  double db = 20 * log10(late / early);
  printf("  level after 4s: %+.1f dB\n", db);
  CHECK_NEAR(db, 0.0, 3.0);
  synth.noteOff(0, 67);
  renderLeft(synth, 13230);
  CHECK(synth.activeVoices() == 0);
}

static void testLegatoAndNoAutoVibrato() {
  Instrument clarinet = ClarinetInstrument(Clarinet());
  SampleSynthesizer synth;
  synth.setInstrument(-1, clarinet);
  synth.begin(44100);
  synth.noteOn(0, 60, 90);
  renderLeft(synth, 44100 * 2);
  // no vibrato on a long note without the modulation wheel
  float lo = 1e9f, hi = 0;
  for (int j = 0; j < 700; j++) {
    renderLeft(synth, 64);
    lo = std::min(lo, soundingVoice(synth)->currentStep());
    hi = std::max(hi, soundingVoice(synth)->currentStep());
  }
  CHECK_NEAR(1200 * log2(hi / lo), 0.0, 0.5);
  // legato: monophonic, without attack
  synth.noteOn(0, 62, 90);
  renderLeft(synth, 64);
  const SamplerVoice* v = soundingVoice(synth);
  CHECK(v && v->note == 62 && v->level() < 0.2f);
  renderLeft(synth, 4410);
  CHECK(synth.activeVoices() == 1);
}

int main() {
  testFitsIntoFlash();
  testPitchAndOctave();
  testSustainAndRelease();
  testLegatoAndNoAutoVibrato();
  return testResult("test_clarinet");
}
