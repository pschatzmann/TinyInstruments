// Renders the example recordings of the README: one wav file per instrument
// and version (full, small, octave), loaded from the data folder.
//   render_examples <output directory>
#include <stdio.h>

#include <functional>
#include <string>
#include <vector>

#define TI_MAX_VOICES 16
#include "TinyInstruments.h"

#ifndef TI_DATA_DIR
#define TI_DATA_DIR "data"
#endif

static const int RATE = 44100;
static std::vector<int16_t> out;
static SampleSynthesizer synth;

static void play(float seconds) {
  size_t frames = seconds * RATE, pos = out.size();
  out.resize(pos + frames * 2);
  synth.render(&out[pos], frames);
}

static void writeWav(const std::string& path) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  uint32_t bytes = out.size() * 2;
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f);
  u32(36 + bytes);
  fwrite("WAVEfmt ", 1, 8, f);
  u32(16);
  u16(1);
  u16(2);
  u32(RATE);
  u32(RATE * 4);
  u16(4);
  u16(16);
  fwrite("data", 1, 4, f);
  u32(bytes);
  fwrite(out.data(), 2, out.size(), f);
  fclose(f);
}

// --- phrases: the same for all versions of an instrument ---

static void pianoPhrase(int ch) {
  int chords[][4] = {{48, 55, 60, 64}, {45, 52, 57, 60}, {41, 48, 53, 57}, {43, 50, 55, 59}};
  for (auto& c : chords) {
    synth.controlChange(ch, 64, 127);
    for (int n : c) synth.noteOn(ch, n, 95);
    play(0.6f);
    for (int n : c) synth.noteOff(ch, n);
    synth.controlChange(ch, 64, 0);
    play(0.1f);
  }
  for (int n = 60; n <= 84; n += 2) {
    synth.noteOn(ch, n, 80);
    play(0.12f);
    synth.noteOff(ch, n);
  }
  synth.noteOn(ch, 36, 110);
  synth.noteOn(ch, 72, 110);
  play(1.5f);
  synth.noteOff(ch, 36);
  synth.noteOff(ch, 72);
}

static void drumPhrase(int ch) {
  for (int bar = 0; bar < 4; bar++) {
    for (int step = 0; step < 8; step++) {
      if (step == 0 && bar % 2 == 0) synth.noteOn(ch, 49, 115);
      synth.noteOn(ch, step == 7 && bar == 3 ? 46 : 42, step % 2 ? 70 : 100);
      if (step == 0 || step == 5) synth.noteOn(ch, 36, 120);
      if (step == 2 || step == 6) synth.noteOn(ch, 38, 105);
      play(0.15f);
    }
  }
  for (int n : {48, 47, 45, 43, 41}) {
    synth.noteOn(ch, n, 110);
    play(0.12f);
  }
  synth.noteOn(ch, 49, 120);
  synth.noteOn(ch, 36, 120);
}

static void windPhrase(int ch, int base) {
  int m[] = {0, 2, 4, 5, 7, 9, 11, 12, 11, 9, 7, 5, 4, 2, 0};
  int count = sizeof(m) / sizeof(int);
  for (int j = 0; j < count; j++) {
    synth.noteOn(ch, base + m[j], 95);  // overlapping notes: legato
    if (j > 0) synth.noteOff(ch, base + m[j - 1]);
    play(j + 1 == count ? 1.5f : 0.25f);
  }
  synth.noteOff(ch, base);
}

static void epianoPhrase(int ch) {
  // ii - V - I in C with jazz voicings, then a short melody
  int chords[][4] = {{50, 53, 57, 60}, {55, 59, 62, 65}, {48, 52, 55, 59}, {48, 52, 55, 59}};
  for (auto& c : chords) {
    for (int n : c) synth.noteOn(ch, n, 85);
    play(0.7f);
    for (int n : c) synth.noteOff(ch, n);
    play(0.05f);
  }
  int melody[] = {72, 74, 76, 79, 77, 76, 74, 72};
  for (int n : melody) {
    synth.noteOn(ch, n, 105);
    play(0.18f);
    synth.noteOff(ch, n);
  }
  for (int n : {36, 48, 52, 55, 59, 64}) synth.noteOn(ch, n, 110);
  play(1.5f);
  for (int n : {36, 48, 52, 55, 59, 64}) synth.noteOff(ch, n);
}

static void walkingBassPhrase(int ch) {
  // walking bass: ii - V - I - VI in C
  int line[] = {38, 41, 45, 48, 43, 47, 50, 49, 36, 40, 43, 45, 45, 44, 43, 41};
  for (int n : line) {
    synth.noteOn(ch, n, 100);
    play(0.3f);
    synth.noteOff(ch, n);
    play(0.02f);
  }
  synth.noteOn(ch, 36, 115);
  play(1.2f);
  synth.noteOff(ch, 36);
}

static void guitarPhrase(int ch) {
  int a[] = {40, 47, 52, 56, 59, 64, 45, 52, 57, 61, 64, 69};
  for (int j = 0; j < 12; j++) {
    synth.noteOn(ch, a[j], 90);
    play(0.2f);
  }
  play(1.0f);
  for (int n : a) synth.noteOff(ch, n);
}

static void bassPhrase(int ch) {
  int l[] = {28, 28, 40, 28, 31, 33, 35, 38, 33, 33, 45, 33, 36, 38, 40, 43};
  for (int n : l) {
    synth.noteOn(ch, n, 100);
    play(0.18f);
    synth.noteOff(ch, n);
    play(0.04f);
  }
  synth.noteOn(ch, 28, 110);
  play(1.2f);
  synth.noteOff(ch, 28);
}

int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : ".";
  GuitarAmp clean(GuitarAmp::Clean), bass_amp(GuitarAmp::Bass);
  TremoloEffect tremolo(5.5f, 0.3f);
  struct Row {
    const char* name;
    Instrument (*preset)(const SampleSet&);
    int channel;
    ChannelEffect* effect;
    std::function<void(int)> phrase;
  };
  Row rows[] = {
      {"UprightPianoKW", PianoInstrument, 0, nullptr, pianoPhrase},
      {"GrandPiano", PianoInstrument, 0, nullptr, pianoPhrase},
      {"DrumKit", DrumInstrument, 9, nullptr, drumPhrase},
      {"TenorSax", SaxophoneInstrument, 0, nullptr, [](int ch) { windPhrase(ch, 56); }},
      {"Clarinet", ClarinetInstrument, 0, nullptr, [](int ch) { windPhrase(ch, 62); }},
      {"ElectricGuitar", GuitarInstrument, 0, &clean, guitarPhrase},
      {"ElectricBass", BassInstrument, 0, &bass_amp, bassPhrase},
      {"Wurlitzer", ElectricPianoInstrument, 0, &tremolo, epianoPhrase},
      {"UprightBass", UprightBassInstrument, 0, nullptr, walkingBassPhrase},
  };
  int count = 0;
  for (auto& row : rows) {
    for (const char* version : {"", "Small", "Octave"}) {
      std::string file = std::string(TI_DATA_DIR) + "/" + row.name + version + ".bin";
      StdioFileStream in(file.c_str());
      if (!in) continue;  // e.g. no octave version of the drum kit
      DynamicSampleSet set;
      if (!set.begin(in)) {
        printf("could not load %s: %s\n", file.c_str(), set.errorText());
        return 1;
      }
      Instrument inst = row.preset(set.sampleSet());
      synth.begin(RATE);
      synth.setInstrument(-1, inst);
      synth.setEffect(-1, row.effect);
      out.clear();
      play(0.1f);
      row.phrase(row.channel);
      play(1.0f);  // decay
      std::string path = dir + "/" + row.name + version + ".wav";
      writeWav(path);
      printf("written %s (%.1f s)\n", path.c_str(), out.size() / 2.0 / RATE);
      count++;
    }
  }
  // the synthesized tonewheel organ with rotary speaker: blues in C, the
  // rotary speaker changes from slow to fast in the middle
  {
    TonewheelOrgan organ("888000000");
    RotarySpeaker leslie(0.3f);
    Instrument inst = OrganInstrument(organ);
    synth.begin(RATE);
    synth.setInstrument(-1, inst);
    synth.setEffect(-1, &leslie);
    out.clear();
    play(0.1f);
    int chords[][4] = {{48, 52, 55, 58}, {53, 57, 60, 63}, {48, 52, 55, 58}, {55, 59, 62, 65}};
    for (int bar = 0; bar < 4; bar++) {
      if (bar == 2) synth.controlChange(0, 1, 127);  // fast
      for (int n : chords[bar]) synth.noteOn(0, n, 100);
      play(0.9f);
      for (int n : chords[bar]) synth.noteOff(0, n);
      play(0.1f);
    }
    // a lick with percussion
    organ.setPercussion(true, true, true, false);
    int lick[] = {72, 75, 76, 79, 82, 79, 76, 72};
    for (int n : lick) {
      synth.noteOn(0, n, 100);
      play(0.16f);
      synth.noteOff(0, n);
      play(0.04f);
    }
    organ.setPercussion(false);
    synth.controlChange(0, 1, 0);  // slow
    for (int n : {36, 48, 52, 55, 58, 64}) synth.noteOn(0, n, 100);
    play(2.5f);
    for (int n : {36, 48, 52, 55, 58, 64}) synth.noteOff(0, n);
    play(0.5f);
    std::string path = dir + "/TonewheelOrgan.wav";
    writeWav(path);
    printf("written %s (%.1f s)\n", path.c_str(), out.size() / 2.0 / RATE);
    count++;
  }
  return count > 0 ? 0 : 1;
}
