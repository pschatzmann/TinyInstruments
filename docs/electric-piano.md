# Electric Piano

`samples/Wurlitzer.h` contains recordings of a Wurlitzer EP200 electric piano
from A1 to A♭6: 20 notes with 4 velocity layers (pp, mp, f, ff). The loud
layers have the characteristic "bark" of the Wurlitzer, which is typical for
soul, blues and rock. It needs about 700 KB of flash.

Source: [Greg Sullivan's E-Pianos](https://github.com/sfzinstruments/GregSullivan.E-Pianos),
Wurlitzer EP200 v1.1 by [Greg Sullivan](http://www.sullivang.net/), SFZ
mapping by kinwie. License: [CC-BY 3.0](https://creativecommons.org/licenses/by/3.0/):
unlike the other sample sets (CC0), the use **requires attribution**, e.g.
"Wurlitzer samples by Greg Sullivan (CC-BY 3.0)".

Smaller versions: `WurlitzerSmall()` (~250 KB, `samples/WurlitzerSmall.h`)
and `WurlitzerOctave()` (~150 KB, `samples/WurlitzerOctave.h`), see
[Versions and sizes](instruments.md#versions-and-sizes). They keep the
loudest layer (with the bark): soft notes are created from it with the
velocity lowpass.

```cpp
#include "samples/Wurlitzer.h"
Instrument epiano = ElectricPianoInstrument(Wurlitzer());
TremoloEffect tremolo(5.5f, 0.3f);  // optional: the built-in "vibrato" of the Wurlitzer
...
synth.setInstrument(-1, epiano);
synth.setEffect(-1, &tremolo);
```

See the [jazz-trio-serial-midi](../examples/jazz-trio-serial-midi) example.

[Listen to an example](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/Wurlitzer.mp3)
(ii - V - I with jazz voicings, a melody, tremolo).

## Playing

`ElectricPianoInstrument()`:

- 10 notes, all keys have dampers (short release), sustain pedal
- the velocity selects the layer (pp ... ff) and the brightness
- little stereo spread; volume 0.6 (the recordings are louder than the other
  instruments)

## Tremolo

The `TremoloEffect` is the amplitude modulation of the Wurlitzer (called
"vibrato" on the instrument). With `stereo = true` the left and right channel
are modulated in opposite phase: the auto pan of a Rhodes suitcase piano. It
can also be used for guitars or organs.

```cpp
TremoloEffect tremolo(5.5f, 0.3f, false);  // rate (Hz), depth (0..1), stereo
tremolo.setDepth(0.5f);
tremolo.setRate(4.0f);
```

| CC | Function |
|---|---|
| 1 (modulation wheel) | depth |
| 76 | rate (1 - 10 Hz) |

## Flash

| | |
|---|---|
| Sample data | 694 133 bytes (48 zones, 32 kHz) |
| `WurlitzerSmall` | 244 744 bytes (13 zones, 22 kHz) |
| `WurlitzerOctave` | 136 212 bytes (5 zones, 22 kHz) |
