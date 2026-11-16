# TinyInstruments

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-blue.svg)](https://www.arduino.cc/reference/en/libraries/)
[![CMake](https://img.shields.io/badge/CMake-Supported-blue.svg)](https://github.com/pschatzmann/TinyInstruments/blob/main/CMakeLists.txt)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-green.svg)](https://www.gnu.org/licenses/gpl-3.0)

Header only, sample based musical instruments for microcontrollers: realistic
sound which fits into the flash memory of an ESP32.

- Instruments (CC0, ~700 KB each): upright piano, grand piano, drum kit, tenor saxophone,
  electric guitar with amplifier, electric bass, clarinet
- Smaller versions with ~250 KB and ~150 KB ([versions and sizes](docs/instruments.md#versions-and-sizes))
- Sample data compiled into flash, or loaded at runtime from any Stream (e.g. a file) into PSRAM
- Output via [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools),
  input via [arduino-midi](https://github.com/pschatzmann/arduino-midi)
- 10 notes need about 29% of one core of an ESP32-S3
- Desktop player for a USB midi keyboard, which can also forward the midi to a
  microcontroller (serial, TCP, UDP, BLE)

## Instruments and sizes

| Instrument | full | small | octave |
|---|---|---|---|
| Upright piano | [702 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/UprightPianoKW.mp3) | [251 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/UprightPianoKWSmall.mp3) | [150 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/UprightPianoKWOctave.mp3) |
| Grand piano | [702 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/GrandPiano.mp3) | [251 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/GrandPianoSmall.mp3) | [150 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/GrandPianoOctave.mp3) |
| Drum kit | [702 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/DrumKit.mp3) | [251 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/DrumKitSmall.mp3) | - |
| Tenor saxophone | [701 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/TenorSax.mp3) | [250 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/TenorSaxSmall.mp3) | [148 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/TenorSaxOctave.mp3) |
| Clarinet | [699 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/Clarinet.mp3) | [250 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/ClarinetSmall.mp3) | [150 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/ClarinetOctave.mp3) |
| Electric guitar | [701 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/ElectricGuitar.mp3) | [251 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/ElectricGuitarSmall.mp3) | [150 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/ElectricGuitarOctave.mp3) |
| Electric bass | [700 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/ElectricBass.mp3) | [250 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/ElectricBassSmall.mp3) | [150 KB](https://raw.githubusercontent.com/pschatzmann/TinyInstruments/main/docs/audio/ElectricBassOctave.mp3) |

- **full**: all recordings and velocity layers, 32 kHz
- **small**: one velocity layer, 22 kHz
- **octave**: one recording per octave, one velocity layer, 22 kHz (only as data file)

Click on a size to listen to an example recording of this version (the same phrase for all
versions of an instrument). Details and quality measurements:
[versions and sizes](docs/instruments.md#versions-and-sizes).

## Documentation

- [Getting Started](docs/getting-started.md)
- [Instruments](docs/instruments.md): [Pianos](docs/piano.md), [Drum Kit](docs/drums.md), [Saxophone](docs/saxophone.md), [Electric Guitar](docs/guitar.md), [Electric Bass](docs/bass.md), [Clarinet](docs/clarinet.md)
- [Loading Sample Sets from Files / PSRAM](docs/loading.md)
- [Performance](docs/performance.md)
- [Desktop Player](docs/desktop.md)
- [Design](docs/design.md)
- [Creating your own Sample Sets](docs/sample-sets.md)
- [Testing](docs/testing.md)

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

I recommend to use git because you can easily update to the latest version just by executing the ```git pull``` command in the project folder.
To use the library on the desktop (tests, desktop player), see [Testing](docs/testing.md) and [Desktop Player](docs/desktop.md).

## License

The library is published under the [GNU General Public License v3.0](LICENSE).

The sample data (`src/samples`, `data`) is in the public domain
([CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/)):

| Sample set | Source |
|---|---|
| UprightPianoKW | [FreePats Upright Piano KW](http://freepats.zenvoid.org/Piano/acoustic-grand-piano.html) |
| GrandPiano, TenorSax | [VCSL](https://github.com/sgossner/VCSL) |
| DrumKit | [Versilian Studios Virtuosity Drums](https://versilian-studios.com/?p=694) |
| ElectricGuitar | [FreePats Electric Guitar FSBS](https://freepats.zenvoid.org/ElectricGuitar/clean-electric-guitar.html) |
| ElectricBass | [Karoryfer Growlybass](https://github.com/sfzinstruments/karoryfer.growlybass) |
| Clarinet | [VSCO 2 Community Edition](https://github.com/sgossner/VSCO-2-CE) |
