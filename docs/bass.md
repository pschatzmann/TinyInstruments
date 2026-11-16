# Electric Bass

`samples/ElectricBass.h` contains direct (DI) recordings of a Squier Jazz Bass
from C#1 to D#4 (low E string, also tuned down to C#): 14 notes in minor
thirds with 2 velocity layers. It needs about 700 KB of flash.

Source: [Karoryfer Growlybass](https://github.com/sfzinstruments/karoryfer.growlybass) (CC0).

Smaller versions: `ElectricBassSmall()` (~250 KB, `samples/ElectricBassSmall.h`)
and `data/ElectricBassOctave.bin` (~150 KB), see
[Versions and sizes](instruments.md#versions-and-sizes).

```cpp
#include "samples/ElectricBass.h"
Instrument bass = BassInstrument(ElectricBass());
GuitarAmp amp(GuitarAmp::Bass);  // optional: warm, slightly driven
...
synth.setInstrument(-1, bass);
synth.setEffect(-1, &amp);
```

See the [bass-serial-midi](../examples/bass-serial-midi) example.

## Notes

The notes are mapped at the sounding pitch, like the General MIDI bass
programs: the low E string is note 28 (E1, 41 Hz). The original sfz mapping
of the library is an octave higher and one zone has a wrong root key: both
were corrected by the converter (`--transpose -12`, `--tune-correct`).

## Playing

- up to 4 notes at the same time (one per string)
- palm mute with CC20: notes decay quickly and are darker
- vibrato with the modulation wheel (CC1), pitch bend
- the velocity selects the layer (p / f) and the brightness

## Bass amplifier

The `GuitarAmp::Bass` preset keeps the full low end (cabinet low cut 30 Hz,
high cut 4 kHz) with a little drive. The guitar presets cut the fundamental
of the low E by about 19 dB, so they are not suited for the bass. The cabinet
can also be set in code: `amp.getSettings().lowCut`, `highCut`, `presence`.

## Flash

| | |
|---|---|
| Sample data | 689 036 bytes + 10 350 bytes pitch marks |
| `bass-serial-midi` (I2S + serial midi) | 1 152 096 bytes: 87% of the default partition |

## Pitch shifting

Notes between the recorded notes are created with PSOLA
(`PitchShift::Psola`): the attack and the timbre are the same as in the
recording. Set `pitchShift = PitchShift::Resample` to save CPU (see
[Instruments](instruments.md#pitch-shifting)).
