#include "TinyInstruments.h"
#include "TestHelper.h"
#include "samples/DrumKit.h"

// Tests with the real drum kit samples (General MIDI percussion keys)

static const uint32_t FLASH_BUDGET = 700000;
static const int DRUMS = 9;  // midi channel 10

static std::vector<float> renderLeft(SampleSynthesizer& synth, size_t frames) {
  std::vector<float> left(frames), right(frames);
  synth.renderFloat(left.data(), right.data(), frames);
  return left;
}

static bool isPlaying(SampleSynthesizer& synth, int note) {
  for (int j = 0; j < SampleSynthesizer::voiceCount(); j++)
    if (synth.voice(j).isActive() && synth.voice(j).note == note) return true;
  return false;
}

static void testFitsIntoFlash() {
  const SampleSet& set = DrumKit();
  uint32_t total = 0;
  for (int j = 0; j < set.zoneCount; j++) total += (set.zones[j].length + 1) / 2;
  printf("%s: %u bytes in %d zones\n", set.name, (unsigned)total, set.zoneCount);
  CHECK(total == DRUMKIT_DATA_SIZE);
  CHECK(total <= FLASH_BUDGET);
}

static void testGeneralMidiKeys() {
  Instrument drums = DrumInstrument(DrumKit());
  SampleSynthesizer synth;
  synth.setInstrument(DRUMS, drums);
  // the main kit pieces must exist
  for (int note : {35, 36, 38, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 53, 57, 59}) {
    for (int vel : {30, 127}) {
      synth.begin(44100);
      synth.noteOn(DRUMS, note, vel);
      double level = rms(renderLeft(synth, 4410));
      // the kit keeps the recorded balance: soft cymbal taps are quiet
      if (level < 0.0001) printf("  note %d vel %d: rms %f\n", note, vel, level);
      CHECK(level >= 0.0001);
    }
  }
  // keys without sample are silent (no transposition of other drums)
  synth.begin(44100);
  synth.noteOn(DRUMS, 65, 100);  // high timbale: not sampled
  synth.noteOn(DRUMS, 20, 100);
  renderLeft(synth, 100);
  CHECK(synth.activeVoices() == 0);
}

static void testLouderWithVelocity() {
  Instrument drums = DrumInstrument(DrumKit());
  SampleSynthesizer synth;
  synth.setInstrument(DRUMS, drums);
  for (int note : {36, 38, 42, 49}) {
    double last = 0;
    bool increasing = true;
    for (int vel : {20, 60, 100, 127}) {
      synth.begin(44100);
      synth.noteOn(DRUMS, note, vel);
      double level = rms(renderLeft(synth, 8820));
      if (level <= last) increasing = false;
      last = level;
    }
    CHECK(increasing);
  }
}

static void testOneShotsEnd() {
  // all sounds end by themselves (note off is ignored)
  Instrument drums = DrumInstrument(DrumKit());
  SampleSynthesizer synth;
  synth.setInstrument(DRUMS, drums);
  bool ok = true;
  for (int note = 35; note <= 84; note++) {
    synth.begin(44100);
    synth.noteOn(DRUMS, note, 127);
    synth.noteOff(DRUMS, note);
    renderLeft(synth, 44100 * 20);  // cymbals ring long (until -80 dB)
    if (synth.activeVoices() != 0) {
      printf("  note %d still sounding after 20s\n", note);
      ok = false;
    }
  }
  CHECK(ok);
  // short sounds end quickly
  synth.begin(44100);
  synth.noteOn(DRUMS, 42, 100);  // closed hi-hat
  renderLeft(synth, 44100);
  CHECK(synth.activeVoices() == 0);
}

static void testHiHatChoke() {
  Instrument drums = DrumInstrument(DrumKit());
  SampleSynthesizer synth;
  synth.setInstrument(DRUMS, drums);
  synth.begin(44100);
  synth.noteOn(DRUMS, 46, 110);  // open hi-hat rings for seconds
  synth.noteOn(DRUMS, 49, 110);  // crash: not affected by the hi-hat
  renderLeft(synth, 8820);
  CHECK(isPlaying(synth, 46));
  synth.noteOn(DRUMS, 44, 80);  // pedal closes the hi-hat
  renderLeft(synth, 2205);
  CHECK(!isPlaying(synth, 46));
  CHECK(isPlaying(synth, 49));
  CHECK(isPlaying(synth, 44));
}

static void testGroove() {
  // piano and drums together: shared voices, no overload
  Instrument drums = DrumInstrument(DrumKit());
  SampleSynthesizer synth;
  synth.setInstrument(DRUMS, drums);
  synth.begin(44100);
  std::vector<int16_t> out(2 * 5512);
  int peak = 0;
  for (int bar = 0; bar < 4; bar++) {
    for (int step = 0; step < 8; step++) {
      synth.noteOn(DRUMS, 42, step % 2 ? 70 : 100);
      if (step % 4 == 0) synth.noteOn(DRUMS, 36, 120);
      if (step % 4 == 2) synth.noteOn(DRUMS, 38, 110);
      if (step == 0 && bar == 0) synth.noteOn(DRUMS, 49, 120);
      synth.render(out.data(), 5512);
      for (int16_t v : out) peak = std::max(peak, abs((int)v));
      CHECK(synth.activeNotes() <= drums.maxNotes);
    }
  }
  CHECK(peak > 3000);
  CHECK(peak < 32767);
}

int main() {
  testFitsIntoFlash();
  testGeneralMidiKeys();
  testLouderWithVelocity();
  testOneShotsEnd();
  testHiHatChoke();
  testGroove();
  return testResult("test_drums");
}
