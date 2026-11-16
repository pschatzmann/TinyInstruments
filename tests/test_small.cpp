#include "TinyInstruments.h"
#include "TestHelper.h"
#include "samples/Clarinet.h"
#include "samples/ClarinetSmall.h"
#include "samples/DrumKit.h"
#include "samples/DrumKitSmall.h"
#include "samples/ElectricBass.h"
#include "samples/ElectricBassSmall.h"
#include "samples/ElectricGuitar.h"
#include "samples/ElectricGuitarSmall.h"
#include "samples/GrandPiano.h"
#include "samples/GrandPianoSmall.h"
#include "samples/TenorSax.h"
#include "samples/TenorSaxSmall.h"
#include "samples/UprightPianoKW.h"
#include "samples/UprightPianoKWSmall.h"

// The small versions of the sample sets (~250 KB): one velocity layer, 22 kHz

#ifndef TI_DATA_DIR
#define TI_DATA_DIR "data"
#endif

struct SmallSet {
  const char* file;
  const SampleSet& small;
  const SampleSet& full;
  Instrument (*preset)(const SampleSet&);
  int lo, hi;      // key range which is checked
  int test_note;   // note for the level and pitch comparison
};

static std::vector<float> renderNote(const Instrument& inst, int channel, int note, int velocity, size_t frames,
                                     size_t skip = 4410) {
  SampleSynthesizer synth;
  synth.setInstrument(channel, inst);
  synth.begin(44100);
  synth.noteOn(channel, note, velocity);
  std::vector<float> l(frames), r(frames);
  synth.renderFloat(l.data(), r.data(), skip);
  synth.renderFloat(l.data(), r.data(), frames);
  return l;
}

static uint32_t markBytes(const SampleSet& set) {
  uint32_t n = 0;
  for (int j = 0; j < set.zoneCount; j++) n += set.zones[j].markCount * 2;
  return n;
}

int main() {
  SmallSet sets[] = {
      {TI_DATA_DIR "/UprightPianoKWSmall.bin", UprightPianoKWSmall(), UprightPianoKW(), PianoInstrument, 21, 108, 60},
      {TI_DATA_DIR "/GrandPianoSmall.bin", GrandPianoSmall(), GrandPiano(), PianoInstrument, 21, 108, 60},
      {TI_DATA_DIR "/TenorSaxSmall.bin", TenorSaxSmall(), TenorSax(), SaxophoneInstrument, 44, 88, 61},
      {TI_DATA_DIR "/ClarinetSmall.bin", ClarinetSmall(), Clarinet(), ClarinetInstrument, 50, 90, 63},
      {TI_DATA_DIR "/ElectricGuitarSmall.bin", ElectricGuitarSmall(), ElectricGuitar(), GuitarInstrument, 40, 88, 57},
      {TI_DATA_DIR "/ElectricBassSmall.bin", ElectricBassSmall(), ElectricBass(), BassInstrument, 28, 67, 45},
  };
  for (auto& s : sets) {
    uint32_t total = s.small.dataSize + markBytes(s.small);
    printf("%s: %u bytes (full: %u), %d zones, %u Hz\n", s.small.name, (unsigned)total,
           (unsigned)(s.full.dataSize + markBytes(s.full)), s.small.zoneCount, (unsigned)s.small.sampleRate);
    CHECK(total <= 250000);

    // the binary file is identical
    StdioFileStream file(s.file);
    DynamicSampleSet loaded;
    CHECK(loaded.begin(file));
    CHECK(loaded.sampleSet().zoneCount == s.small.zoneCount && loaded.sampleSet().dataSize == s.small.dataSize);

    // all keys sound like in the full version (level of the first 0.2 s)
    Instrument inst = s.preset(s.small);
    Instrument full = s.preset(s.full);
    bool ok = true;
    double max_diff = 0;
    for (int note = s.lo; note <= s.hi; note++) {
      for (int vel : {30, 120}) {
        double small_level = rms(renderNote(inst, 0, note, vel, 8820, 0));
        double full_level = rms(renderNote(full, 0, note, vel, 8820, 0));
        double diff = 20 * log10((small_level + 1e-9) / (full_level + 1e-9));
        if (vel >= 100) max_diff = std::max(max_diff, fabs(diff));
        // loud notes: like the full version. Soft notes are created from the
        // loud recording with the velocity lowpass: darker and up to ~8 dB
        // quieter than the soft recordings of the full version
        bool bad = vel >= 100 ? fabs(diff) > 6.0 : diff < -12.0;
        if (!std::isfinite(small_level) || small_level < 1e-5 || bad) {
          printf("  note %d vel %d: %+.1f dB compared with the full version\n", note, vel, diff);
          ok = false;
        }
      }
    }
    printf("  all keys: max level difference (velocity 120) %.1f dB\n", max_diff);
    CHECK(ok);

    // pitch and level of a sustained part compared with the full version
    double freq = 440.0 * pow(2.0, (s.test_note - 69) / 12.0);
    auto a = renderNote(inst, 0, s.test_note, 100, 16384, 13230);
    auto b = renderNote(full, 0, s.test_note, 100, 16384, 13230);
    double cents = 1200 * log2(estimateFrequency(a, 44100, freq * 0.8, freq * 1.25) / freq);
    double db = 20 * log10(rms(a) / rms(b));
    printf("  note %d: %+.1f cents, level %+.1f dB compared with the full version\n", s.test_note, cents, db);
    CHECK_NEAR(cents, 0.0, 20.0);
    CHECK_NEAR(db, 0.0, 4.0);
  }

  // drums: the General MIDI keys
  printf("%s: %u bytes, %d zones\n", DrumKitSmall().name, (unsigned)DrumKitSmall().dataSize, DrumKitSmall().zoneCount);
  CHECK(DrumKitSmall().dataSize <= 250000);
  Instrument drums = DrumInstrument(DrumKitSmall());
  bool ok = true;
  for (int note : {35, 36, 38, 40, 42, 44, 46, 49, 51, 57}) {
    if (rms(renderNote(drums, 9, note, 110, 4410, 0)) < 0.0001) {
      printf("  drum %d silent\n", note);
      ok = false;
    }
  }
  CHECK(ok);
  return testResult("test_small");
}
