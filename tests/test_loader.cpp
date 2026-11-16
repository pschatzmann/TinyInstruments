#include <string>

#include "TinyInstruments.h"
#include "TestHelper.h"
#include "samples/DrumKit.h"
#include "samples/GrandPiano.h"
#include "samples/TenorSax.h"
#include "samples/UprightPianoKW.h"

// Loading sample sets at runtime from the binary files in the data folder

#ifndef TI_DATA_DIR
#define TI_DATA_DIR "data"
#endif

/// Stream on a memory buffer which delivers the data in small pieces
/// (like a network stream)
struct MemoryStream {
  std::vector<uint8_t> data;
  size_t pos = 0;
  size_t chunk = 1000;
  size_t readBytes(char* out, size_t len) {
    size_t n = std::min(std::min(len, chunk), data.size() - pos);
    memcpy(out, data.data() + pos, n);
    pos += n;
    return n;
  }
};

static std::vector<uint8_t> readFile(const char* path) {
  std::vector<uint8_t> result;
  FILE* f = fopen(path, "rb");
  if (!f) return result;
  uint8_t buffer[4096];
  size_t n;
  while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) result.insert(result.end(), buffer, buffer + n);
  fclose(f);
  return result;
}

static bool sameZones(const SampleSet& a, const SampleSet& b) {
  if (a.zoneCount != b.zoneCount || a.sampleRate != b.sampleRate || a.dataSize != b.dataSize)
    return false;
  if (strcmp(a.name, b.name) != 0) return false;
  for (int j = 0; j < a.zoneCount; j++) {
    const SampleZone& x = a.zones[j];
    const SampleZone& y = b.zones[j];
    if (x.length != y.length || x.loopStart != y.loopStart || x.loopEnd != y.loopEnd ||
        x.loopPredictor != y.loopPredictor || x.loopIndex != y.loopIndex ||
        x.decayStart != y.decayStart || x.decayLn != y.decayLn || x.rootKey != y.rootKey ||
        x.loKey != y.loKey || x.hiKey != y.hiKey || x.loVel != y.loVel || x.hiVel != y.hiVel ||
        x.group != y.group || x.offBy != y.offBy || x.tune != y.tune)
      return false;
    if (memcmp(x.data, y.data, (x.length + 1) / 2) != 0) return false;
  }
  return true;
}

static void testFilesMatchHeaders() {
  struct { const char* file; const SampleSet& set; } sets[] = {
      {TI_DATA_DIR "/UprightPianoKW.bin", UprightPianoKW()},
      {TI_DATA_DIR "/GrandPiano.bin", GrandPiano()},
      {TI_DATA_DIR "/DrumKit.bin", DrumKit()},
      {TI_DATA_DIR "/TenorSax.bin", TenorSax()},
  };
  for (auto& s : sets) {
    StdioFileStream file(s.file);
    CHECK(file);
    DynamicSampleSet loaded;
    bool ok = loaded.begin(file);
    if (!ok) printf("  %s: %s\n", s.file, loaded.errorText());
    CHECK(ok);
    CHECK(sameZones(loaded.sampleSet(), s.set));
  }
}

static void testChunkedStream() {
  // the stream delivers the data in small pieces
  MemoryStream in;
  in.data = readFile(TI_DATA_DIR "/TenorSax.bin");
  in.chunk = 333;
  DynamicSampleSet loaded;
  CHECK(loaded.begin(in));
  CHECK(sameZones(loaded.sampleSet(), TenorSax()));
}

static void testPlayLoaded() {
  StdioFileStream file(TI_DATA_DIR "/GrandPiano.bin");
  DynamicSampleSet grand;
  CHECK(grand.begin(file));
  Instrument piano = PianoInstrument(grand.sampleSet());
  SampleSynthesizer synth;
  synth.setInstrument(-1, piano);
  synth.begin(44100);
  synth.noteOn(0, 69, 100);
  std::vector<float> left(8192), right(8192);
  synth.renderFloat(left.data(), right.data(), 2205);
  synth.renderFloat(left.data(), right.data(), 8192);
  CHECK_NEAR(estimateFrequency(left, 44100, 400, 480), 440.0, 3.0);
}

static void testErrors() {
  MemoryStream in;
  in.data = readFile(TI_DATA_DIR "/DrumKit.bin");
  DynamicSampleSet loaded;

  MemoryStream truncated = in;
  truncated.data.resize(in.data.size() - 100);
  CHECK(!loaded.begin(truncated));
  CHECK(loaded.error() == DynamicSampleSet::ReadError);
  CHECK(!loaded.isLoaded());

  MemoryStream bad_magic = in;
  bad_magic.data[0] = 'X';
  CHECK(!loaded.begin(bad_magic));
  CHECK(loaded.error() == DynamicSampleSet::InvalidFormat);

  MemoryStream bad_version = in;
  bad_version.data[4] = 9;
  CHECK(!loaded.begin(bad_version));
  CHECK(loaded.error() == DynamicSampleSet::UnsupportedVersion);

  MemoryStream empty;
  CHECK(!loaded.begin(empty));

  // a valid file can be loaded afterwards
  CHECK(loaded.begin(in));
  CHECK(loaded.isLoaded());
  loaded.end();
  CHECK(!loaded.isLoaded());
}

static void testAllDataFiles() {
  // all versions of all instruments can be loaded and played
  const char* names[] = {"UprightPianoKW", "GrandPiano", "DrumKit", "TenorSax", "Clarinet", "ElectricGuitar",
                         "ElectricBass"};
  int count = 0;
  for (const char* name : names) {
    for (const char* version : {"", "Small", "Octave"}) {
      std::string path = std::string(TI_DATA_DIR) + "/" + name + version + ".bin";
      FILE* f = fopen(path.c_str(), "rb");
      if (f == nullptr) continue;  // the drum kit has no octave version
      fclose(f);
      StdioFileStream file(path.c_str());
      DynamicSampleSet set;
      bool ok = set.begin(file);
      if (!ok) printf("  %s: %s\n", path.c_str(), set.errorText());
      CHECK(ok);
      if (!ok) continue;
      bool drums = std::string(name) == "DrumKit";
      Instrument inst = drums ? DrumInstrument(set.sampleSet()) : PianoInstrument(set.sampleSet());
      SampleSynthesizer synth;
      synth.setInstrument(-1, inst);
      synth.begin(44100);
      synth.noteOn(drums ? 9 : 0, drums ? 38 : 60, 110);
      std::vector<float> l(4410), r(4410);
      synth.renderFloat(l.data(), r.data(), 4410);
      CHECK(rms(l) > 0.001);
      count++;
    }
  }
  printf("  %d data files loaded and played\n", count);
  CHECK(count == 20);
}

int main() {
  testFilesMatchHeaders();
  testAllDataFiles();
  testChunkedStream();
  testPlayLoaded();
  testErrors();
  return testResult("test_loader");
}
