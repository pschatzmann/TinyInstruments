# Getting Started

Install [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools)
and [arduino-midi](https://github.com/pschatzmann/arduino-midi). The
[material-gui](../examples/material-gui) example also needs
[TinyMaterialDesign](https://github.com/pschatzmann/TinyMaterialDesign),
[TinyGPU](https://github.com/pschatzmann/TinyGPU) and
[arduino-audio-driver](https://github.com/pschatzmann/arduino-audio-driver).

A sketch consists of:

- a `SampleSynthesizer` which plays the notes
- one or more `Instrument`s: a sample set with the playing parameters, assigned
  to midi channels
- `SampleSynthesizerStream`: provides the audio as arduino-audio-tools stream
- `SampleSynthesizerMidiAction`: passes the midi events from arduino-midi to
  the synthesizer

The following sketch plays an instrument with a midi keyboard connected to a
serial port and outputs the sound to an I2S DAC:

```cpp
#include "AudioTools.h"
#include "Midi.h"
#include "TinyInstrumentsAudioTools.h"
#include "TinyInstrumentsMidi.h"
#include "samples/GrandPiano.h"

SampleSynthesizer synth;
Instrument piano = PianoInstrument(GrandPiano());
SampleSynthesizerStream in(synth);
I2SStream out;
StreamCopy copier(out, in);
SampleSynthesizerMidiAction action(synth);
MidiStreamIn midi_in(Serial2, action);

void setup() {
  Serial2.begin(31250, SERIAL_8N1, 21, 22);
  synth.setInstrument(-1, piano);  // all midi channels
  auto cfg = out.defaultConfig(TX_MODE);
  cfg.copyFrom(in.defaultConfig());  // 44100 Hz, stereo, 16 bits
  out.begin(cfg);
  in.begin(cfg);
}

void loop() {
  midi_in.loop();
  copier.copy();
}
```

To use another instrument, include its header and use the matching preset:

| Instrument | Header | Preset | Midi channel |
|---|---|---|---|
| Upright piano | `samples/UprightPianoKW.h` | `PianoInstrument(UprightPianoKW())` | any |
| Grand piano | `samples/GrandPiano.h` | `PianoInstrument(GrandPiano())` | any |
| Drum kit | `samples/DrumKit.h` | `DrumInstrument(DrumKit())` | 10 (`setInstrument(9, ...)`) |
| Tenor saxophone | `samples/TenorSax.h` | `SaxophoneInstrument(TenorSax())` | any |
| Electric guitar | `samples/ElectricGuitar.h` | `GuitarInstrument(ElectricGuitar())` + `GuitarAmp` effect | any |
| Clarinet | `samples/Clarinet.h` | `ClarinetInstrument(Clarinet())` | any |
| Electric bass | `samples/ElectricBass.h` | `BassInstrument(ElectricBass())` + `GuitarAmp(GuitarAmp::Bass)` | any |
| Tonewheel organ | - (synthesized) | `OrganInstrument(organ)` with `TonewheelOrgan organ("888000000")` + `RotarySpeaker` | any |

Each instrument also has a small version (~250 KB instead of ~700 KB), e.g.
`samples/GrandPianoSmall.h` with `PianoInstrument(GrandPianoSmall())`, and
(except the drum kit) an octave version (~150 KB), e.g.
`samples/GrandPianoOctave.h` with `PianoInstrument(GrandPianoOctave())`. See
[Instruments](instruments.md) for the details and
[Versions and sizes](instruments.md#versions-and-sizes) to compare them.
Instead of compiling the samples into the program, they can also be
[loaded from a file](loading.md).

## Trying it out on the desktop

The instruments can be played on a computer with a USB midi keyboard before
you flash a board: see the [Desktop Player](desktop.md).

All the examples below also build and run unchanged on the desktop (sound
card, midi on stdin, the GUI in a window): see
[running the examples on the desktop](testing.md#running-the-examples-on-the-desktop).

## Examples

| Example | Description | ESP32 partition scheme |
|---|---|---|
| [piano-demo](../examples/piano-demo) | plays by itself, no midi needed | default |
| [drum-demo](../examples/drum-demo) | plays a drum groove, no midi needed | default |
| [piano-serial-midi](../examples/piano-serial-midi) | piano, serial midi input | default |
| [sax-serial-midi](../examples/sax-serial-midi) | saxophone, serial midi input | default |
| [guitar-serial-midi](../examples/guitar-serial-midi) | electric guitar with amplifier, serial midi input | default |
| [bass-serial-midi](../examples/bass-serial-midi) | electric bass with amplifier, serial midi input | default |
| [clarinet-serial-midi](../examples/clarinet-serial-midi) | clarinet, serial midi input | default |
| [organ-serial-midi](../examples/organ-serial-midi) | tonewheel organ with rotary speaker (synthesized), serial midi input | default |
| [piano-ble-midi](../examples/piano-ble-midi) | piano, BLE midi input | Huge APP |
| [piano-drums-serial-midi](../examples/piano-drums-serial-midi) | grand piano and drums (channel 10) | Huge APP |
| [file-psram-serial-midi](../examples/file-psram-serial-midi) | instrument loaded from LittleFS into PSRAM | with file system |
| [all-instruments](../examples/all-instruments) | all instruments loaded from LittleFS into PSRAM, selected with program changes (General MIDI) | own partitions.csv (16 MB flash) |
| [material-gui](../examples/material-gui) | touch GUI ([TinyMaterialDesign](https://github.com/pschatzmann/TinyMaterialDesign)) for the ESP32-S3 2.8" display board: on-screen piano keyboard, midi input via serial/TCP/UDP, output via I2S or forwarded midi (serial/TCP/UDP) | 3 MB APP (e.g. Huge APP) |
| [benchmark](../examples/benchmark) | measures the CPU load (piano, guitar with amp) | Huge APP |
