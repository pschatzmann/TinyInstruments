# TinyInstruments

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-blue.svg)](https://www.arduino.cc/reference/en/libraries/)
[![CMake](https://img.shields.io/badge/CMake-Supported-blue.svg)](https://github.com/pschatzmann/TinyInstruments/blob/main/CMakeLists.txt)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-green.svg)](https://www.gnu.org/licenses/gpl-3.0)

Header only musical instruments for microcontrollers: realistic, sample based
sound which fits into the flash memory of an ESP32, and a synthesized
tonewheel organ.

- Instruments (~700 KB each): upright piano, grand piano, electric piano (Wurlitzer),
  drum kit, tenor saxophone, clarinet, electric guitar with amplifier, electric bass,
  upright bass
- Tonewheel organ (Hammond style) with rotary speaker (Leslie): synthesized, no sample data
- Smaller versions with ~250 KB and ~150 KB, also as headers ([versions and sizes](docs/instruments.md#versions-and-sizes))
- Sample data compiled into flash, or loaded at runtime from any Stream (e.g. a file) into PSRAM
- Output via [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools),
  input via [arduino-midi](https://github.com/pschatzmann/arduino-midi) (serial, BLE, TCP, UDP and
  USB midi as device or host)
- 10 piano notes need about 29% of one core of an ESP32-S3
- Examples from simple serial midi sketches up to a touch GUI with an on-screen
  piano keyboard ([TinyMaterialDesign](https://github.com/pschatzmann/TinyMaterialDesign))
  and a player for General MIDI files ([arduino-midi-fileparser](https://github.com/pschatzmann/arduino-midi-fileparser))
- All example sketches also build and run unchanged on the desktop
  ([Arduino-Emulator](https://github.com/pschatzmann/Arduino-Emulator))
- Desktop player for a USB midi keyboard, which can also forward the midi to a
  microcontroller (serial, TCP, UDP, BLE)

## Instruments and sizes

| Instrument | full | small | octave |
|---|---|---|---|
| Upright piano | [702 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/UprightPianoKW.mp3) | [251 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/UprightPianoKWSmall.mp3) | [150 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/UprightPianoKWOctave.mp3) |
| Grand piano | [702 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/GrandPiano.mp3) | [251 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/GrandPianoSmall.mp3) | [150 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/GrandPianoOctave.mp3) |
| Drum kit | [702 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/DrumKit.mp3) | [251 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/DrumKitSmall.mp3) | - |
| Tenor saxophone | [701 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/TenorSax.mp3) | [250 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/TenorSaxSmall.mp3) | [148 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/TenorSaxOctave.mp3) |
| Clarinet | [699 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/Clarinet.mp3) | [250 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/ClarinetSmall.mp3) | [150 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/ClarinetOctave.mp3) |
| Electric guitar | [701 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/ElectricGuitar.mp3) | [251 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/ElectricGuitarSmall.mp3) | [150 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/ElectricGuitarOctave.mp3) |
| Electric bass | [700 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/ElectricBass.mp3) | [250 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/ElectricBassSmall.mp3) | [150 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/ElectricBassOctave.mp3) |
| Electric piano | [696 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/Wurlitzer.mp3) | [245 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/WurlitzerSmall.mp3) | [136 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/WurlitzerOctave.mp3) |
| Upright bass | [700 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/UprightBass.mp3) | [250 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/UprightBassSmall.mp3) | [150 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/UprightBassOctave.mp3) |
| Tonewheel organ | [synthesized: 0 KB](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/TonewheelOrgan.mp3) | - | - |

- **full**: all recordings and velocity layers, 32 kHz
- **small**: one velocity layer, 22 kHz
- **octave**: one recording per octave, one velocity layer, 22 kHz
- **organ**: synthesized (drawbars, percussion, rotary speaker), no sample data

Click on a size to listen to an example recording of this version (the same phrase for all
versions of an instrument). Details and quality measurements:
[versions and sizes](docs/instruments.md#versions-and-sizes).

## Documentation

- [Getting Started](docs/getting-started.md) (with the list of [examples](docs/getting-started.md#examples))
- [Instruments](docs/instruments.md): [Pianos](docs/piano.md), [Electric Piano](docs/electric-piano.md), [Drum Kit](docs/drums.md), [Saxophone](docs/saxophone.md), [Clarinet](docs/clarinet.md), [Electric Guitar](docs/guitar.md), [Electric Bass](docs/bass.md), [Upright Bass](docs/upright-bass.md), [Tonewheel Organ](docs/organ.md)
- [Loading Sample Sets from Files / PSRAM](docs/loading.md)
- [Playing MIDI Files](docs/midi-files.md)
- [Performance](docs/performance.md)
- [Desktop Player](docs/desktop.md)
- [Design](docs/design.md)
- [Creating your own Sample Sets](docs/sample-sets.md)
- [Testing](docs/testing.md) and [running the examples on the desktop](docs/testing.md#running-the-examples-on-the-desktop)

## Installation in Arduino

You can download the library as zip and call include Library -> zip library. Or you can git clone this project into the Arduino libraries folder e.g. with

```
cd  ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/TinyInstruments.git
```

The examples also need the following libraries, which are installed the same way:

```
git clone https://github.com/pschatzmann/arduino-audio-tools.git
git clone https://github.com/pschatzmann/arduino-midi.git
```

The [midi-file-player](examples/midi-file-player) example additionally needs:

```
git clone https://github.com/pschatzmann/arduino-midi-fileparser.git
```

The [material-gui](examples/material-gui) example additionally needs:

```
git clone https://github.com/pschatzmann/TinyMaterialDesign.git
git clone https://github.com/pschatzmann/TinyGPU.git
git clone https://github.com/pschatzmann/arduino-audio-driver.git
```

I recommend to use git because you can easily update to the latest version just by executing the ```git pull``` command in the project folder.
To use the library on the desktop (tests, example sketches, desktop player), see [Testing](docs/testing.md) and [Desktop Player](docs/desktop.md).

## License

The library is published under the [GNU General Public License v3.0](LICENSE).

The sample data (`src/samples`, `data`) is in the public domain
([CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/)), except the
electric piano: [CC-BY 3.0](https://creativecommons.org/licenses/by/3.0/), which
requires attribution ("Wurlitzer samples by Greg Sullivan"). The tonewheel organ
is synthesized and uses no samples:

| Sample set | Source |
|---|---|
| UprightPianoKW | [FreePats Upright Piano KW](http://freepats.zenvoid.org/Piano/acoustic-grand-piano.html) |
| GrandPiano, TenorSax | [VCSL](https://github.com/sgossner/VCSL) |
| DrumKit | [Versilian Studios Virtuosity Drums](https://versilian-studios.com/?p=694) |
| ElectricGuitar | [FreePats Electric Guitar FSBS](https://freepats.zenvoid.org/ElectricGuitar/clean-electric-guitar.html) |
| ElectricBass | [Karoryfer Growlybass](https://github.com/sfzinstruments/karoryfer.growlybass) |
| Clarinet | [VSCO 2 Community Edition](https://github.com/sgossner/VSCO-2-CE) |
| UprightBass | [Karoryfer Meatbass](https://github.com/sfzinstruments/karoryfer.meatbass) |
| Wurlitzer (CC-BY 3.0) | [Greg Sullivan's E-Pianos](https://github.com/sfzinstruments/GregSullivan.E-Pianos), Wurlitzer EP200 by [Greg Sullivan](http://www.sullivang.net/) |
