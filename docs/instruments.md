# Instruments

An `Instrument` combines a sample set (the recorded sounds) with the
parameters which define how it is played. The included sample sets are all
CC0 (public domain) and need about 700 KB each:

| Instrument | Sample set | Preset | Source | Details |
|---|---|---|---|---|
| Upright piano | `UprightPianoKW()` | `PianoInstrument()` | FreePats Upright Piano KW | [Pianos](piano.md) |
| Grand piano | `GrandPiano()` | `PianoInstrument()` | VCSL Steinway B | [Pianos](piano.md) |
| Drum kit | `DrumKit()` | `DrumInstrument()` | Versilian Virtuosity Drums | [Drum Kit](drums.md) |
| Tenor saxophone | `TenorSax()` | `SaxophoneInstrument()` | VCSL Tenor Saxophone | [Saxophone](saxophone.md) |
| Electric guitar | `ElectricGuitar()` | `GuitarInstrument()` + `GuitarAmp` | FreePats Electric Guitar FSBS | [Electric Guitar](guitar.md) |
| Clarinet | `Clarinet()` | `ClarinetInstrument()` | VSCO 2 Community Edition | [Clarinet](clarinet.md) |
| Electric bass | `ElectricBass()` | `BassInstrument()` + `GuitarAmp(GuitarAmp::Bass)` | Karoryfer Growlybass | [Electric Bass](bass.md) |

## Versions and sizes

Each instrument is available in up to three versions:

- **full** (~700 KB): all recordings and velocity layers, 32 kHz
- **small** (~250 KB): one velocity layer, 22 kHz; soft notes are created from
  the loud recording with the velocity lowpass
- **octave** (~150 KB): one recording per octave, one velocity layer, 22 kHz;
  the other notes are transposed. Only as data file (not for the drum kit)

The full and small versions can be compiled into the program (header) or
[loaded from a file](loading.md); the octave versions only from a file.

```cpp
#include "samples/GrandPianoSmall.h"
Instrument piano = PianoInstrument(GrandPianoSmall());
```

| Instrument | Version | Data file | Header (`src/samples`) | Size | Recorded notes | Velocity layers | Sample rate | Recorded notes¹ | Transposed notes² |
|---|---|---|---|---|---|---|---|---|---|
| Upright piano | full | `data/UprightPianoKW.bin` | `UprightPianoKW.h` | 702 KB | 38 | 2 | 32.00 kHz | 2.9 dB | - |
| Upright piano | small | `data/UprightPianoKWSmall.bin` | `UprightPianoKWSmall.h` | 251 KB | 36 | 1 | 22.05 kHz | 3.8 dB | - |
| Upright piano | octave | `data/UprightPianoKWOctave.bin` | - | 150 KB | 8 | 1 | 22.05 kHz | 2.2 dB | 6.0 dB |
| Grand piano | full | `data/GrandPiano.bin` | `GrandPiano.h` | 702 KB | 22 | 3 | 32.00 kHz | 3.5 dB | - |
| Grand piano | small | `data/GrandPianoSmall.bin` | `GrandPianoSmall.h` | 251 KB | 22 | 1 | 22.05 kHz | 3.2 dB | - |
| Grand piano | octave | `data/GrandPianoOctave.bin` | - | 150 KB | 7 | 1 | 22.05 kHz | 2.6 dB | 6.9 dB |
| Drum kit | full | `data/DrumKit.bin` | `DrumKit.h` | 702 KB | 43 | 2 | 32.00 kHz | 3.4 dB | - |
| Drum kit | small | `data/DrumKitSmall.bin` | `DrumKitSmall.h` | 251 KB | 43 | 1 | 22.05 kHz | 3.8 dB | - |
| Tenor saxophone | full | `data/TenorSax.bin` | `TenorSax.h` | 701 KB | 23 | 2 | 32.00 kHz | 2.7 dB | - |
| Tenor saxophone | small | `data/TenorSaxSmall.bin` | `TenorSaxSmall.h` | 250 KB | 23 | 1 | 22.05 kHz | 4.4 dB | - |
| Tenor saxophone | octave | `data/TenorSaxOctave.bin` | - | 148 KB | 4 | 1 | 22.05 kHz | 3.7 dB | 8.6 dB |
| Clarinet | full | `data/Clarinet.bin` | `Clarinet.h` | 699 KB | 11 | 3 | 32.00 kHz | 3.2 dB | - |
| Clarinet | small | `data/ClarinetSmall.bin` | `ClarinetSmall.h` | 250 KB | 11 | 1 | 22.05 kHz | 4.8 dB | - |
| Clarinet | octave | `data/ClarinetOctave.bin` | - | 150 KB | 4 | 1 | 22.05 kHz | 4.0 dB | 10.7 dB |
| Electric guitar | full | `data/ElectricGuitar.bin` | `ElectricGuitar.h` | 701 KB | 19 | 2 | 32.00 kHz | 1.0 dB | - |
| Electric guitar | small | `data/ElectricGuitarSmall.bin` | `ElectricGuitarSmall.h` | 251 KB | 19 | 1 | 22.05 kHz | 3.0 dB | - |
| Electric guitar | octave | `data/ElectricGuitarOctave.bin` | - | 150 KB | 4 | 1 | 22.05 kHz | 2.3 dB | 6.8 dB |
| Electric bass | full | `data/ElectricBass.bin` | `ElectricBass.h` | 700 KB | 14 | 2 | 32.00 kHz | 0.4 dB | - |
| Electric bass | small | `data/ElectricBassSmall.bin` | `ElectricBassSmall.h` | 250 KB | 14 | 1 | 22.05 kHz | 0.6 dB | - |
| Electric bass | octave | `data/ElectricBassOctave.bin` | - | 150 KB | 4 | 1 | 22.05 kHz | 0.6 dB | 8.8 dB |

¹ Spectral difference of the recorded notes: the playback (loop, decay,
ADPCM) is compared with the original recording, as RMS difference of the third
octave band levels (`tools/sfz2header.py --quality`; guitar and bass up to
5 kHz, which is what the amplifier lets through). Lower is better.

² The same for the notes which are created by transposing the next recording
(with the pitch shifting of the preset), compared with their own original
recordings. It can only be measured if there are more recordings than used, so
only for the octave versions. For comparison: neighboring notes of a real
instrument differ by a similar amount.

The sizes were chosen from a sweep over sample rates, budgets and velocity
layers: below about 250 KB per instrument with all recordings the quality
drops faster. The values are measurements: listen to the versions before you
choose one.

## Kinds of instruments

The presets cover different ways of playing:

| | Piano | Drums | Saxophone, clarinet | Electric guitar, bass |
|---|---|---|---|---|
| sound | decays naturally | one shot, decays naturally | sustained while the key is pressed | decays naturally |
| note off | release (dampers), sustain pedal | ignored | release | release |
| notes | 10 (polyphonic) | 8 | 1 (monophonic, legato) | 6 / 4 (strings) |
| key mapping | closest zone, transposed | exact keys (General MIDI) | closest zone, transposed | closest zone, transposed |
| special | keys without dampers, stereo spread | choke groups (hi-hat) | vibrato, breath controller | amplifier effect, palm mute, vibrato |

## Parameters

The presets return an `Instrument` which can be changed before it is assigned:

```cpp
Instrument piano = PianoInstrument(GrandPiano());
piano.velocityCurve = 1.2f;   // less dynamic
piano.stereoWidth = 0.0f;     // mono
synth.setInstrument(-1, piano);
```

| Parameter | Description |
|---|---|
| `releaseLow`, `releaseHigh` | release time (s) for the lowest and highest key |
| `undampedKey` | keys from here on ignore note off (piano: 89, drums: 0) |
| `exactKeys` | only play keys which have their own sample (drums) |
| `maxNotes` | max notes of this instrument at the same time |
| `velocityCurve` | gain = (velocity / 127) ^ velocityCurve |
| `minCutoff` | lowpass cutoff (Hz) for soft notes: the sound gets brighter with the velocity |
| `stereoWidth` | stereo spread of the keys (0 = mono) |
| `restrikeTime` | fade out (s) of a note which is played again |
| `chokeTime` | fade out (s) of a zone silenced by its choke group |
| `monophonic`, `legato`, `legatoTime` | one note at a time, smooth transitions |
| `muteController`, `muteDecay`, `muteCutoff` | controller for muted notes (palm mute): faster decay, darker |
| `vibratoRate`, `vibratoDepth` | vibrato speed (Hz) and depth (cents) with the modulation wheel |
| `autoVibratoDepth`, `autoVibratoDelay`, `autoVibratoRise` | vibrato which fades in on long notes |
| `pitchShift` | `PitchShift::Resample` or `PitchShift::Psola`: how notes between the recorded notes are created (see below) |
| `volume` | gain of the instrument |

## Pitch shifting

Only some notes are recorded (e.g. every whole tone): the notes in between
are created from the closest recording. This can be configured per instrument:

```cpp
Instrument sax = SaxophoneInstrument(TenorSax());
sax.pitchShift = PitchShift::Resample;  // or PitchShift::Psola
```

| | `PitchShift::Resample` | `PitchShift::Psola` |
|---|---|---|
| Method | plays the recording faster or slower | overlaps periods of the recording at the new distance (TD-PSOLA) |
| Length of the attack and decay | changes with the pitch (e.g. +12% one whole tone higher) | unchanged |
| Timbre (formants) | shifted with the pitch | kept |
| Attack | crisp | the attack before the first period is played unchanged |
| CPU (ESP32-S3) | 2.9% per note | 5.8% per note |
| Flash | - | pitch marks: 2 bytes per period (1.5% - 7%) |
| RAM | - | 8 KB per voice (PSRAM if available), allocated with the first note |
| Best for | piano, guitar, drums | saxophone, clarinet, bass |

The saxophone, clarinet and bass presets use `Psola`; their sample sets
contain the pitch marks. Sample sets without pitch marks always use
`Resample`, also if `Psola` is configured. Pitch bend and vibrato are always
done by resampling (small changes).

## Midi

| Message | Effect |
|---|---|
| note on / off | velocity: loudness, velocity layer, brightness |
| pitch bend | +/- 2 semitones (`setPitchBendRange()`) |
| CC1 modulation wheel | vibrato depth |
| CC2 breath controller | volume of the channel |
| CC7 volume, CC11 expression | volume of the channel (smoothed) |
| CC64 sustain pedal | holds the notes |
| CC120, CC121, CC123 | all sound off, reset controllers, all notes off |
| program change | selects the instrument of the channel from the `InstrumentBank` |

## Program changes

An `InstrumentBank` maps program numbers (e.g. General MIDI) to instruments
and effects, so that a keyboard can switch the instruments:

```cpp
InstrumentBank bank;
bank.add(0, 7, grand);                // General MIDI pianos
bank.add(24, 31, guitar, &clean_amp); // guitars
bank.add(30, 30, guitar, &lead_amp);  // distortion guitar: later entries take precedence
bank.add(32, 39, bass, &bass_amp);
bank.setDrumKit(drums);               // program changes on channel 10 select the drums
synth.setInstrumentBank(&bank);
```

Program changes are queued like the other midi events. Unknown programs are
ignored. If the new program uses another effect, the sounding notes of the
channel fade out within 50 ms. The [all-instruments](../examples/all-instruments)
example maps all instruments to the General MIDI programs.

## Effects

An effect processes the audio of the midi channels it is assigned to, before
it is mixed with the other channels. Channels which share the same effect
object are processed together:

```cpp
GuitarAmp amp(GuitarAmp::Lead);
synth.setEffect(0, &amp);  // only midi channel 1
```

The control changes of the channel are passed to the effect. Own effects are
derived from `ChannelEffect` (`begin()`, `process()`, `controlChange()`,
`hasTail()`). Included: [GuitarAmp](guitar.md).

## Combining instruments

Each midi channel can have its own instrument. All channels share the voices
of the synthesizer (`TI_MAX_VOICES`, default 10), so increase them before
including the library if several instruments play together:

```cpp
#define TI_MAX_VOICES 16
#include "TinyInstrumentsAudioTools.h"
...
synth.setInstrument(-1, piano);  // all channels ...
synth.setInstrument(9, drums);   // ... except channel 10
```

Each sample set needs about 700 KB, so two instruments need the "Huge APP"
partition scheme on an ESP32 with 4 MB flash, or can be
[loaded into PSRAM](loading.md).

Your own instruments can be created with the [converter](sample-sets.md).
