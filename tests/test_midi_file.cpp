// Tests of the midi file loading of the desktop player (MidiFileSong.h): the
// tracks of a format 1 file are merged, the tempo map applies to all tracks,
// and the General MIDI bank selects the instruments
#include <vector>

#include "../desktop/MidiFileSong.h"
#include "TinyInstruments.h"
#include "TestHelper.h"
#include "examples/example-midi.h"  // arduino-midi-fileparser

using namespace tinyinstruments;

typedef std::vector<uint8_t> Bytes;

static void append(Bytes& out, std::initializer_list<uint8_t> bytes) { out.insert(out.end(), bytes); }

static void appendBE32(Bytes& out, uint32_t v) {
  append(out, {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v});
}

static void appendTrack(Bytes& out, const Bytes& events) {
  append(out, {'M', 'T', 'r', 'k'});
  appendBE32(out, events.size());
  out.insert(out.end(), events.begin(), events.end());
}

/// Midi file with the indicated format, 480 ticks per quarter note
static Bytes header(uint16_t format, uint16_t tracks) {
  Bytes out;
  append(out, {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, (uint8_t)format, 0, (uint8_t)tracks, 0x01, 0xE0});
  return out;
}

/// Format 1: the tempo map in track 1, the notes in tracks 2 and 3
static void testMultiTrack() {
  Bytes file = header(1, 3);
  // track 1: 60 BPM (1000000 us), after 1 quarter note (480 ticks) 120 BPM
  appendTrack(file, {0x00, 0xFF, 0x51, 0x03, 0x0F, 0x42, 0x40,                    // tempo 1000000
                     0x83, 0x60, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,              // +480: 500000
                     0x00, 0xFF, 0x2F, 0x00});
  // track 2: note 60 at 0, off after 2 quarter notes (960 ticks)
  appendTrack(file, {0x00, 0x90, 60, 100, 0x87, 0x40, 0x80, 60, 0, 0x00, 0xFF, 0x2F, 0x00});
  // track 3: program change and note 64 after 1 quarter note (480 ticks)
  appendTrack(file, {0x00, 0xC1, 33, 0x83, 0x60, 0x91, 64, 90, 0x00, 0xFF, 0x2F, 0x00});

  MidiFileSong song;
  CHECK(song.load(file.data(), file.size()));
  CHECK(song.format == 1 && song.track_count == 3);
  CHECK(song.events.size() == 4);
  if (song.events.size() != 4) return;
  // the tracks are merged by time
  CHECK(song.events[0].data[0] == 0x90 && song.events[0].len == 3);
  CHECK(song.events[1].data[0] == 0xC1 && song.events[1].data[1] == 33 && song.events[1].len == 2);
  CHECK(song.events[2].data[0] == 0x91 && song.events[2].data[1] == 64);
  CHECK(song.events[3].data[0] == 0x80);
  // 1 quarter at 60 BPM = 1000 ms, the 2nd quarter at 120 BPM = 500 ms
  CHECK_NEAR(song.events[0].time_ms, 0.0, 0.01);
  CHECK_NEAR(song.events[2].time_ms, 1000.0, 0.01);
  CHECK_NEAR(song.events[3].time_ms, 1500.0, 0.01);
  CHECK(song.noteCount() == 2);
}

/// Without a Set Tempo event: 120 BPM
static void testDefaultTempo() {
  Bytes file = header(0, 1);
  appendTrack(file, {0x00, 0x90, 60, 100, 0x83, 0x60, 0x80, 60, 0, 0x00, 0xFF, 0x2F, 0x00});
  MidiFileSong song;
  CHECK(song.load(file.data(), file.size()));
  CHECK(song.events.size() == 2);
  if (song.events.size() == 2) CHECK_NEAR(song.events[1].time_ms, 500.0, 0.01);
}

static void testInvalid() {
  MidiFileSong song;
  uint8_t garbage[] = {1, 2, 3, 4, 5, 6, 7, 8};
  CHECK(!song.load(garbage, sizeof(garbage)));
  CHECK(!song.error.empty());
  CHECK(!song.load("/nonexistent/song.mid"));
}

/// The example song of arduino-midi-fileparser (format 0, 4781 notes, 3:55)
static void testExampleSong() {
  MidiFileSong song;
  CHECK(song.load(example_mid, example_mid_len));
  CHECK(song.noteCount() == 4781);
  CHECK_NEAR(song.durationMs() / 1000.0, 235.0, 2.0);
}

/// The General MIDI bank selects the instruments with the program changes
static void testGeneralMidiBank() {
  GeneralMidiBank gm;
  GeneralMidiBank::SampleSets sets;  // no sample sets: replacements
  SampleSynthesizer synth;
  gm.begin(synth, sets);
  const InstrumentBank& bank = gm.bank();
  CHECK(bank.find(0, 0) != nullptr);
  CHECK(bank.find(0, 20)->effect != nullptr);   // organ with rotary speaker
  CHECK(bank.find(0, 72)->instrument->transpose == -12);  // piccolo
  CHECK(bank.find(0, 78)->instrument->transpose == -24);  // whistle
  CHECK(bank.find(9, 5) == bank.find(9, 100));             // drum channel: drum kit
}

int main() {
  testMultiTrack();
  testDefaultTempo();
  testInvalid();
  testExampleSong();
  testGeneralMidiBank();
  return testResult("test_midi_file");
}
