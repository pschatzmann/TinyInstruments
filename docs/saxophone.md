# Saxophone

`samples/TenorSax.h` contains a tenor saxophone (G#2 to E6): 23 notes in whole
tone steps with 2 velocity layers, recorded without vibrato. It needs about
700 KB of flash.

Source: [VCSL](https://github.com/sgossner/VCSL) Tenor Saxophone (CC0).

Smaller versions: `TenorSaxSmall()` (~250 KB, `samples/TenorSaxSmall.h`) and
`TenorSaxOctave()` (~150 KB, `samples/TenorSaxOctave.h`), see
[Versions and sizes](instruments.md#versions-and-sizes).

```cpp
#include "samples/TenorSax.h"
Instrument sax = SaxophoneInstrument(TenorSax());
synth.setInstrument(-1, sax);
```

## Playing

- **Sustain**: the tone holds its level as long as the key is pressed
  (constant level loop) and fades out within 0.12 s after the note off.
- **Monophonic**: only one note sounds at the same time.
- **Legato**: if a key is pressed while another one is still held, the new
  note starts without the tongued attack and replaces the old one within 40 ms.
  Releasing the upper note of a trill returns to the note which is still held.
  Detached notes start with the full attack.
- **Vibrato**: long notes get a natural vibrato (12 cents) after 0.45 s. The
  modulation wheel (CC1) adds up to 35 cents.
- **Dynamics**: the velocity selects the layer and the brightness. Use a
  breath controller (CC2) or an expression pedal (CC11) to shape the notes.

The vibrato is created by the player: the samples are recorded without vibrato,
because they can be looped much better.

## Flash

| | Flash (ESP32) |
|---|---|
| Sample data | 659 686 bytes + 39 654 bytes pitch marks |
| `sax-serial-midi` (I2S + serial midi) | 1 141 836 bytes: 87% of the default partition |

## Pitch shifting

Notes between the recorded notes are created with PSOLA
(`PitchShift::Psola`): the attack and the timbre are the same as in the
recording. Set `pitchShift = PitchShift::Resample` to save CPU (see
[Instruments](instruments.md#pitch-shifting)).
