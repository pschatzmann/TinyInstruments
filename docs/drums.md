# Drum Kit

`samples/DrumKit.h` contains an acoustic drum kit with percussion, mapped to
the General MIDI percussion keys. It needs about 700 KB of flash.

Source: [Virtuosity Drums](https://versilian-studios.com/?p=694)
by Versilian Studios (CC0, public domain), mid microphone, 2 velocity layers.

`samples/DrumKitSmall.h` (`DrumKitSmall()`, ~250 KB) has the same keys with one
velocity layer at 22 kHz. There is no octave version: each key is a different
sound, so the keys can't be created by transposing (see
[Versions and sizes](instruments.md#versions-and-sizes)).

```cpp
#include "samples/DrumKit.h"
Instrument drums = DrumInstrument(DrumKit());
synth.setInstrument(9, drums);  // midi channel 10
```

## Keys

| Key | Sound | Key | Sound |
|---|---|---|---|
| 35 | Kick (snares off) | 51 | Ride |
| 36 | Kick | 53 | Ride bell |
| 37 | Side stick (stick shot) | 54 | Tambourine |
| 38 | Snare | 55 | Flat ride (as splash) |
| 39 | Snare off center | 56 | Cowbell |
| 40 | Snare rim shot | 57 | Crash (sizzle) |
| 41 | Low tom | 58 | Vibraslap |
| 42 | Hi-hat closed | 59 | Flat ride |
| 43 | Low tom off center | 60, 61 | Bongo high, low |
| 44 | Hi-hat pedal | 62, 63, 64 | Conga mute, open, tumba |
| 45 | Low tom rim shot | 67, 68 | Agogo high, low |
| 46 | Hi-hat open | 69, 70 | Cabasa, shaker |
| 47 | Low tom cross stick | 71, 72 | Whistle short, long |
| 48 | High tom | 73, 74 | Guiro short, long |
| 49 | Crash | 75 | Claves |
| 50 | High tom off center | 80, 81 | Triangle mute, open |
| | | 82, 83, 84 | Shaker, sleigh bells, bell tree |

Keys without sample (e.g. timbales 65, 66) are silent.

## Behavior

- one shot: note off is ignored, every sound decays naturally
- the closed (42) and pedal (44) hi-hat silence the open hi-hat (46)
- no transposition: only the exact keys are played
- max 8 drum notes at the same time (`Instrument::maxNotes`)

## Flash

| | Flash (ESP32) |
|---|---|
| `drum-demo` (I2S) | 1 026 788 bytes: 78% of the default partition (1.25 MB) |
| `piano-drums-serial-midi` | 1 843 828 bytes: too big for the default partition, 58% of "Huge APP" (3 MB) |

## Piano and drums together

Both sample sets need 1.4 MB, so select the "Huge APP" partition scheme on an
ESP32. The instruments share the voices of the synthesizer, so allow more
notes at the same time before including the library:

```cpp
#define TI_MAX_VOICES 16
#include "TinyInstrumentsAudioTools.h"
```

See the [piano-drums-serial-midi](../examples/piano-drums-serial-midi) example.
