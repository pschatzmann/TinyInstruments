# Design

## Goals

- Realistic instruments on a microcontroller (target: ESP32 with 4 MB flash)
- Sample data must fit into flash (PROGMEM): about 700 KB per instrument, so
  that one instrument fits into the default partition scheme together with the
  program; smaller versions (~250 KB, ~150 KB) for several instruments
- Header only, the core does not depend on Arduino (testable on the desktop)
- Output via arduino-audio-tools, input via arduino-midi
- Max 10 notes at the same time for a piano (one per finger)
- Different kinds of instruments: decaying (piano), one shot (drums) and
  sustained (saxophone)
- Optionally load the samples at runtime from any Stream (e.g. into PSRAM)

## Approach: compressed multisamples

Physical modelling and other synthesis methods were considered, but recorded
samples give the most realistic result for acoustic instruments. The challenge is the size:
a good piano recording has many gigabytes, a simple one about 40 MB. The data
is reduced offline by a converter, so that the player on the microcontroller
stays simple and fast.

```
 SFZ instrument          tools/sfz2header.py            src/samples/X.h
 (wav/flac, 40 MB+) ───────────────────────────────▶   (C++ header, ~700 KB)
                         mono, resample, trim,
                         loop + decay, IMA ADPCM,
                         fit into byte budget
```

| Reduction | Factor |
|---|---|
| mono, 32 kHz instead of stereo 44.1/48 kHz, 24 bit | ~4-6 |
| attack + short sustain loop instead of the full decay (5-10 s) | ~5-15 |
| 4 bit IMA ADPCM instead of 16 bit PCM | 4 |
| fewer velocity layers / round robins (optional) | 1-30 |

### Zones, loops and decay

Each zone (one recorded note and velocity layer) keeps the attack, which is
the most characteristic part of the sound, and a short loop in the sustain:

```
 amplitude
   │╲
   │ ╲  attack + early decay            loop: decay removed,
   │  ╲___                              re-applied by the player
   │      ‾‾‾───___ ┌──────────────┐
   │               ‾│‾‾‾‾‾‾‾‾‾‾‾‾‾‾│◀─┐ played repeatedly
   │                └──────────────┘  │
   └──────────────────────────────────┴──▶ time
                 loopStart       loopEnd
```

- The loop start is chosen with a normalized cross correlation, so that the
  signal before loopEnd matches the signal before loopStart. The end of the
  loop is crossfaded into the signal preceding the loop start; the less
  correlated, the longer and more equal-power the crossfade.
- The natural decay is measured (dB/s), removed from the loop region (constant
  amplitude) and stored as `decayLn`. The player applies it as exponential
  envelope from the loop start on, so the note decays naturally for any
  length. Decay rates are smoothed over neighboring keys to remove outliers.
- Zone lengths are scaled automatically to the byte budget. The converter has
  three modes:

| Mode | Used for | Zone length | Loop |
|---|---|---|---|
| `keyboard` | pianos, mallets | long (bass) to short (treble) | decay re-applied by the player |
| `natural` | drums, percussion | natural length up to a common maximum | short sounds: none (one shot); long sounds: loop with decay |
| `sustain` | wind instruments, organs | same for all zones | constant level while the key is held |

- With `--tune-correct` the pitch of each zone is measured (autocorrelation)
  and a correction in cents is stored in the zone (`tune`).

### IMA ADPCM

4 bits per sample, decoding needs only a few integer operations and no
tables beyond 89 step sizes. The decoder state (predictor, step index) at the
loop start is stored in the zone, so that the decoder can jump back without
decoding from the beginning. The data is a single continuous stream per zone
(no blocks), the low nibble is the first sample.

## Runtime architecture

```
 midi input (serial, BLE, ...)                 audio output (I2S, ...)
        │ arduino-midi                                  ▲ arduino-audio-tools
        ▼                                               │
 SampleSynthesizerMidiAction              SampleSynthesizerStream
        │ noteOn/noteOff/CC/pitchBend                   │ readBytes()
        ▼                                               │
 ┌───────────────────────── SampleSynthesizer ─────────────────────────┐
 │ EventQueue (lock free SPSC) ──▶ processEvents() at render() start   │
 │ ChannelState[16]: instrument, volume, expression, bend, sustain     │
 │ SamplerVoice[TI_MAX_VOICES + TI_FADE_VOICES] ──▶ mix ──▶ limiter    │
 └─────────────────────────────────────────────────────────────────────┘
        │ Instrument: SampleSet + playing parameters
        ▼
 SampleSet ──▶ SampleZone[]: ADPCM data, loop, decay, key/vel range, tune, pitch marks
    ▲                            ▲
    │ compiled in (flash)        │ DynamicSampleSet: loaded from a Stream
 samples/X.h                     data/X.bin  (into PSRAM if available)
```

| Class | Responsibility |
|---|---|
| `SampleZone`, `SampleSet` | sample data, zone lookup by note and velocity |
| `DynamicSampleSet` | loads a sample set from a Stream into PSRAM / RAM |
| `ImaAdpcmDecoder` | streaming decoder with seek to a saved state |
| `SamplerVoice` | one sounding note: decode, interpolate, filter, envelopes, pan |
| `PsolaSource` | PSOLA pitch shifting of a zone (used by the voice with `PitchShift::Psola`) |
| `TonewheelOrgan`, `OrganSource` | synthesized organ: the registration of the instrument and the tone generator of a note (used by the voice instead of the sample data, see [Tonewheel Organ](organ.md)) |
| `Instrument` | how a sample set is played: release, dampers, velocity curve, polyphony, choke, legato, vibrato, pitch shifting |
| `InstrumentBank` | maps midi program numbers to instruments and effects |
| `ChannelEffect`, `GuitarAmp`, `RotarySpeaker`, `TremoloEffect` | effects which process the audio of their midi channels |
| `SampleSynthesizer` | midi handling, voice allocation, mixing, output conversion |
| `EventQueue` | passes midi events from the midi task to the audio task |
| `SampleSynthesizerStream` | audio-tools adapter (`AudioStream`) |
| `SampleSynthesizerMidiAction` | arduino-midi adapter (`MidiAction`) |

### Voice processing

The audio is rendered in blocks of `TI_BLOCK_SIZE` (64) frames. Per block a
voice calculates:

- the pitch:
  `step = zoneRate / outputRate * 2^((note - root + tune/100) / 12) * pitchBend * vibrato`
  (the vibrato lfo is slow, so once per block is enough)
- the amplitude at the start and the end of the block: velocity gain, channel
  gain (CC7 x CC11 x CC2, smoothed), attack, decay and release envelopes

Per output sample a voice:

1. interpolates between the decoded samples with a 4 point hermite
   interpolation (pitch shifting and sample rate conversion in one step)
2. applies a one pole lowpass whose cutoff opens with the velocity inside of
   the velocity layer (smooth timbre between layers)
3. multiplies with the amplitude, which is interpolated linearly inside of the
   block
4. adds the result with constant power panning (stereo spread by key) to the
   mix buffer

A voice ends when the envelope is below -80 dB or a one shot sample has been
played completely. Everything is `float`, which is fast on the ESP32 FPU. The
mix is converted to 16 bit with a soft limiter. See
[Performance](performance.md) for the measured CPU load.

### Velocity

The recordings of the velocity layers are normalized (same loudness), so the
loudness comes from the velocity only: `gain = (velocity / 127) ^ curve`. The
velocity layer and the lowpass only change the timbre.

### Polyphony and voice stealing

- Max `TI_MAX_VOICES` (default 10) notes sound at the same time, and max
  `Instrument::maxNotes` per midi channel. Notes held by the sustain pedal and
  release tails count as well.
- If the limit is reached, the quietest released note, otherwise the oldest
  note, is faded out within 5 ms. This uses one of `TI_FADE_VOICES` (2)
  additional voices, so the new note starts immediately without click.
- A note which is played again while it is still sounding fades out the old
  voice within 30 ms (re-strike).

### Channel effects

Voices of channels without effect are mixed directly. For each effect the
voices of its channels are rendered into a separate block buffer, processed by
the effect (e.g. `GuitarAmp`) and then added to the mix, so that the
distortion of a guitar amplifier does not affect the other instruments. The
effect gets the control changes of its channels. Effects with a tail (echo) are
processed as long as `hasTail()` is true, even without sounding notes.

### Instrument behavior

| | Piano | Drums | Saxophone | Guitar |
|---|---|---|---|---|
| note off | release (time by key) unless sustain pedal | ignored (one shot) | release (0.12 s) | release |
| keys without damper | >= F6 (89) | all | - | - |
| key mapping | closest zone, transposed | exact key only, no transposition | closest zone, transposed | closest zone, transposed |
| choke groups | - | closed hi-hat silences open hi-hat (sfz `group` / `off_by`) | - | - |
| polyphony | 10 | 8 | 1 (monophonic) | 6 (strings) |
| stereo | spread by key | center | center | amp: mono, chorus: stereo |
| special | - | - | legato, vibrato | amp effect, palm mute (extra decay + lowpass) |

### Program changes

An `InstrumentBank` maps program numbers to an instrument and an optional
effect. A program change (queued like the other events) replaces the
instrument and the effect of the channel; on the drum channel the drum kit of
the bank is selected for any program (General MIDI). Sounding notes keep their
samples; if the effect changes, they fade out within 50 ms.

### Pitch shifting with PSOLA

With `PitchShift::Psola` the transposition to the played note is done with
TD-PSOLA instead of the resampling, so that the length and the formants of the
recording are kept:

- The converter finds the start of each period (pitch marks): the signal is
  low pass filtered around the fundamental, the first mark is the first peak
  after the onset, the next ones are found by correlation with the previous
  period, so that all marks have the same phase. They are stored as 16 bit
  distances.
- A grain covers the two periods around a mark with a rising and a falling
  window (smoothstep: the windows of neighboring marks add up to 1). The grains
  are placed at the distance of the new period, while the time advances
  normally; the sum of the windows is normalized.
- Because the time is not changed, all grains are close to the current time:
  the zone is decoded sequentially into a ring buffer (`TI_PSOLA_BUFFER`, 4096
  samples) from which the grains are read. The loop is repeated on this time
  line. No decoder states need to be stored.
- The attack before the first mark is played unchanged and crossfaded into the
  grains, so that the onset stays crisp. With a ratio of 1 the output is
  identical to the recording.
- The PSOLA output is at the rate of the zone: the voice converts it to the
  output rate with the interpolation, which also applies pitch bend and
  vibrato.

### Monophonic legato

For monophonic instruments the synthesizer keeps a stack of the held keys per
channel:

- A key pressed while another one is held starts the new note at its loop
  start (the decoder state there is stored in the zone), so the tongued
  attack is skipped, with a fade in of `legatoTime`. The old note fades out
  in the same time.
- Releasing the sounding key returns to the last key which is still held.
- A key pressed when no other key is held starts with the full attack.

### Vibrato

A per voice lfo modulates the pitch. Its depth is the sum of the modulation
wheel (`vibratoDepth` x CC1) and an automatic vibrato (`autoVibratoDepth`),
which starts after `autoVibratoDelay` and fades in within `autoVibratoRise`,
like a player who adds vibrato on long notes. The samples are recorded without
vibrato, because they loop much better.

### Threading

The midi callbacks (e.g. BLE task, RtMidi thread on the desktop) only push
events into a lock free single producer / single consumer queue. The audio
task applies them at the start of the next `render()` call, so the voices are
only touched by one task and no mutex is needed. The latency is one audio
buffer.

### Memory

| | Flash | RAM |
|---|---|---|
| sample data per instrument | ~700 KB full, ~250 KB small (or PSRAM when loaded) | - |
| pitch marks (PSOLA instruments) | 2 bytes per period, included in the sample data budget | - |
| zone table | 44 bytes per zone | - |
| synthesizer (12 voices, queue, channels) | - | ~7 KB |
| PSOLA ring buffer | - | 8 KB per voice which uses PSOLA (PSRAM if available) |
| `GuitarAmp` | - | ~8.5 KB + echo buffer (~33 KB, PSRAM if available) |
| render buffers | - | 2 x `TI_BLOCK_SIZE` floats on the stack |

The sample data is `const` and therefore stays in flash on the ESP32 (memory
mapped via the cache); it is read directly without copying. The data is
defined inside of inline functions (`UprightPianoKW()`), so it exists only once
even if the header is included in several translation units.

`DynamicSampleSet` loads the [binary format](loading.md) from any Stream and
allocates the zones and the data in PSRAM if available.

## Testing

The core is tested on the desktop with CMake (see [testing](testing.md)):
ADPCM round trip and seeking, pitch accuracy, loops without discontinuities,
decay and release, sustain pedal, dampers, polyphony limit, click free voice
stealing, choke groups, legato, vibrato, midi controllers, program changes,
PSOLA pitch shifting, the guitar amplifier, flash budget of the sample sets
(full and small versions) and loading from streams. The CPU load is measured
on the device with the benchmark example.
