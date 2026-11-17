# Getting Started

Install [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools)
and [arduino-midi](https://github.com/pschatzmann/arduino-midi). The
[material-gui](../examples/material-gui) example also needs
[TinyMaterialDesign](https://github.com/pschatzmann/TinyMaterialDesign),
[TinyGPU](https://github.com/pschatzmann/TinyGPU) and
[arduino-audio-driver](https://github.com/pschatzmann/arduino-audio-driver), the
[midi-file-player](../examples/midi-file-player) example
[arduino-midi-fileparser](https://github.com/pschatzmann/arduino-midi-fileparser).

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

Other midi inputs of arduino-midi are used the same way, e.g. USB midi
(`MidiUsb.h`, see [piano-usb-midi](../examples/piano-usb-midi)):

```cpp
#include "MidiUsb.h"  // instead of Midi.h
...
MidiUsbDevice midi_in("TinyPiano", &action);  // the board is a USB midi device
// MidiUsbHost midi_in(&action);              // or: a USB midi keyboard on the board

void setup() {
  midi_in.begin();
  ...
}
```

`MidiUsbDevice` needs a board with native USB: ESP32-S2/S3/P4 with Tools - USB
Mode "USB-OTG (TinyUSB)", RP2040/RP2350 (`#include <MIDIUSB.h>` before
`MidiUsb.h` with the default "Pico SDK" USB stack, or `<Adafruit_TinyUSB.h>`
with "Adafruit TinyUSB"). `MidiUsbHost` needs the ESP32 Arduino core >= 3.3 (or
Adafruit TinyUSB with host support) and an OTG adapter which supplies 5V to the
keyboard; while the host is running the USB port can not be used for `Serial`.
See the [arduino-midi](https://github.com/pschatzmann/arduino-midi#usb-midi)
documentation for all platforms.

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
| Electric piano | `samples/Wurlitzer.h` | `ElectricPianoInstrument(Wurlitzer())` + optional `TremoloEffect` | any |
| Upright bass | `samples/UprightBass.h` | `UprightBassInstrument(UprightBass())` | any |
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

The instruments can be played on a computer with a USB midi keyboard or a
midi file before you flash a board: see the [Desktop Player](desktop.md).

All the examples below also build and run unchanged on the desktop (sound
card, serial midi on stdin, USB midi from the keyboards connected to the
computer (Linux), the GUI in a window): see
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
| [midi-file-player](../examples/midi-file-player) | plays a single track midi file ([arduino-midi-fileparser](https://github.com/pschatzmann/arduino-midi-fileparser)), the General MIDI programs select the instruments (`GeneralMidiBank`), see [Playing MIDI Files](midi-files.md) | Huge APP |
| [jazz-trio-serial-midi](../examples/jazz-trio-serial-midi) | electric piano (channel 1), upright bass (channel 2) and drums (channel 10), small versions | default |
| [piano-ble-midi](../examples/piano-ble-midi) | piano, BLE midi input | Huge APP |
| [piano-usb-midi](../examples/piano-usb-midi) | piano, USB midi input: the board is a USB midi device (arduino-midi `MidiUsbDevice`, e.g. ESP32-S3 with "USB-OTG (TinyUSB)", RP2040) | default |
| [piano-drums-serial-midi](../examples/piano-drums-serial-midi) | grand piano and drums (channel 10) | Huge APP |
| [file-psram-serial-midi](../examples/file-psram-serial-midi) | instrument loaded from LittleFS into PSRAM | with file system |
| [all-instruments](../examples/all-instruments) | all instruments loaded from LittleFS into PSRAM, selected with program changes (General MIDI) | own partitions.csv (16 MB flash) |
| [material-gui](../examples/material-gui) | touch GUI ([TinyMaterialDesign](https://github.com/pschatzmann/TinyMaterialDesign)) for the ESP32-S3 2.8" display board: on-screen piano keyboard, light/dark theme (app bar menu), midi input via serial/USB (keyboard on the USB host port)/TCP/UDP, output via I2S or forwarded midi (serial/TCP/UDP) | Custom: own partitions.csv (16 MB flash, 6 MB application) |
| [benchmark](../examples/benchmark) | measures the CPU load (piano, guitar with amp) | Huge APP |
