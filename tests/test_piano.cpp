#include "TinyInstruments.h"
#include "TestHelper.h"
#include "samples/GrandPiano.h"
#include "samples/UprightPianoKW.h"

// Tests with the real piano samples: all tests run for every piano

/// Budget for the sample data so that the piano fits into the default ESP32
/// app partition (1.25 MB) together with the program
static const uint32_t FLASH_BUDGET = 700000;

static std::vector<float> renderLeft(SampleSynthesizer& synth, size_t frames) {
  std::vector<float> left(frames), right(frames);
  synth.renderFloat(left.data(), right.data(), frames);
  return left;
}

static void testFitsIntoFlash(const SampleSet& set, uint32_t dataSize) {
  uint32_t total = 0;
  for (int j = 0; j < set.zoneCount; j++) total += (set.zones[j].length + 1) / 2;
  printf("%s: %u bytes in %d zones\n", set.name, (unsigned)total, set.zoneCount);
  CHECK(total == set.dataSize);
  CHECK(total == dataSize);
  CHECK(total <= FLASH_BUDGET);
}

static void testZonesAreConsistent(const SampleSet& set) {
  bool ok = true;
  for (int j = 0; j < set.zoneCount; j++) {
    const SampleZone& z = set.zones[j];
    if (!(z.loopStart < z.loopEnd && z.loopEnd <= z.length)) ok = false;
    if (!(z.decayStart <= z.loopStart)) ok = false;
    if (!(z.loKey <= z.rootKey + 2 && z.rootKey <= z.hiKey + 2)) ok = false;
    if (!(z.decayLn < 0.0f)) ok = false;
    if (z.loopIndex > 88) ok = false;
  }
  CHECK(ok);
  // every key and velocity has a zone
  bool all = true;
  for (int note = 21; note <= 108; note++)
    for (int vel = 1; vel <= 127; vel++)
      if (set.findZone(note, vel) == nullptr) all = false;
  CHECK(all);
}

static void testAllKeysSound(const SampleSet& set) {
  Instrument piano = PianoInstrument(set);
  SampleSynthesizer synth;
  synth.setInstrument(-1, piano);
  synth.begin(44100);
  bool ok = true;
  for (int note = 21; note <= 108; note++) {
    for (int vel : {20, 80, 81, 127}) {
      synth.begin(44100);
      synth.noteOn(0, note, vel);
      auto out = renderLeft(synth, 4410);
      double level = rms(out);
      bool finite = true;
      for (float v : out)
        if (!std::isfinite(v)) finite = false;
      if (!finite || level < 0.0005 || level > 1.0) {
        printf("  note %d vel %d: rms %f finite %d\n", note, vel, level, finite);
        ok = false;
      }
    }
  }
  CHECK(ok);
}

static void testPitch(const SampleSet& set) {
  Instrument piano = PianoInstrument(set);
  SampleSynthesizer synth;
  synth.setInstrument(-1, piano);
  // notes which are recorded (root) and notes which are transposed
  struct { int note; double freq; } notes[] = {
      {57, 220.0}, {58, 233.08}, {69, 440.0}, {70, 466.16}, {81, 880.0}, {64, 329.63}};
  for (auto n : notes) {
    synth.begin(44100);
    synth.noteOn(0, n.note, 100);
    renderLeft(synth, 2205);  // skip the attack
    auto out = renderLeft(synth, 8192);
    double f = estimateFrequency(out, 44100, n.freq * 0.8, n.freq * 1.25);
    double cents = 1200.0 * log2(f / n.freq);
    printf("  note %d: %.2f Hz (%+.1f cents)\n", n.note, f, cents);
    CHECK_NEAR(cents, 0.0, 30.0);
  }
}

static void testVelocityLayers(const SampleSet& set) {
  Instrument piano = PianoInstrument(set);
  SampleSynthesizer synth;
  synth.setInstrument(-1, piano);
  // louder with higher velocity, also across the layer boundary
  double last = 0;
  bool increasing = true;
  for (int vel : {10, 40, 80, 81, 100, 127}) {
    synth.begin(44100);
    synth.noteOn(0, 60, vel);
    double level = rms(renderLeft(synth, 8820));
    if (level <= last) increasing = false;
    last = level;
  }
  CHECK(increasing);
}

static void testNaturalDecayAndRelease(const SampleSet& set) {
  Instrument piano = PianoInstrument(set);
  SampleSynthesizer synth;
  synth.setInstrument(-1, piano);
  synth.begin(44100);
  synth.noteOn(0, 48, 100);
  double first = rms(renderLeft(synth, 22050));
  renderLeft(synth, 44100);
  double later = rms(renderLeft(synth, 22050));
  CHECK(later < first);    // decays
  CHECK(later > 0.0001);   // but still sounding (looped)
  synth.noteOff(0, 48);
  renderLeft(synth, 44100);
  CHECK(synth.activeVoices() == 0);
}

static void testTenFingers(const SampleSet& set) {
  // 10 notes at the same time: all must sound
  Instrument piano = PianoInstrument(set);
  SampleSynthesizer synth;
  synth.setInstrument(-1, piano);
  synth.begin(44100);
  int notes[10] = {36, 43, 48, 52, 55, 60, 64, 67, 72, 76};
  for (int n : notes) synth.noteOn(0, n, 100);
  std::vector<int16_t> out(2 * 44100);
  synth.render(out.data(), 44100);
  CHECK(synth.activeNotes() == 10);
  int peak = 0;
  for (int16_t v : out) peak = std::max(peak, abs((int)v));
  CHECK(peak > 3000);
  CHECK(peak < 32767);  // no clipping with the default volume
}

int main() {
  testFitsIntoFlash(UprightPianoKW(), UPRIGHTPIANOKW_DATA_SIZE);
  testFitsIntoFlash(GrandPiano(), GRANDPIANO_DATA_SIZE);
  for (const SampleSet* set : {&UprightPianoKW(), &GrandPiano()}) {
    printf("%s\n", set->name);
    testZonesAreConsistent(*set);
    testAllKeysSound(*set);
    testPitch(*set);
    testVelocityLayers(*set);
    testNaturalDecayAndRelease(*set);
    testTenFingers(*set);
  }
  return testResult("test_piano");
}
