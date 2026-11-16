# Performance

Measured with the [benchmark](../examples/benchmark) example on an ESP32-S3
(240 MHz, 16 MB flash, 8 MB octal PSRAM): 44.1 kHz stereo output, blocks of 256
frames (5805 us real time). The values are the share of one CPU core:

| Notes | CPU (one core) | Time per block |
|---|---|---|
| 1 | 4.0% | 230 us |
| 4 | 12.2% | 707 us |
| 10 | 28.8% | 1670 us |
| 16 | 45.3% | 2630 us |

- Each additional note costs about 160 us per block (about 150 CPU cycles per
  output sample). The values are the same for all instruments.
- The worst block is within 1% of the average: there are no peaks e.g. from
  flash cache misses.
- Samples in flash or in PSRAM give the same result: the rendering is limited
  by the CPU, not by the memory.

## Pitch shifting with PSOLA

Saxophone, notes between the recorded notes (share of one core):

| Notes | `PitchShift::Resample` | `PitchShift::Psola` |
|---|---|---|
| 1 | 4.0% | 7.0% |
| 6 | 18.6% | 36.8% |
| 10 | 30.3% | 60.9% |

PSOLA needs about twice the CPU of the resampling per note. For the
monophonic saxophone and clarinet this is not relevant; for polyphonic
instruments use it only where the timbre matters (see
[Instruments](instruments.md#pitch-shifting)).

## Guitar amplifier

The `GuitarAmp` effect is calculated once per block for the whole channel,
independent of the number of notes:

| Preset | Amp | Guitar, 6 notes + amp |
|---|---|---|
| Lead | 6.2% | 23.5% |
| Crunch | 7.3% | 24.1% |
| Clean (with chorus) | 9.5% | 26.9% |

The chorus lfo is calculated per block and the delay line is read with integer
index arithmetic; tone bands at 0 dB are skipped.

## Optimizations

The first version needed 44.5% for 10 notes. The following changes reduced
this by 35%:

| Change | 10 notes |
|---|---|
| first version | 44.5% |
| envelopes calculated per block (interpolated linearly) and local variables in the render loop | 35.9% |
| time critical code compiled with `-O2` instead of `-Os` | 28.8% |

The Arduino ESP32 core compiles with `-Os` (optimize for size). TinyInstruments
compiles its time critical code with `-O2` (`TI_OPTIMIZE`, enabled
automatically when the build optimizes for size).

## Tips

- `TI_MAX_VOICES` limits the CPU load: every additional voice costs about 2.9%
  of one core on the ESP32-S3.
- The load is proportional to the output sample rate: 32000 Hz instead of
  44100 Hz needs about 27% less (the stored samples have 32 kHz anyway).
- Run the audio output in its own task, so that midi processing (e.g. BLE)
  does not interrupt it.
