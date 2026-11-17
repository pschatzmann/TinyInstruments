# Clarinet

`samples/Clarinet.h` contains a B♭ clarinet from D3 to F#6 (sounding pitch):
11 notes with 3 velocity layers, sustained without vibrato. It needs about
700 KB of flash.

Source: [VSCO 2 Community Edition](https://github.com/sgossner/VSCO-2-CE) (CC0).

Smaller versions: `ClarinetSmall()` (~250 KB, `samples/ClarinetSmall.h`) and
`ClarinetOctave()` (~150 KB, `samples/ClarinetOctave.h`), see
[Versions and sizes](instruments.md#versions-and-sizes).

```cpp
#include "samples/Clarinet.h"
Instrument clarinet = ClarinetInstrument(Clarinet());
synth.setInstrument(-1, clarinet);
```

See the [clarinet-serial-midi](../examples/clarinet-serial-midi) example.

## Playing

The clarinet is played like the [saxophone](saxophone.md):

- the tone holds its level as long as the key is pressed
- monophonic with legato: overlapping notes are connected without a new
  attack; releasing the upper note of a trill returns to the held note
- no automatic vibrato (classical clarinet), up to 20 cents with the
  modulation wheel (CC1)
- dynamics with the velocity, a breath controller (CC2) or expression (CC11)

## Notes

The file names of the samples are an octave below the sounding pitch and the
highest sample (named F#) sounds F. Both were corrected by the converter
(`files2sfz.py --transpose 12`, `sfz2header.py --tune-correct`).

## Flash

| | |
|---|---|
| Sample data | 651 585 bytes + 46 526 bytes pitch marks |

## Pitch shifting

Notes between the recorded notes are created with PSOLA
(`PitchShift::Psola`): the attack and the timbre are the same as in the
recording. Set `pitchShift = PitchShift::Resample` to save CPU (see
[Instruments](instruments.md#pitch-shifting)).
