# Electric Guitar

`samples/ElectricGuitar.h` contains direct (DI) recordings of a Fender electric
guitar (bridge pickup) from C2 to C#6: 30 zones, soft and hard velocity layers
up to E4. It needs about 700 KB of flash.

Source: [FreePats Electric Guitar FSBS direct](https://freepats.zenvoid.org/ElectricGuitar/clean-electric-guitar.html) (CC0).

Smaller versions: `ElectricGuitarSmall()` (~250 KB, `samples/ElectricGuitarSmall.h`)
and `data/ElectricGuitarOctave.bin` (~150 KB), see
[Versions and sizes](instruments.md#versions-and-sizes).

The direct signal of a guitar sounds thin on its own: like a real guitar it is
played through an amplifier. The `GuitarAmp` effect is assigned to the midi
channel of the guitar, so it only processes the guitar:

```cpp
#include "samples/ElectricGuitar.h"
Instrument guitar = GuitarInstrument(ElectricGuitar());
GuitarAmp amp(GuitarAmp::Crunch);
...
synth.setInstrument(-1, guitar);
synth.setEffect(-1, &amp);
```

See the [guitar-serial-midi](../examples/guitar-serial-midi) example.

## GuitarAmp

Signal chain: input gain (drive) → 2x oversampled, slightly asymmetric soft
clipping → tone stack (bass, mid, treble) → speaker cabinet (low cut, presence
peak, steep high cut) → echo → stereo chorus.

| Preset | Sound | Harmonics (sine test) |
|---|---|---|
| `GuitarAmp::Clean` | clean with chorus | 0% |
| `GuitarAmp::Crunch` | slightly overdriven | 5% |
| `GuitarAmp::Lead` | distorted, mid boost, echo | 29% |
| `GuitarAmp::Bass` | for the [electric bass](bass.md): full low end | - |

The settings can be changed in code:

```cpp
amp.getSettings().drive = 0.6f;  // 0..1
amp.getSettings().mid = 4.0f;    // dB
amp.update();
```

| Setting | Description |
|---|---|
| `drive` | 0..1: input gain 0..48 dB |
| `bass`, `mid`, `treble` | tone stack in dB (150 Hz, 800 Hz, 3 kHz) |
| `level` | output level |
| `asymmetry` | even harmonics (tube character) |
| `chorus` | 0..1 chorus mix |
| `echo`, `echoTime`, `echoFeedback` | echo level, time (max 0.5 s), feedback |

The echo buffer (16 bit) is allocated in PSRAM if available, otherwise on the
heap (about 33 KB for 0.38 s).

## Midi

| Message | Effect |
|---|---|
| CC19 | amp preset: 0-42 clean, 43-85 crunch, 86-127 lead |
| CC16, CC17, CC18 | drive, tone (treble), level |
| CC93, CC91 | chorus, echo |
| CC20 | palm mute (>= 64): notes decay quickly and are darker |
| CC1 | vibrato |
| pitch bend | string bends (+/- 2 semitones) |

The guitar plays up to 6 notes at the same time (one per string).

## Flash and CPU

| | |
|---|---|
| Sample data | 699 985 bytes |
| `guitar-serial-midi` (I2S + serial midi) | 1 151 888 bytes: 87% of the default partition (1.25 MB) |
| RAM of the amp | ~8 KB (chorus) + 33 KB echo buffer (PSRAM if available) |
| CPU ESP32-S3, 6 notes + amp | 23.5% (lead) to 26.9% (clean) of one core |
