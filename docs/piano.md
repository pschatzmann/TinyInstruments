# Pianos

Two pianos are included. Both are CC0 (public domain) and need about 700 KB
of flash each:

| Header | Function | Source | Zones |
|---|---|---|---|
| `samples/UprightPianoKW.h` | `UprightPianoKW()` | [FreePats Upright Piano KW](http://freepats.zenvoid.org/Piano/acoustic-grand-piano.html) (Kawai upright) | 66: minor thirds, 2 velocity layers |
| `samples/GrandPiano.h` | `GrandPiano()` | [VCSL](https://github.com/sgossner/VCSL) Grand Piano, Steinway B | 66: major thirds, 3 velocity layers |

```cpp
#include "samples/GrandPiano.h"
Instrument piano = PianoInstrument(GrandPiano());
```

## Fitting into flash

See the [design](design.md) for the details. In short, the converter keeps
the attack and a short sustain loop of each zone (mono, 32 kHz, 4 bit ADPCM)
and re-creates the natural decay with an envelope.

| | Flash (ESP32, default partition 1.25 MB) |
|---|---|
| Piano sample data | ~700 KB |
| `piano-demo` (I2S) | 1 026 648 bytes (78%) |
| `piano-serial-midi` (I2S + serial midi) | 1 140 936 bytes (87%) |
| `piano-ble-midi` (I2S + BLE midi) | 1 961 663 bytes: too big, use "Huge APP" (3 MB): 62% |

The pianos can also be [loaded from a file into PSRAM](loading.md).

## Features

- max 10 notes at the same time (one per finger). If more notes are played,
  the quietest released note (or the oldest note) fades out within 5 ms
- velocity: loudness, velocity layer and brightness (lowpass)
- sustain pedal (CC64), keys without dampers (>= F6), key dependent release
- re-striking a sounding note, stereo spread by key
- volume (CC7), expression (CC11), pitch bend, all notes/sound off

The parameters can be changed in the `Instrument` returned by
`PianoInstrument()` (e.g. `releaseLow`, `velocityCurve`, `stereoWidth`).
