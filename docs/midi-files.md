# Playing MIDI Files

The [midi-file-player](../examples/midi-file-player) example plays a standard
MIDI file (`.mid`) with the instruments of TinyInstruments. The file is parsed
with [arduino-midi-fileparser](https://github.com/pschatzmann/arduino-midi-fileparser),
the program changes of the file (General MIDI) select the instruments with an
[InstrumentBank](instruments.md).

```
cd ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/arduino-midi-fileparser.git
```

By default the example plays the midi file which is included in
arduino-midi-fileparser (`examples/example-midi.h`: a song with 4781 notes on
15 channels, 3:55 minutes). At the end it starts again after a pause of 2 s.

## How it works

- The file is read in pieces of 256 bytes while it is played, so it never has
  to be in memory as a whole. The source is any Arduino `Stream`.
- Each midi event is played when its time has come; in the meantime the audio
  is rendered (no `delay()`: the I2S output must never run empty).
- The time of the events is calculated from the ticks and the tempo of the
  file with floating point numbers. The rounded milliseconds of the parser
  (`timeInMs()`) would add up to an error of 2.5 s over the example song.
- The note, control change, program change and pitch bend events are passed
  to the `SampleSynthesizer`; meta events only advance the time. A tempo
  change applies to the events after it; until the first one the tempo is
  120 BPM (the default of the midi standard).
- **Only single track files (format 0)** are supported: the events are played
  in the order of the file, so the tracks of a multi track file (format 1)
  would be played one after the other. Convert such a file to format 0 (e.g.
  with a DAW or a midi editor) or play it with the
  [desktop player](#on-the-desktop), which merges the tracks. The example song
  is a format 0 file.

## Instruments

The instruments are defined by the `GeneralMidiBank` of the library, which is
also used by the [desktop player](desktop.md#playing-midi-files). The sketch
passes the small versions of the sample sets (8 x 250 KB: select the
partition scheme "Huge APP"):

```cpp
GeneralMidiBank instruments;

void setupInstruments() {
  GeneralMidiBank::SampleSets sets;
  sets.piano = &GrandPianoSmall();
  sets.electricPiano = &WurlitzerSmall();
  ...
  sets.drums = &DrumKitSmall();
  instruments.begin(synth, sets);  // piano until the first program change, drums on channel 10
}
```

A sample set which is not provided (`nullptr`) is replaced: the electric piano
by the piano, the upright bass by the electric bass; the other instruments are
silent. General MIDI has 128 programs; the ones without a matching instrument
are played by the most similar one:

| Program (General MIDI) | Instrument |
|---|---|
| 0-3, 6-15 pianos, mallets | grand piano |
| 4, 5 electric pianos | electric piano (Wurlitzer), tremolo |
| 16-23 organs | tonewheel organ, rotary speaker |
| 24-31 guitars | electric guitar, clean amp |
| 32 acoustic bass | upright bass |
| 33-39 basses | electric bass, bass amp |
| 40-55 strings, choirs | tonewheel organ (sustained sound) |
| 56-63 brass | tenor saxophone |
| 64-67 saxophones | tenor saxophone |
| 68-79 reeds, pipes | clarinet (+6 dB; piccolo 1 octave lower, whistle 2 octaves lower) |
| 80-103 synthesizer leads, pads | tonewheel organ |
| 104-111 ethnic instruments | electric guitar, clean amp |
| 112-127 sound effects | silent |
| channel 10 | drum kit |

The sound effects are silent (an `Instrument` without sample set): e.g. a
"reverse cymbal" would otherwise be played as piano notes. The saxophone is
monophonic: a brass section plays only its top voice.

The clarinet plays the flute parts (e.g. the theme of the example song), so
it is 6 dB louder than its preset (`GeneralMidiBank::clarinetVolume`, set it
before `begin()`). The piccolo and the
whistle sound higher than the range of the clarinet: they are played by
copies of the clarinet which are transposed one or two octaves down
(`Instrument::transpose`, see [Transposition](instruments.md#transposition)).

To change the mapping, add entries with `instruments.bank().add(...)` after
`begin()` (later entries take precedence, see
[Program changes](instruments.md#program-changes)); the instruments and effects
which you add must stay alive while the synthesizer uses them.

The instruments share `TI_MAX_VOICES` (16 in the example) voices. A song with
many instruments needs more CPU than a single instrument: see
[Performance](performance.md).

## Playing your own file

Replace the `MemoryStream` with any other `Stream`, e.g. a file from LittleFS
or an SD card (copy the `.mid` file to the data folder of the sketch and
upload it):

```cpp
#include <LittleFS.h>
File midi_file;  // instead of the MemoryStream

void setup() {
  ...
  LittleFS.begin();
  midi_file = LittleFS.open("/song.mid", "r");
  parser.begin(false);
}
```

To play it again at the end, use `midi_file.seek(0)` instead of
`midi_file.begin()`.

## On the desktop

The example also runs on the desktop (see
[running the examples on the desktop](testing.md#running-the-examples-on-the-desktop)).

The [desktop player](desktop.md#playing-midi-files) plays any midi file with
the same `GeneralMidiBank`, but with the full versions of the sample sets:

```
./build/desktop/tinyinstruments-player --midi-file song.mid
```

It reads the whole file first, so it also plays multi track files (format 1):
the ticks are counted per track, the tracks are merged by time and the tempo
changes (usually in the first track) apply to all tracks. The events can also
be [forwarded to a microcontroller](desktop.md#forwarding-midi-to-a-microcontroller),
e.g. to play a song on the board without storing it there.
