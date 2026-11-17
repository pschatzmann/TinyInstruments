#include <string.h>

#include "TinyInstruments.h"
#include "TestHelper.h"
#include "samples/ClarinetOctave.h"
#include "samples/ElectricBassOctave.h"
#include "samples/ElectricGuitarOctave.h"
#include "samples/GrandPianoOctave.h"
#include "samples/TenorSaxOctave.h"
#include "samples/UprightPianoKWOctave.h"

// The octave versions of the sample sets (~150 KB): one recording per octave,
// compiled in (src/samples/*Octave.h, generated with tools/bin2header.py from
// the data files)

#ifndef TI_DATA_DIR
#define TI_DATA_DIR "data"
#endif

struct OctaveSet {
  const char* file;
  const SampleSet& set;
  Instrument (*preset)(const SampleSet&);
  int lo, hi;     // key range which is checked
  int test_note;  // note for the pitch check
};

static std::vector<float> renderNote(const Instrument& inst, int note, int velocity, size_t frames,
                                     size_t skip = 0) {
  SampleSynthesizer synth;
  synth.setInstrument(0, inst);
  synth.begin(44100);
  synth.noteOn(0, note, velocity);
  std::vector<float> l(frames), r(frames);
  if (skip > 0) {
    std::vector<float> sl(skip), sr(skip);
    synth.renderFloat(sl.data(), sr.data(), skip);
  }
  synth.renderFloat(l.data(), r.data(), frames);
  return l;
}

/// The compiled set is identical with the binary file
static bool sameAsFile(const SampleSet& a, const SampleSet& b) {
  if (strcmp(a.name, b.name) != 0 || a.zoneCount != b.zoneCount || a.sampleRate != b.sampleRate ||
      a.dataSize != b.dataSize)
    return false;
  for (int j = 0; j < a.zoneCount; j++) {
    const SampleZone& x = a.zones[j];
    const SampleZone& y = b.zones[j];
    if (x.length != y.length || x.loopStart != y.loopStart || x.loopEnd != y.loopEnd ||
        x.rootKey != y.rootKey || x.loKey != y.loKey || x.hiKey != y.hiKey || x.markCount != y.markCount)
      return false;
    if (memcmp(x.data, y.data, (x.length + 1) / 2) != 0) return false;
    if (x.markCount > 0 && memcmp(x.marks, y.marks, x.markCount * 2) != 0) return false;
  }
  return true;
}

int main() {
  OctaveSet sets[] = {
      {TI_DATA_DIR "/UprightPianoKWOctave.bin", UprightPianoKWOctave(), PianoInstrument, 21, 108, 60},
      {TI_DATA_DIR "/GrandPianoOctave.bin", GrandPianoOctave(), PianoInstrument, 21, 108, 60},
      {TI_DATA_DIR "/TenorSaxOctave.bin", TenorSaxOctave(), SaxophoneInstrument, 44, 88, 61},
      {TI_DATA_DIR "/ClarinetOctave.bin", ClarinetOctave(), ClarinetInstrument, 50, 90, 63},
      {TI_DATA_DIR "/ElectricGuitarOctave.bin", ElectricGuitarOctave(), GuitarInstrument, 40, 88, 57},
      {TI_DATA_DIR "/ElectricBassOctave.bin", ElectricBassOctave(), BassInstrument, 28, 67, 45},
  };
  for (auto& s : sets) {
    printf("%s: %u bytes, %d zones, %u Hz\n", s.set.name, (unsigned)s.set.dataSize, s.set.zoneCount,
           (unsigned)s.set.sampleRate);
    CHECK(s.set.dataSize <= 150000);

    // identical with the data file
    StdioFileStream file(s.file);
    DynamicSampleSet loaded;
    CHECK(loaded.begin(file));
    CHECK(sameAsFile(s.set, loaded.sampleSet()));

    // all keys sound
    Instrument inst = s.preset(s.set);
    bool ok = true;
    for (int note = s.lo; note <= s.hi; note++) {
      double level = rms(renderNote(inst, note, 110, 4410));
      if (!std::isfinite(level) || level < 1e-5) {
        printf("  note %d silent\n", note);
        ok = false;
      }
    }
    CHECK(ok);

    // pitch
    double freq = 440.0 * pow(2.0, (s.test_note - 69) / 12.0);
    auto a = renderNote(inst, s.test_note, 100, 16384, 13230);
    double cents = 1200 * log2(estimateFrequency(a, 44100, freq * 0.8, freq * 1.25) / freq);
    printf("  note %d: %+.1f cents\n", s.test_note, cents);
    CHECK_NEAR(cents, 0.0, 20.0);
  }
  return testResult("test_octave");
}
