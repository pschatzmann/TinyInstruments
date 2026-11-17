# Upright Bass

`samples/UprightBass.h` contains pizzicato recordings of an acoustic double
bass (1958 Otto Rubner, fifths tuning CGDA) from A0 to C4: 14 notes in minor
thirds with 2 velocity layers. The walking bass of a jazz trio. It needs about
700 KB of flash.

Source: [Karoryfer Meatbass](https://github.com/sfzinstruments/karoryfer.meatbass) (CC0).

Smaller versions: `UprightBassSmall()` (~250 KB, `samples/UprightBassSmall.h`)
and `UprightBassOctave()` (~150 KB, `samples/UprightBassOctave.h`), see
[Versions and sizes](instruments.md#versions-and-sizes).

```cpp
#include "samples/UprightBass.h"
Instrument bass = UprightBassInstrument(UprightBass());
...
synth.setInstrument(1, bass);  // e.g. midi channel 2
```

See the [jazz-trio-serial-midi](../examples/jazz-trio-serial-midi) example
(electric piano, upright bass and drums).

[Listen to an example](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/UprightBass.mp3)
(walking bass).

## Playing

`UprightBassInstrument()`:

- up to 4 notes at the same time (one per string), no amplifier
- the velocity selects the layer and the brightness: soft notes are dark
- vibrato with the modulation wheel (CC1), pitch bend
- notes between the recorded notes are created with PSOLA
  (`PitchShift::Psola`), which keeps the attack and the length

## Conversion

The original recordings (4 velocity layers, 4 round robins) were reduced to
the softest and the loudest layer, first round robin (`--max-layers 2`). The
loud C4 recording contains a low thump instead of the note, so the f layer is
used for it. A plucked string is a little sharper at the start and drifts in
pitch, so the tuning is corrected with the pitch after the attack
(`--tune-correct`): all notes are within about 11 cents.

## Flash

| | |
|---|---|
| Sample data | 690 738 bytes + 8 004 bytes pitch marks (28 zones, 32 kHz) |
| `UprightBassSmall` | 245 228 bytes + 4 134 bytes pitch marks (14 zones, 22 kHz) |
| `UprightBassOctave` | 147 243 bytes + 2 338 bytes pitch marks (4 zones, 22 kHz) |
