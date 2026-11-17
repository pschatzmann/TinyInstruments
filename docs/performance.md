# Performance

Measured with the [benchmark](../examples/benchmark) example on an ESP32-S3
(240 MHz, 16 MB flash, 8 MB octal PSRAM): 44.1 kHz stereo output, blocks of 256
frames (5805 us real time). The values are the share of one CPU core:

| Notes | CPU (one core) | Time per block |
|---|---|---|
| 1 | 4.0% | 232 us |
| 4 | 12.4% | 723 us |
| 10 | 29.5% | 1710 us |
| 16 | 46.2% | 2684 us |

- Each additional note costs about 164 us per block (about 150 CPU cycles per
  output sample). The values are about the same for all sampled instruments
  (piano, guitar, saxophone with resampling).
- The worst block is within 1% of the average: there are no peaks e.g. from
  flash cache misses.
- Samples in flash or in PSRAM give the same result: the rendering is limited
  by the CPU, not by the memory.

## Pitch shifting with PSOLA

Saxophone, notes between the recorded notes (share of one core):

| Notes | `PitchShift::Resample` | `PitchShift::Psola` |
|---|---|---|
| 1 | 4.1% | 7.0% |
| 6 | 18.6% | 37.0% |
| 10 | 30.3% | 61.2% |

PSOLA needs about twice the CPU of the resampling per note. For the
monophonic saxophone and clarinet this is not relevant; for polyphonic
instruments use it only where the timbre matters (see
[Instruments](instruments.md#pitch-shifting)).

## Guitar amplifier

The `GuitarAmp` effect is calculated once per block for the whole channel,
independent of the number of notes:

| Preset | Amp | Guitar, 6 notes + amp |
|---|---|---|
| Lead | 6.1% | 23.9% |
| Crunch | 6.7% | 24.5% |
| Clean (with chorus) | 9.5% | 27.3% |

The chorus lfo is calculated per block and the delay line is read with integer
index arithmetic; tone bands at 0 dB are skipped.

## Tonewheel organ

The organ is synthesized: one sine (table with interpolation) per active
drawbar and note instead of the decoding of the samples (10 notes):

| Registration | CPU (one core) |
|---|---|
| `888000000` (3 drawbars) | 42.0% |
| `888888888` (9 drawbars) | 66.9% |
| `888888888` + `RotarySpeaker` | 71.5% |

The rotary speaker needs about 4.6% of one core, independent of the number of
notes. Use registrations with few drawbars or fewer notes (`maxNotes`) to
reduce the load.

## Tremolo

The `TremoloEffect` (electric piano) needs 0.3% of one core: the gain is
calculated per block (sine, interpolated linearly) and applied to the float
audio of its channels.

## Optimizations

The first version needed 44.5% for 10 notes. The following changes reduced
this by 35%:

| Change | 10 notes |
|---|---|
| first version | 44.5% |
| envelopes calculated per block (interpolated linearly) and local variables in the render loop | 35.9% |
| time critical code compiled with `-O2` instead of `-Os` | 28.8% |

The voices can also play the synthesized organ: the source (samples or organ)
is selected once per block with a template, so the sample path has no
additional check per sample (a check per sample cost 12%: 33.0% instead of
29.4%).

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
