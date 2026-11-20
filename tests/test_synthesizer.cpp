#include "TinyInstruments.h"
#include "TestHelper.h"

// SampleSynthesizer tests with a synthetic sine sample set

static SineZone sine;
static SampleSet sineSet = {"sine", &sine.zone, 1, 32000, 0};

static std::vector<float> renderLeft(SampleSynthesizer& synth, size_t frames) {
  std::vector<float> left(frames), right(frames);
  synth.renderFloat(left.data(), right.data(), frames);
  return left;
}

static void testNoteOnOff() {
  Instrument inst(sineSet);
  SampleSynthesizer synth;
  synth.setInstrument(-1, inst);
  synth.begin(44100);
  CHECK(synth.activeVoices() == 0);
  synth.noteOn(0, 67, 127);
  auto out = renderLeft(synth, 4410);
  CHECK(synth.activeVoices() == 1);
  CHECK(rms(out) > 0.05);
  CHECK_NEAR(estimateFrequency(out, 44100, 200, 1000), 400.0, 1.0);

  synth.noteOff(0, 67);
  renderLeft(synth, 44100);
  CHECK(synth.activeVoices() == 0);
}

static void testTransposition() {
  Instrument inst(sineSet);
  SampleSynthesizer synth;
  synth.setInstrument(-1, inst);
  synth.begin(48000);  // different output rate
  synth.noteOn(0, 67 + 7, 127);  // fifth above the root
  auto out = renderLeft(synth, 8192);
  CHECK_NEAR(estimateFrequency(out, 48000, 300, 1200), 400.0 * pow(2.0, 7 / 12.0), 2.0);
}

// setTranspose(): notes of a channel are played lower / higher, note off
// still matches when the transposition changes while the note is held
static void testChannelTranspose() {
  Instrument inst(sineSet);
  SampleSynthesizer synth;
  synth.setInstrument(-1, inst);
  synth.begin(44100);
  synth.setTranspose(2, -12);
  CHECK(synth.transpose(2) == -12 && synth.transpose(0) == 0);
  synth.noteOn(2, 67, 127);  // root 67 = 400 Hz: one octave lower
  auto out = renderLeft(synth, 8192);
  CHECK_NEAR(estimateFrequency(out, 44100, 120, 500), 200.0, 1.0);
  // the transposition changes while the note is held: note off still works
  synth.setTranspose(2, 5);
  synth.noteOff(2, 67);
  renderLeft(synth, 44100);
  CHECK(synth.activeVoices() == 0);
  // other channels are not transposed
  synth.noteOn(0, 67, 127);
  out = renderLeft(synth, 8192);
  CHECK_NEAR(estimateFrequency(out, 44100, 200, 1000), 400.0, 1.0);
  synth.noteOff(0, 67);
  renderLeft(synth, 44100);
  // all channels: re-strike of the same key replaces the note
  synth.setTranspose(-1, 12);
  synth.noteOn(1, 60, 127);
  synth.noteOn(1, 60, 127);
  renderLeft(synth, 4410);
  CHECK(synth.activeNotes() == 1);
  synth.noteOff(1, 60);
  renderLeft(synth, 44100);
  CHECK(synth.activeVoices() == 0);
  // monophonic instrument (legato) and organ: note off with the received key
  Instrument mono(sineSet);
  mono.monophonic = true;
  mono.legato = true;
  mono.maxNotes = 1;
  TonewheelOrgan tonewheels("008000000");
  Instrument organ = OrganInstrument(tonewheels);
  synth.setTranspose(-1, -24);
  synth.setInstrument(3, mono);
  synth.setInstrument(4, organ);
  synth.noteOn(3, 79, 127);
  synth.noteOn(3, 81, 127);
  synth.noteOn(4, 93, 127);  // A6 two octaves lower: A4 = 440 Hz
  auto organ_out = renderLeft(synth, 8192);
  synth.noteOff(3, 81);
  synth.noteOff(3, 79);
  synth.noteOff(4, 93);
  renderLeft(synth, 44100);
  CHECK(synth.activeVoices() == 0);
  SampleSynthesizer organ_synth;
  organ_synth.setInstrument(-1, organ);
  organ_synth.begin(44100);
  organ_synth.setTranspose(0, -24);
  organ_synth.noteOn(0, 93, 127);
  out = renderLeft(organ_synth, 8192);
  CHECK_NEAR(estimateFrequency(out, 44100, 350, 550), 440.0, 1.5);

  // transposition of the instrument: added to the one of the channel
  Instrument low(sineSet);
  low.transpose = -12;
  SampleSynthesizer synth2;
  synth2.setInstrument(-1, low);
  synth2.begin(44100);
  synth2.noteOn(0, 67, 127);  // 400 Hz one octave lower
  out = renderLeft(synth2, 8192);
  CHECK_NEAR(estimateFrequency(out, 44100, 120, 500), 200.0, 1.0);
  synth2.noteOff(0, 67);
  renderLeft(synth2, 44100);
  CHECK(synth2.activeVoices() == 0);
  synth2.setTranspose(0, 12);  // channel + instrument = 0
  synth2.noteOn(0, 67, 127);
  out = renderLeft(synth2, 8192);
  CHECK_NEAR(estimateFrequency(out, 44100, 200, 1000), 400.0, 1.0);
}

static void testVelocity() {
  Instrument inst(sineSet);
  SampleSynthesizer synth;
  synth.setInstrument(-1, inst);
  synth.begin(44100);
  synth.noteOn(0, 67, 127);
  double loud = rms(renderLeft(synth, 4410));
  synth.begin(44100);
  synth.noteOn(0, 67, 40);
  double soft = rms(renderLeft(synth, 4410));
  CHECK(soft < loud * 0.5);
  CHECK(soft > 0.0);
}

static void testSustainPedal() {
  Instrument inst(sineSet);
  SampleSynthesizer synth;
  synth.setInstrument(-1, inst);
  synth.begin(44100);
  synth.controlChange(0, 64, 127);
  synth.noteOn(0, 67, 100);
  renderLeft(synth, 1000);
  synth.noteOff(0, 67);
  renderLeft(synth, 44100);
  CHECK(synth.activeVoices() == 1);  // held by the pedal
  synth.controlChange(0, 64, 0);
  renderLeft(synth, 44100);
  CHECK(synth.activeVoices() == 0);
}

static void testUndampedKeys() {
  Instrument inst(sineSet);
  inst.undampedKey = 60;
  SampleSynthesizer synth;
  synth.setInstrument(-1, inst);
  synth.begin(44100);
  synth.noteOn(0, 67, 100);
  renderLeft(synth, 1000);
  synth.noteOff(0, 67);
  renderLeft(synth, 44100);
  CHECK(synth.activeVoices() == 1);  // no damper: keeps on sounding
}

static void testRestrike() {
  Instrument inst(sineSet);
  SampleSynthesizer synth;
  synth.setInstrument(-1, inst);
  synth.begin(44100);
  synth.noteOn(0, 67, 100);
  renderLeft(synth, 1000);
  synth.noteOn(0, 67, 100);
  renderLeft(synth, 100);
  CHECK(synth.activeVoices() == 2);  // old one is fading out
  renderLeft(synth, 4410);
  CHECK(synth.activeVoices() == 1);
}

static void testPolyphonyLimit() {
  Instrument inst(sineSet);
  SampleSynthesizer synth;
  synth.setInstrument(-1, inst);
  synth.begin(44100);
  CHECK(synth.maxNotes() == 10);
  for (int n = 0; n < 20; n++) {
    synth.noteOn(0, 40 + n, 100);
    renderLeft(synth, 64);
    CHECK(synth.activeNotes() <= 10);
    CHECK(synth.activeVoices() <= SampleSynthesizer::voiceCount());
  }
  CHECK(synth.activeNotes() == 10);
  // the stolen notes fade out quickly
  renderLeft(synth, 4410);
  CHECK(synth.activeVoices() == 10);
  // the most recent notes are playing
  bool newest = false, oldest = false;
  for (int j = 0; j < SampleSynthesizer::voiceCount(); j++) {
    if (!synth.voice(j).isActive()) continue;
    if (synth.voice(j).note == 59) newest = true;
    if (synth.voice(j).note == 40) oldest = true;
  }
  CHECK(newest);
  CHECK(!oldest);
}

static void testReleasedNotesAreStolenFirst() {
  Instrument inst(sineSet);
  SampleSynthesizer synth;
  synth.setInstrument(-1, inst);
  synth.begin(44100, 4);
  for (int n = 0; n < 4; n++) synth.noteOn(0, 60 + n, 100);
  renderLeft(synth, 100);
  synth.noteOff(0, 62);  // released: should be replaced first
  renderLeft(synth, 100);
  synth.noteOn(0, 70, 100);
  renderLeft(synth, 4410);
  bool released_gone = true, first_kept = false;
  for (int j = 0; j < SampleSynthesizer::voiceCount(); j++) {
    if (!synth.voice(j).isActive()) continue;
    if (synth.voice(j).note == 62) released_gone = false;
    if (synth.voice(j).note == 60) first_kept = true;
  }
  CHECK(released_gone);
  CHECK(first_kept);
}

static void testStealingDoesNotClick() {
  Instrument inst(sineSet);
  SampleSynthesizer synth;
  synth.setInstrument(-1, inst);
  synth.begin(44100, 1);
  synth.noteOn(0, 67, 127);
  auto a = renderLeft(synth, 4410);
  synth.noteOn(0, 79, 127);  // steals the first note
  auto b = renderLeft(synth, 4410);
  a.insert(a.end(), b.begin(), b.end());
  float max_step = 0;
  for (size_t j = 1; j < a.size(); j++) max_step = std::max(max_step, fabsf(a[j] - a[j - 1]));
  // slope of the sine one octave higher + fading part
  CHECK(max_step < 0.5f * 0.5f * 2 * M_PI * 800 / 44100 * 2.0);
}

static void testChannels() {
  Instrument inst(sineSet);
  SampleSynthesizer synth;
  synth.setInstrument(1, inst);  // only channel 1 has an instrument
  synth.begin(44100);
  synth.noteOn(0, 67, 100);
  synth.noteOn(1, 67, 100);
  renderLeft(synth, 100);
  CHECK(synth.activeVoices() == 1);
  // all notes off on channel 1
  synth.controlChange(1, 123, 0);
  renderLeft(synth, 44100);
  CHECK(synth.activeVoices() == 0);
}

static void testVolumeAndPitchBend() {
  Instrument inst(sineSet);
  SampleSynthesizer synth;
  synth.setInstrument(-1, inst);
  synth.begin(44100);
  synth.noteOn(0, 67, 127);
  double normal = rms(renderLeft(synth, 4410));
  synth.controlChange(0, 7, 50);
  renderLeft(synth, 2205);  // volume changes are smoothed
  double reduced = rms(renderLeft(synth, 4410));
  CHECK_NEAR(reduced / normal, 50.0 / 100.0, 0.05);

  synth.pitchBend(0, 16383);  // + 2 semitones
  auto out = renderLeft(synth, 8192);
  CHECK_NEAR(estimateFrequency(out, 44100, 200, 1000), 400.0 * pow(2.0, 2.0 / 12.0), 2.0);
  synth.pitchBend(0, 8192);
  out = renderLeft(synth, 8192);
  CHECK_NEAR(estimateFrequency(out, 44100, 200, 1000), 400.0, 1.0);
}

// B6 5B xx: CC91 on midi channel 7 mapped to the volume (CC7)
static void testControllerMapping() {
  Instrument inst(sineSet);
  SampleSynthesizer synth;
  synth.setInstrument(-1, inst);
  synth.begin(44100);
  synth.noteOn(6, 67, 127);
  double normal = rms(renderLeft(synth, 4410));
  // without mapping CC91 has no effect
  synth.controlChange(6, 91, 0);
  renderLeft(synth, 2205);
  CHECK_NEAR(rms(renderLeft(synth, 4410)) / normal, 1.0, 0.02);
  synth.mapController(91, 7);
  CHECK(synth.controllerMapping(91) == 7);
  synth.controlChange(6, 91, 50);
  renderLeft(synth, 2205);  // volume changes are smoothed
  CHECK_NEAR(rms(renderLeft(synth, 4410)) / normal, 50.0 / 100.0, 0.05);
  synth.controlChange(6, 91, 0x7F);
  renderLeft(synth, 2205);
  CHECK_NEAR(rms(renderLeft(synth, 4410)) / normal, 127.0 / 100.0, 0.05);
  // the volume of the other channels is not changed
  synth.noteOn(0, 67, 127);
  renderLeft(synth, 2205);
  // ignored controller: no effect
  synth.mapController(7, SampleSynthesizer::CC_IGNORE);
  synth.mapController(91, 91);
  double before = rms(renderLeft(synth, 4410));
  synth.controlChange(6, 7, 0);
  renderLeft(synth, 2205);
  CHECK_NEAR(rms(renderLeft(synth, 4410)) / before, 1.0, 0.02);
  // allNotesOff() is not affected by the mapping
  synth.mapController(123, SampleSynthesizer::CC_IGNORE);
  synth.allNotesOff();
  renderLeft(synth, 44100);
  CHECK(synth.activeVoices() == 0);
}

static void testInt16Output() {
  Instrument inst(sineSet);
  SampleSynthesizer synth;
  synth.setInstrument(-1, inst);
  synth.begin(44100);
  synth.setVolume(4.0f);  // overdrive: the limiter must prevent wrap around
  for (int n = 0; n < 8; n++) synth.noteOn(0, 60 + n, 127);
  std::vector<int16_t> out(2 * 4410);
  CHECK(synth.render(out.data(), 4410) == 4410);
  int max_value = 0;
  bool wrapped = false;
  for (size_t j = 2; j < out.size(); j += 2) {
    max_value = std::max(max_value, abs((int)out[j]));
    if (abs(out[j] - out[j - 2]) > 40000) wrapped = true;
  }
  CHECK(max_value > 29000);
  CHECK(!wrapped);
}

static void testEventQueueOverflow() {
  Instrument inst(sineSet);
  SampleSynthesizer synth;
  synth.setInstrument(-1, inst);
  synth.begin(44100);
  int accepted = 0;
  for (int j = 0; j < 1000; j++) accepted += synth.noteOn(0, 60, 100) ? 1 : 0;
  CHECK(accepted == TI_EVENT_QUEUE_SIZE - 1);
  renderLeft(synth, 100);  // must not crash
  CHECK(synth.noteOn(0, 60, 100));
}

/// Drum kit with 3 one key zones: 36 (kick), 42 (closed hh, group 1), 46 (open hh, off by 1)
struct TestDrums {
  SineZone a, b, c;
  SampleZone zones[3];
  SampleSet set;
  TestDrums() {
    zones[0] = a.zone; zones[0].rootKey = zones[0].loKey = zones[0].hiKey = 36;
    zones[1] = b.zone; zones[1].rootKey = zones[1].loKey = zones[1].hiKey = 42; zones[1].group = 1;
    zones[2] = c.zone; zones[2].rootKey = zones[2].loKey = zones[2].hiKey = 46; zones[2].offBy = 1;
    set = SampleSet{"drums", zones, 3, 32000, 0};
  }
};

static void testDrumsExactKeysAndNoteOff() {
  TestDrums drums;
  Instrument inst = DrumInstrument(drums.set);
  SampleSynthesizer synth;
  synth.setInstrument(9, inst);
  synth.begin(44100);
  synth.noteOn(9, 37, 100);  // no zone: silent
  renderLeft(synth, 100);
  CHECK(synth.activeVoices() == 0);
  synth.noteOn(9, 36, 100);
  renderLeft(synth, 100);
  synth.noteOff(9, 36);  // ignored: one shot
  renderLeft(synth, 4410);
  CHECK(synth.activeVoices() == 1);
  // not transposed
  auto out = renderLeft(synth, 8192);
  CHECK_NEAR(estimateFrequency(out, 44100, 200, 1000), 400.0, 1.0);
}

static void testChokeGroup() {
  TestDrums drums;
  Instrument inst = DrumInstrument(drums.set);
  SampleSynthesizer synth;
  synth.setInstrument(9, inst);
  synth.begin(44100);
  synth.noteOn(9, 46, 100);  // open hi-hat
  synth.noteOn(9, 36, 100);  // kick: not affected
  renderLeft(synth, 1000);
  CHECK(synth.activeVoices() == 2);
  synth.noteOn(9, 42, 100);  // closed hi-hat chokes the open one
  renderLeft(synth, 4410);
  bool open = false, kick = false, closed = false;
  for (int j = 0; j < SampleSynthesizer::voiceCount(); j++) {
    if (!synth.voice(j).isActive()) continue;
    if (synth.voice(j).note == 46) open = true;
    if (synth.voice(j).note == 36) kick = true;
    if (synth.voice(j).note == 42) closed = true;
  }
  CHECK(!open);
  CHECK(kick);
  CHECK(closed);
}

static void testInstrumentPolyphony() {
  // piano (10 notes) and drums share the voices: the drums are limited separately
  Instrument piano(sineSet);
  Instrument drums(sineSet);
  drums.maxNotes = 2;
  SampleSynthesizer synth;
  synth.setInstrument(0, piano);
  synth.setInstrument(9, drums);
  synth.begin(44100);
  for (int n = 0; n < 5; n++) synth.noteOn(0, 60 + n, 100);
  for (int n = 0; n < 5; n++) synth.noteOn(9, 40 + n, 100);
  renderLeft(synth, 4410);
  CHECK(synth.activeNotes() == 7);
}

/// Effect which only counts the processed frames
struct CountingEffect : public ChannelEffect {
  long frames = 0;
  void begin(uint32_t) override {}
  void process(float*, float*, int n) override { frames += n; }
};

static void testInstrumentBank() {
  Instrument a(sineSet), b(sineSet);
  InstrumentBank bank;
  CHECK(bank.add(0, 7, a));
  CHECK(bank.add(24, 31, b));
  CHECK(bank.add(30, 30, a));  // later entries take precedence
  CHECK(!bank.add(10, 5, a));  // invalid range
  CHECK(bank.find(0)->instrument == &a);
  CHECK(bank.find(7)->instrument == &a);
  CHECK(bank.find(25)->instrument == &b);
  CHECK(bank.find(30)->instrument == &a);
  CHECK(bank.find(8) == nullptr);
  CHECK(bank.size() == 3);
}

static void testProgramChange() {
  Instrument piano(sineSet);
  Instrument guitar(sineSet);
  guitar.maxNotes = 1;  // to see which instrument plays
  CountingEffect amp;
  InstrumentBank bank;
  bank.add(0, 7, piano);
  bank.add(24, 31, guitar, &amp);
  SampleSynthesizer synth;
  synth.setInstrumentBank(&bank);
  synth.begin(44100);
  CHECK(synth.program(0) == -1);

  // no instrument before a program change
  synth.noteOn(0, 67, 100);
  renderLeft(synth, 64);
  CHECK(synth.activeVoices() == 0);

  synth.programChange(0, 0);
  synth.noteOn(0, 60, 100);
  synth.noteOn(0, 64, 100);
  renderLeft(synth, 64);
  CHECK(synth.program(0) == 0);
  CHECK(synth.activeNotes() == 2);
  CHECK(amp.frames == 0);

  // guitar with the effect: the piano notes fade out
  synth.programChange(0, 26);
  synth.noteOn(0, 67, 100);
  synth.noteOn(0, 69, 100);
  renderLeft(synth, 4410);
  CHECK(synth.program(0) == 26);
  CHECK(synth.activeNotes() == 1);  // guitar.maxNotes
  CHECK(synth.activeVoices() == 1);
  CHECK(amp.frames > 0);

  // unknown program: no change
  synth.programChange(0, 100);
  renderLeft(synth, 64);
  CHECK(synth.program(0) == 26);

  // other channels are not affected
  CHECK(synth.program(1) == -1);

  // General MIDI: program changes on channel 10 select the drum kit
  Instrument drums = DrumInstrument(sineSet);
  bank.setDrumKit(drums);
  CHECK(bank.find(9, 0)->instrument == &drums);
  CHECK(bank.find(9, 30)->instrument == &drums);
  CHECK(bank.find(0, 0)->instrument == &piano);
  synth.programChange(9, 0);
  renderLeft(synth, 64);
  CHECK(synth.program(9) == 0);
  // the drum kit ignores note off (one shot): the note keeps on sounding
  synth.noteOn(9, 67, 100);
  renderLeft(synth, 64);
  synth.noteOff(9, 67);
  renderLeft(synth, 44100);
  bool sounding = false;
  for (int j = 0; j < SampleSynthesizer::voiceCount(); j++)
    if (synth.voice(j).isActive() && synth.voice(j).channel == 9) sounding = true;
  CHECK(sounding);
}

int main() {
  testNoteOnOff();
  testTransposition();
  testVelocity();
  testSustainPedal();
  testUndampedKeys();
  testRestrike();
  testPolyphonyLimit();
  testReleasedNotesAreStolenFirst();
  testStealingDoesNotClick();
  testChannels();
  testVolumeAndPitchBend();
  testControllerMapping();
  testInt16Output();
  testEventQueueOverflow();
  testDrumsExactKeysAndNoteOff();
  testChokeGroup();
  testInstrumentPolyphony();
  testInstrumentBank();
  testProgramChange();
  testChannelTranspose();
  return testResult("test_synthesizer");
}
