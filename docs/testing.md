# Testing

The core does not depend on Arduino, so it can be built and tested on the
desktop with CMake:

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

| Test | Content |
|---|---|
| `test_adpcm` | ADPCM decoding, seeking to a saved decoder state |
| `test_voice` | pitch, loops, decay, release, stereo |
| `test_synthesizer` | midi handling, polyphony, voice stealing, sustain, choke groups, instrument bank and program changes |
| `test_piano` | both pianos: flash budget, all keys, pitch, velocity, release, 10 notes |
| `test_drums` | drum kit: General MIDI keys, one shots, hi-hat choke, groove |
| `test_sax` | saxophone: pitch and octave, sustain, monophonic, legato, vibrato, breath |
| `test_loader` | loading the `data/*.bin` files, chunked streams, errors |
| `test_clarinet` | clarinet: pitch and octave, corrected root key, sustain, legato, no automatic vibrato |
| `test_psola` | PSOLA: lossless at ratio 1, pitch, length kept (amplitude step), continuity, level, loops, one shots, legato start, formants kept with the real clarinet |
| `test_small` | small sample sets: size, binary files, every key compared with the full version, pitch |
| `test_organ` | tonewheel organ: pitch of the drawbars, foldback, sustain, release, percussion (single triggered), drawbars with CCs; rotary speaker: stereo, slow and fast modulation, acceleration |
| `test_octave` | octave sample sets: size, headers identical with the data files, every key sounds, pitch |
| `test_forwarders` | midi forwarding of the desktop player: serial (pseudo terminal, 115200 and 31250 baud), TCP with reconnect, UDP, target parsing (POSIX only) |
| `test_bass` | bass: pitch and octave down to the low E, bass amp keeps the low end, 4 strings, palm mute |
| `test_guitar` | guitar: pitch, all keys, amp distortion per preset, echo tail, effect only on its channel, palm mute, midi control |

To listen to the result, render a demo of all instruments to a wav file:

```
./build/tests/render_demo demo.wav
```

The example recordings of the README (`docs/audio`) are rendered from the
data files with the same phrase for all versions of an instrument:

```
./build/tests/render_examples /tmp/examples
for f in /tmp/examples/*.wav; do
  ffmpeg -i "$f" -codec:a libmp3lame -b:a 96k "docs/audio/$(basename "${f%.wav}").mp3"
done
```

## Running the examples on the desktop

All example sketches also build and run unchanged on the desktop with the
[Arduino-Emulator](https://github.com/pschatzmann/Arduino-Emulator):

```
cmake -S . -B build-examples -DTI_BUILD_EXAMPLES=ON
cmake --build build-examples
./build-examples/examples/piano-demo
ctest --test-dir build-examples -L examples   # each sketch must run for 3 seconds
```

The examples are not built by default (`TI_BUILD_EXAMPLES=OFF`). Requirements:
CMake 3.16+, a C++17 compiler, a sound card, internet access for the first
build and, for material-gui, SDL2 (`sudo apt install libsdl2-dev`; without
SDL2 material-gui is skipped). Tested on Linux.

The sketches contain no desktop specific code. The platform specific parts
are provided by the libraries and, for the hardware of material-gui, by
[desktop/emulator](../desktop/emulator), which is only used by this build:

| Sketch uses | Desktop | Provided by |
|---|---|---|
| `I2SStream` | sound card (miniaudio) | [TinyInstrumentsAudioTools.h](../src/TinyInstrumentsAudioTools.h) |
| `I2SCodecStream` | sound card (miniaudio), the board is ignored | arduino-audio-tools |
| `Serial`, `Serial2` (serial midi) | stdout / stdin: e.g. `printf '\x90\x3c\x64' \| ./build-examples/examples/piano-serial-midi` | Arduino-Emulator |
| `LittleFS` | the `data` folder of this library (environment variable `LITTLEFS_ROOT`) | Arduino-Emulator |
| `WiFi`, TCP, UDP | the network of the computer | Arduino-Emulator |
| `ESP.getFreeHeap()` ... | memory of the computer, no PSRAM | Arduino-Emulator |
| `audio_tools::Task` | a thread | arduino-audio-tools |
| `MidiBleServer` | not supported: an error is logged | arduino-midi |
| display board `LCDBoardESP32S3_2_8Display` | SDL window (240x320), mouse as touch | [DesktopBoards.h](../desktop/emulator/DesktopBoards.h) |
| audio board `ESP32S3HosyondDisplay` | placeholder: arduino-audio-driver is not used on the desktop | [AudioBoards/ESP32S3HosyondDisplay.h](../desktop/emulator/AudioBoards/ESP32S3HosyondDisplay.h) |

arduino-audio-tools, arduino-midi, TinyMaterialDesign and TinyGPU are used
from the sibling directories (Arduino libraries folder) if available,
otherwise they are downloaded together with the Arduino-Emulator and
miniaudio. Use `-DFETCHCONTENT_SOURCE_DIR_ARDUINO_EMULATOR=<dir>` for a local
checkout of the Arduino-Emulator.

The CPU load on a microcontroller is measured with the
[benchmark](../examples/benchmark) example (see [Performance](performance.md)).
