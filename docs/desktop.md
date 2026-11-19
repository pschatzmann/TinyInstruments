# Desktop Player

`tinyinstruments-player` plays the instruments on a computer with a USB midi
keyboard or a [midi file](#playing-midi-files) and the sound card. It uses exactly the same code as the
microcontroller, so you can try out the instruments, the amplifier and the
parameters before you flash a board. It can also
[forward the midi messages to a microcontroller](#forwarding-midi-to-a-microcontroller)
via serial, TCP, UDP or BLE:

```
USB midi keyboard ──▶ RtMidi ──▶ arduino-midi MidiParser ──▶ SampleSynthesizerMidiAction
                                                                     │ (lock free queue)
sound card ◀── miniaudio ◀── SampleSynthesizer::render() ◀───────────┘
```

> **Status:** built and tested on Linux (ALSA): all instruments via the
> virtual midi port, sound card output, wav recording and forwarding to
> serial, TCP, UDP, midi output ports and BLE (an ESP32-S3 with arduino-midi).
> macOS and Windows have not been tested.

## Requirements

| | |
|---|---|
| Linux | ALSA development files (`sudo apt install libasound2-dev`) |
| macOS | Xcode command line tools (CoreMIDI, CoreAudio) |
| Windows | Visual Studio or MinGW (WinMM) |
| all | CMake 3.16+, a C++11 compiler, internet access for the first build |

The following libraries are downloaded by CMake (FetchContent):

- [RtMidi](https://github.com/thestk/rtmidi) 6.0.0: midi input
- [miniaudio](https://github.com/mackron/miniaudio) 0.11.22: audio output
- [arduino-midi](https://github.com/pschatzmann/arduino-midi): midi parser
  (the sibling directory `../arduino-midi` is used if it exists)
- [arduino-midi-fileparser](https://github.com/pschatzmann/arduino-midi-fileparser):
  midi files (the sibling directory `../arduino-midi-fileparser` is used if it
  exists)

## Build

```
cmake -S . -B build -DTI_BUILD_DESKTOP=ON
cmake --build build --target tinyinstruments-player
```

The desktop player is not built by default (`TI_BUILD_DESKTOP=OFF`), so the
unit tests do not need the downloads.

## Usage

Connect the midi keyboard and start the player. Without `--port` it listens to
all midi inputs:

```
./build/desktop/tinyinstruments-player --list
./build/desktop/tinyinstruments-player --instrument grand
./build/desktop/tinyinstruments-player --instrument grand --drums
./build/desktop/tinyinstruments-player --instrument sax --port "USB"
./build/desktop/tinyinstruments-player --instrument guitar --amp lead
./build/desktop/tinyinstruments-player --instrument organ --drawbars 888800000 --leslie fast
./build/desktop/tinyinstruments-player --instrument epiano --tremolo 0.5
./build/desktop/tinyinstruments-player --instrument upright-bass
./build/desktop/tinyinstruments-player --file data/TenorSax.bin
./build/desktop/tinyinstruments-player --instrument bass
./build/desktop/tinyinstruments-player --instrument clarinet
./build/desktop/tinyinstruments-player --instrument upright --wav recording.wav --seconds 30
./build/desktop/tinyinstruments-player --send tcp:192.168.1.40 --no-audio
./build/desktop/tinyinstruments-player --midi-file song.mid --loop
./build/desktop/tinyinstruments-player --port Keystation --send midi:UM-ONE --thru --no-audio
```

Stop the player with Ctrl+C.

| Option | Description |
|---|---|
| `--list` | list the midi input and output ports |
| `--port N\|NAME` | midi input port number or part of its name (default: all) |
| `--virtual` | open a virtual midi port `TinyInstruments` (e.g. for a DAW or `aconnect`) |
| `--midi-file PATH` | play a midi file (`.mid`); the player stops at the end (see [Playing midi files](#playing-midi-files)) |
| `--loop` | play the midi file again and again, with a pause of 2 s |
| `--gm` | General MIDI: the program changes select the instruments (`GeneralMidiBank`); the default for `--midi-file`, unless `--instrument` or `--file` is given |
| `--instrument NAME` | `upright`, `grand`, `epiano`, `drums`, `sax`, `clarinet`, `guitar`, `bass`, `upright-bass` or `organ` (synthesized, see [Tonewheel Organ](organ.md)) (default: `grand`) |
| `--tremolo DEPTH` | tremolo 0..1 (default: 0.3 for `epiano`, otherwise off); the modulation wheel changes the depth |
| `--drawbars DIGITS` | organ registration: 9 digits 0..8 (default: `888000000`) |
| `--leslie MODE` | organ rotary speaker: `slow` (default), `fast` or `off`; the modulation wheel switches the speed |
| `--file PATH` | load the sample set from a file (e.g. `data/GrandPiano.bin`) |
| `--type TYPE` | `piano`, `epiano`, `drums`, `sax`, `clarinet`, `guitar`, `bass` or `upright-bass`: playing parameters for `--file` (default: from the file name) |
| `--amp PRESET` | amplifier: `clean`, `crunch`, `lead`, `bass` or `off` (default: `crunch` for the guitar, `bass` for the bass, otherwise `off`) |
| `--channel N` | midi channel 1..16 of the instrument (default: all, drums: 10) |
| `--drums` | also play the drum kit on midi channel 10 |
| `--volume V` | master volume (default: 0.5) |
| `--rate N` | sample rate (default: 44100) |
| `--buffer N` | audio buffer in frames (default: 256 = 5.8 ms) |
| `--wav FILE` | record to a wav file instead of playing on the sound card |
| `--seconds S` | stop after S seconds |
| `--verbose` | print the received midi messages (hex) |
| `--send TARGET` | forward the midi messages (repeatable, see below) |
| `--no-audio` | do not play on the desktop: only forward |
| `--thru` | forward all messages unchanged, also system exclusive, clock and active sensing (default: they are ignored), see [MIDI thru](#midi-thru-copy-an-input-to-an-output) |

All midi controllers of the instruments work as on the microcontroller, e.g.
the sustain pedal, the modulation wheel (vibrato) or CC19 to switch the amp
preset (see [Instruments](instruments.md) and [Electric Guitar](guitar.md)).

## Forwarding midi to a microcontroller

With `--send` the midi messages of the keyboard are forwarded to a
microcontroller, which plays them with TinyInstruments. They are played on the
desktop as well, unless `--no-audio` is given. `--send` can be used several
times, e.g. to play on several boards.

| Target | Transport | Receiver on the microcontroller (arduino-midi) |
|---|---|---|
| `serial:PATH[:BAUD]` | raw midi bytes, default 115200 baud | `MidiStreamIn` on `Serial` |
| `tcp:HOST[:PORT]` | raw midi bytes, default port 5008, reconnects automatically | `MidiIpServer` |
| `udp:HOST[:PORT]` | one datagram per message, default port 5008 | `MidiUdpServer` |
| `midi:NAME` | midi output port whose name contains NAME | `MidiUsbDevice` (USB midi device, e.g. ESP32-S3 with "USB-OTG (TinyUSB)" or RP2040, see [piano-usb-midi](../examples/piano-usb-midi)) |
| `ble:NAME` | BLE midi device paired by the operating system | `MidiBleServer` |

```
# USB serial of an ESP32 (MidiStreamIn(Serial) with Serial.begin(115200))
tinyinstruments-player --send serial:/dev/ttyUSB0 --no-audio
# standard midi (DIN) with a USB serial adapter
tinyinstruments-player --send serial:/dev/ttyUSB0:31250
# USB midi device (piano-usb-midi: MidiUsbDevice "TinyPiano")
tinyinstruments-player --send midi:TinyPiano --no-audio
# WiFi
tinyinstruments-player --send tcp:192.168.1.40 --no-audio
tinyinstruments-player --send udp:esp32.local:5008 --no-audio
# play on the desktop and on two boards
tinyinstruments-player --send tcp:192.168.1.40 --send tcp:192.168.1.41
```

On the microcontroller, the received messages are passed to the synthesizer
with the `SampleSynthesizerMidiAction`, e.g. for TCP:

```cpp
SampleSynthesizerMidiAction action(synth);
MidiIpServer<> midi_server(&action);

void setup() {
  WiFi.begin(SSID, PASSWORD);
  ...
  midi_server.begin(5008);
}

void loop() {
  midi_server.loop();
  copier.copy();
}
```

Notes:

- **Serial**: the port is opened without changing DTR/RTS, so most boards are
  not reset. Use the same baud rate in the sketch. On Linux any baud rate is
  supported (e.g. 31250), on macOS only the standard rates.
- **TCP**: if the connection is lost (e.g. the board restarts), it is opened
  again automatically, at most once per second.
- **UDP**: messages can get lost on a busy WiFi; TCP is more reliable.

### BLE

BLE midi is supported by the operating systems: pair the board once, then it
appears as midi output port and can be used with `--send ble:NAME`:

| | Pairing |
|---|---|
| macOS | Audio MIDI Setup → Window → Show MIDI Studio → Bluetooth Configuration → Connect |
| Linux | `bluetoothctl` → `scan on` → `pair ADDRESS` → `connect ADDRESS` (BlueZ with midi support, as in most distributions) |
| Windows | Settings → Bluetooth & devices → Add device |

```
tinyinstruments-player --list                    # the paired board is listed as output
tinyinstruments-player --send ble:TinyPiano --no-audio
```

A BLE library in the player itself was not used: the cross-platform library
SimpleBLE is under the Business Source License, which does not allow free
commercial use.

## MIDI thru: copy an input to an output

The player can copy one midi input to a midi output, e.g. a USB keyboard to a
USB midi interface, a synthesizer or a board with `MidiUsbDevice`:

```
./build/desktop/tinyinstruments-player --list
./build/desktop/tinyinstruments-player --port Keystation --send midi:UM-ONE --thru --no-audio
```

- `--port` selects the input (number or part of the name; without `--port`
  all inputs are copied), `--send midi:NAME` the output (part of the name, see
  `--list`). `--send` can be repeated, e.g. to copy to two outputs or to a
  board via TCP at the same time.
- `--thru` copies all messages unchanged, also system exclusive (e.g. patch
  dumps), clock and start/stop (e.g. to sync a drum machine) and active
  sensing. Without `--thru` these are ignored.
- Without `--no-audio` the notes are also played on the desktop.
- An output which is also one of the inputs (e.g. `Midi Through`) is refused:
  it would send the messages back to the input in an endless loop.

## Playing midi files

`--midi-file` plays a standard midi file (format 0 or 1):

```
./build/desktop/tinyinstruments-player --midi-file song.mid
./build/desktop/tinyinstruments-player --midi-file song.mid --loop
./build/desktop/tinyinstruments-player --midi-file song.mid --wav song.wav
./build/desktop/tinyinstruments-player --midi-file song.mid --instrument epiano
./build/desktop/tinyinstruments-player --midi-file song.mid --send tcp:192.168.1.40 --no-audio
```

- The file is parsed with arduino-midi-fileparser before it is played
  ([MidiFileSong.h](../desktop/MidiFileSong.h)): the tracks are merged by time
  and the tempo changes apply to all tracks. Without a tempo the song plays
  at 120 BPM.
- The General MIDI program changes of the file select the instruments
  (`GeneralMidiBank`, the full versions of the sample sets; the same mapping as
  the [midi-file-player](midi-files.md#instruments) example). With
  `--instrument` or `--file` all channels are played by that instrument
  instead (add `--gm` to use the General MIDI instruments anyway).
- The events of the file take the same way as the ones of a keyboard: they can
  be shown with `--verbose`, recorded with `--wav` and forwarded with `--send`
  (e.g. to a board with `--no-audio`). A connected keyboard can be played at
  the same time.
- At the end (and with Ctrl+C) all notes and the sustain pedal are switched
  off on all channels, also on the forward targets. Without `--loop` the player
  stops 2 s after the end of the song.

## Playing from a DAW (Linux)

With `--virtual` the player opens the midi port `TinyInstruments`, which can be
connected to other programs:

```
./build/desktop/tinyinstruments-player --virtual &
aconnect -l                               # list the ports
aplaymidi -p TinyInstruments song.mid     # play a midi file with ALSA
```

## Troubleshooting

| Problem | Solution |
|---|---|
| `no midi input` | check the keyboard with `--list`; on Linux the user needs access to the sound devices (group `audio`) |
| no sound | check the midi messages with `--verbose`; try another `--channel` |
| crackling | increase `--buffer` (e.g. 512) |
| latency too high | decrease `--buffer` (e.g. 128) |
| distorted | reduce `--volume` |
| `could not play FILE: invalid midi file` | not a standard midi file (`.mid`); e.g. RIFF midi (`.rmi`) or karaoke files with another header are not supported |
| `could not open serial ...` | check the path and the permissions (Linux: group `dialout`); close the serial monitor of the Arduino IDE |
| `could not open tcp ...` | check the IP address and that `MidiIpServer` is started on the board |
| `midi output ... is also an input` | the output would send the messages back to the input: select the input with `--port` |
| `midi output 'NAME' not found` | check `--list`; BLE devices must be paired and connected first |
