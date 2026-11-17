/**
 * @file midi-file-player.ino
 * @brief Plays a midi file with the instruments of TinyInstruments: the file
 * is parsed incrementally with arduino-midi-fileparser, the General MIDI
 * program changes of the file select the instruments. The output goes to an
 * I2S DAC.
 *
 * The example uses the midi file which is included in arduino-midi-fileparser
 * (a song with piano, organ, guitar, bass and drums). To play your own file,
 * replace the MemoryStream with any other Stream, e.g. a File from LittleFS or
 * an SD card: the file is read in small pieces while it is played.
 *
 * | Program (General MIDI)          | Instrument                          |
 * |---------------------------------|-------------------------------------|
 * | 0-3, 6-15 pianos, mallets       | grand piano                         |
 * | 4, 5 electric pianos            | electric piano (Wurlitzer), tremolo |
 * | 16-23 organs                    | tonewheel organ, rotary speaker     |
 * | 24-31 guitars                   | electric guitar, clean amp          |
 * | 32 acoustic bass                | upright bass                        |
 * | 33-39 basses                    | electric bass, bass amp             |
 * | 40-55 strings, choirs           | tonewheel organ (sustained sound)   |
 * | 56-63 brass                     | tenor saxophone                     |
 * | 64-67 saxophones                | tenor saxophone                     |
 * | 68-79 reeds, pipes              | clarinet (piccolo: 1 octave lower,  |
 * |                                 | whistle: 2 octaves lower)           |
 * | 80-103 synthesizer leads, pads  | tonewheel organ                     |
 * | 104-111 ethnic instruments      | electric guitar, clean amp          |
 * | 112-127 sound effects           | silent                              |
 * | channel 10                      | drum kit                            |
 *
 * The mapping is defined by GeneralMidiBank: change it with
 * instruments.bank() after begin().
 *
 * The events are played in the order of the file, so only single track midi
 * files (format 0) are supported: in a multi track file (format 1) the tracks
 * would be played one after the other. Convert such a file to format 0 (e.g.
 * with a DAW or a midi editor) or play it with the desktop player
 * (tinyinstruments-player --midi-file), which merges the tracks.
 *
 * Requirements:
 * - Libraries: arduino-audio-tools, arduino-midi-fileparser
 *   (https://github.com/pschatzmann/arduino-midi-fileparser)
 * - The small versions of the sample sets (8 x 250 KB) are compiled in:
 *   select the partition scheme "Huge APP (3MB No OTA/1MB SPIFFS)"
 */
// several instruments play at the same time
#define TI_MAX_VOICES 16

#include "AudioTools.h"
#include "MidiFileParser.h"
#include "TinyInstrumentsAudioTools.h"
#include "examples/example-midi.h"  // the midi file of arduino-midi-fileparser
#include "samples/ClarinetSmall.h"
#include "samples/DrumKitSmall.h"
#include "samples/ElectricBassSmall.h"
#include "samples/ElectricGuitarSmall.h"
#include "samples/GrandPianoSmall.h"
#include "samples/TenorSaxSmall.h"
#include "samples/UprightBassSmall.h"
#include "samples/WurlitzerSmall.h"

// the instruments, selected with General MIDI program changes (see the
// table above): the small versions of the sample sets
GeneralMidiBank instruments;

SampleSynthesizer synth;
SampleSynthesizerStream in(synth);
I2SStream out;
StreamCopy copier(out, in);

// the midi file: any Stream, e.g. a File
MemoryStream midi_file(example_mid, example_mid_len);
midi::MidiFileParser parser;
uint8_t buffer[256];

// the next midi event: it is played when its time has come
bool event_pending = false;
uint32_t event_due_ms = 0;
midi::midi_midi_event event;
uint32_t start_ms = 0;
double song_ms = 0;  // exact: rounding the time of each event would add up
int note_count = 0;
double us_per_quarter = 500000;  // tempo: 120 BPM until a Set Tempo event

void setupInstruments() {
  GeneralMidiBank::SampleSets sets;
  sets.piano = &GrandPianoSmall();
  sets.electricPiano = &WurlitzerSmall();
  sets.guitar = &ElectricGuitarSmall();
  sets.uprightBass = &UprightBassSmall();
  sets.bass = &ElectricBassSmall();
  sets.saxophone = &TenorSaxSmall();
  sets.clarinet = &ClarinetSmall();
  sets.drums = &DrumKitSmall();
  instruments.begin(synth, sets);
}

/// Keeps the buffer of the parser filled from the midi file
void feedParser() {
  int space = parser.availableForWrite();
  if (space >= (int)sizeof(buffer) && midi_file.available() > 0) {
    size_t len = midi_file.readBytes(buffer, sizeof(buffer));
    parser.write(buffer, len);
  }
}

/// Sends a midi event of the file to the synthesizer
void play(const midi::midi_midi_event& e) {
  switch (e.status) {
    case midi::MIDI_STATUS_NOTE_ON:
      // velocity 0 = note off
      synth.noteOn(e.channel, e.param1, e.param2);
      if (e.param2 > 0) note_count++;
      break;
    case midi::MIDI_STATUS_NOTE_OFF:
      synth.noteOff(e.channel, e.param1);
      break;
    case midi::MIDI_STATUS_CC:
      synth.controlChange(e.channel, e.param1, e.param2);
      break;
    case midi::MIDI_STATUS_PGM_CHANGE:
      synth.programChange(e.channel, e.param1);
      break;
    case midi::MIDI_STATUS_PITCH_BEND:
      synth.pitchBend(e.channel, (e.param2 << 7) | e.param1);
      break;
    default:
      break;
  }
}

/// Plays the events of the midi file at their time
void processMidiFile() {
  feedParser();
  while (true) {
    if (!event_pending) {
      auto& state = parser.parse();
      if (state.status == midi::MIDI_PARSER_EOB || state.status == midi::MIDI_PARSER_ERROR) {
        if (midi_file.available() > 0) return;  // more data is needed
        Serial.printf("end of the midi file: %d notes, %u s\n", note_count, (unsigned)(song_ms / 1000));
        synth.allNotesOff();
        // play it again after a pause of 2 s (no delay(): the audio continues)
        midi_file.begin();
        parser.begin(false);
        song_ms = 0;
        note_count = 0;
        us_per_quarter = 500000;
        start_ms = millis() + 2000;
        return;
      }
      // the delta time of each event (also of the meta events) advances the
      // song; without a Set Tempo event: 120 BPM (500000 us per quarter note)
      double division = state.header.time_division > 0 ? state.header.time_division : 480;
      song_ms += state.timeInTicks() * us_per_quarter / division / 1000.0;
      // a tempo change applies to the following events
      if (state.status == midi::MIDI_PARSER_TRACK_META && state.meta.type == midi::MIDI_META_SET_TEMPO)
        us_per_quarter = state.tempo;
      if (state.status != midi::MIDI_PARSER_TRACK_MIDI) continue;
      event = state.midi;
      event_due_ms = (uint32_t)song_ms;
      event_pending = true;
    }
    // not yet: render audio in the meantime
    if ((int32_t)(millis() - start_ms) < (int32_t)event_due_ms) return;
    play(event);
    event_pending = false;
  }
}

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
  setupInstruments();

  auto cfg = out.defaultConfig(TX_MODE);
  cfg.copyFrom(in.defaultConfig());  // 44100 Hz, stereo, 16 bits
  out.begin(cfg);
  in.begin(cfg);

  midi_file.begin();
  parser.begin(false);  // no logging
  start_ms = millis();
}

void loop() {
  processMidiFile();
  copier.copy();
}
