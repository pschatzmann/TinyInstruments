#include <string.h>

#include "TinyInstruments.h"
#include "TestHelper.h"
#include "samples/UprightBass.h"
#include "samples/UprightBassOctave.h"
#include "samples/UprightBassSmall.h"
#include "samples/Wurlitzer.h"
#include "samples/WurlitzerOctave.h"
#include "samples/WurlitzerSmall.h"

// Electric piano (Wurlitzer EP200), upright bass (pizzicato) and the tremolo

#ifndef TI_DATA_DIR
#define TI_DATA_DIR "data"
#endif

static const int RATE = 44100;

static std::vector<float> renderNote(const Instrument& inst, int note, int velocity, size_t frames,
                                     size_t skip = 0, ChannelEffect* effect = nullptr) {
  SampleSynthesizer synth;
  synth.setInstrument(0, inst);
  synth.setEffect(0, effect);
  synth.begin(RATE);
  synth.noteOn(0, note, velocity);
  if (skip > 0) {
    std::vector<float> sl(skip), sr(skip);
    synth.renderFloat(sl.data(), sr.data(), skip);
  }
  std::vector<float> l(frames), r(frames);
  synth.renderFloat(l.data(), r.data(), frames);
  return l;
}

/// The compiled set is identical with the binary file
static bool sameAsFile(const SampleSet& a, const SampleSet& b) {
  if (strcmp(a.name, b.name) != 0 || a.zoneCount != b.zoneCount || a.dataSize != b.dataSize) return false;
  for (int j = 0; j < a.zoneCount; j++) {
    const SampleZone& x = a.zones[j];
    const SampleZone& y = b.zones[j];
    if (x.length != y.length || x.rootKey != y.rootKey || x.loKey != y.loKey || x.hiKey != y.hiKey ||
        x.markCount != y.markCount || memcmp(x.data, y.data, (x.length + 1) / 2) != 0)
      return false;
    if (x.markCount > 0 && memcmp(x.marks, y.marks, x.markCount * 2) != 0) return false;
  }
  return true;
}

struct Set {
  const char* file;
  const SampleSet& set;
  uint32_t budget;
};

int main() {
  // sizes and data files
  Set sets[] = {
      {TI_DATA_DIR "/Wurlitzer.bin", Wurlitzer(), 700000},
      {TI_DATA_DIR "/WurlitzerSmall.bin", WurlitzerSmall(), 250000},
      {TI_DATA_DIR "/WurlitzerOctave.bin", WurlitzerOctave(), 150000},
      {TI_DATA_DIR "/UprightBass.bin", UprightBass(), 700000},
      {TI_DATA_DIR "/UprightBassSmall.bin", UprightBassSmall(), 250000},
      {TI_DATA_DIR "/UprightBassOctave.bin", UprightBassOctave(), 150000},
  };
  for (auto& s : sets) {
    uint32_t marks = 0;
    for (int j = 0; j < s.set.zoneCount; j++) marks += s.set.zones[j].markCount * 2;
    printf("%s: %u bytes, %d zones\n", s.set.name, (unsigned)(s.set.dataSize + marks), s.set.zoneCount);
    CHECK(s.set.dataSize + marks <= s.budget);
    StdioFileStream file(s.file);
    DynamicSampleSet loaded;
    CHECK(loaded.begin(file));
    CHECK(sameAsFile(s.set, loaded.sampleSet()));
  }

  struct Inst {
    const char* name;
    Instrument inst;
    int lo, hi, test_note;
  } instruments[] = {
      {"Wurlitzer", ElectricPianoInstrument(Wurlitzer()), 33, 96, 60},
      {"WurlitzerSmall", ElectricPianoInstrument(WurlitzerSmall()), 33, 96, 60},
      {"WurlitzerOctave", ElectricPianoInstrument(WurlitzerOctave()), 33, 96, 60},
      {"UprightBass", UprightBassInstrument(UprightBass()), 28, 60, 40},
      {"UprightBassSmall", UprightBassInstrument(UprightBassSmall()), 28, 60, 40},
      {"UprightBassOctave", UprightBassInstrument(UprightBassOctave()), 28, 60, 40},
  };
  for (auto& i : instruments) {
    // all keys sound
    bool ok = true;
    for (int note = i.lo; note <= i.hi; note++) {
      double level = rms(renderNote(i.inst, note, 100, 4410));
      if (!std::isfinite(level) || level < 1e-4) {
        printf("  %s: note %d silent\n", i.name, note);
        ok = false;
      }
    }
    CHECK(ok);
    // pitch
    double freq = 440.0 * pow(2.0, (i.test_note - 69) / 12.0);
    auto x = renderNote(i.inst, i.test_note, 100, 16384, 4410);
    double cents = 1200 * log2(estimateFrequency(x, RATE, freq * 0.8, freq * 1.25) / freq);
    // louder with the velocity
    double soft = rms(renderNote(i.inst, i.test_note, 40, 8820));
    double loud = rms(renderNote(i.inst, i.test_note, 120, 8820));
    printf("%s: note %d %+.1f cents, velocity 40 -> 120: %+.1f dB\n", i.name, i.test_note, cents,
           20 * log10(loud / soft));
    CHECK_NEAR(cents, 0.0, 25.0);
    CHECK(loud > soft * 2);
  }

  // release: the dampers stop the note quickly
  {
    Instrument ep = ElectricPianoInstrument(Wurlitzer());
    SampleSynthesizer synth;
    synth.setInstrument(0, ep);
    synth.begin(RATE);
    synth.noteOn(0, 60, 100);
    std::vector<float> l(RATE / 2), r(RATE / 2);
    synth.renderFloat(l.data(), r.data(), l.size());
    double held = rms(l, l.size() - 4410);
    synth.noteOff(0, 60);
    synth.renderFloat(l.data(), r.data(), l.size());
    double after = rms(l, l.size() - 4410);
    printf("electric piano release: %.1f dB after 0.5 s\n", 20 * log10(after / held + 1e-12));
    CHECK(after < held * 0.01);
  }

  // upright bass: 4 strings
  {
    Instrument bass = UprightBassInstrument(UprightBass());
    SampleSynthesizer synth;
    synth.setInstrument(0, bass);
    synth.begin(RATE);
    for (int n : {28, 33, 38, 43, 47}) synth.noteOn(0, n, 100);
    std::vector<float> l(4410), r(4410);
    synth.renderFloat(l.data(), r.data(), l.size());
    printf("upright bass: %d notes of 5\n", synth.activeNotes());
    CHECK(synth.activeNotes() == 4);
  }

  // tremolo: amplitude modulation at the rate, stereo (auto pan) in opposite phase
  {
    Instrument ep = ElectricPianoInstrument(Wurlitzer());
    TremoloEffect tremolo(5.0f, 0.8f, true);
    SampleSynthesizer synth;
    synth.setInstrument(0, ep);
    synth.setEffect(0, &tremolo);
    synth.begin(RATE);
    synth.noteOn(0, 60, 100);
    size_t n = RATE;
    std::vector<float> l(n), r(n);
    synth.renderFloat(l.data(), r.data(), n);
    // envelope in 10 ms frames: left and right are anti correlated
    std::vector<double> el, er;
    for (size_t i = 0; i + 441 <= n; i += 441) {
      el.push_back(rms(l, i, i + 441));
      er.push_back(rms(r, i, i + 441));
    }
    // remove the decay: ratio left / right oscillates with the tremolo
    int crossings = 0;
    double lo = 1e9, hi = 0;
    for (size_t i = 0; i < el.size(); i++) {
      double ratio = 20 * log10(el[i] / er[i]);
      lo = std::min(lo, ratio);
      hi = std::max(hi, ratio);
      if (i > 0 && (20 * log10(el[i - 1] / er[i - 1])) * ratio < 0) crossings++;
    }
    printf("tremolo (auto pan): %.1f Hz, left/right %.1f .. %+.1f dB\n", crossings / 2.0, lo, hi);
    CHECK_NEAR(crossings / 2.0, 5.0, 1.0);
    CHECK(hi - lo > 10.0);
    tremolo.controlChange(1, 0);  // depth 0: no modulation
    CHECK(tremolo.getDepth() == 0.0f);
  }
  return testResult("test_jazz");
}
