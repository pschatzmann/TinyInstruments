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
  to the `SampleSynthesizer`; meta events (e.g. tempo changes) only advance the
  time.

## Instruments

The small versions of the sample sets are compiled in (8 x 250 KB: select the
partition scheme "Huge APP"). General MIDI has 128 programs; the ones without
a matching instrument are played by the most similar one:

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
it is 6 dB louder than its preset (`CLARINET_VOLUME`). The piccolo and the
whistle sound higher than the range of the clarinet: they are played by
copies of the clarinet which are transposed one or two octaves down
(`Instrument::transpose`, see [Transposition](instruments.md#transposition)).

Change the mapping in `setupInstruments()` of the sketch.

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
With the [desktop player](desktop.md#playing-from-a-daw-or-a-midi-file-linux)
a midi file can be played with `aplaymidi` instead.
