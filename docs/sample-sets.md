# Creating your own Sample Sets

Any SFZ instrument can be converted into a sample set header with
`tools/sfz2header.py` (requires python3, numpy and ffmpeg). The converter
supports `#include`, `#define`, the usual region opcodes and evaluates
controller conditions (`locc`/`hicc`) with the default controller values.

## Keyboard instruments (piano, mallets, ...)

```
python3 tools/sfz2header.py MyPiano.sfz --name MyPiano --budget 700000 \
    --rate 32000 -o src/samples/MyPiano.h
```

The zone lengths go from long (bass, `--bass`) to short (treble, `--treble`)
and are scaled to fit into the budget.

The natural decay is measured over 2.5 s and re-applied by the player. A
piano decays faster at the beginning, where the loop is, so the converter
also removes the remaining level trend inside of the loop: otherwise the
level would drop during each pass of the loop and jump back at its start,
which sounds like a vibrato on long notes (`--max-flatten-db` limits the
correction).

## Drums and percussion

```
python3 tools/sfz2header.py Drums.sfz --name MyDrums --mode natural \
    --keys 35-84 --max-layers 2 --choke "46:42,44" --normalize-layers \
    --budget 700000 -o src/samples/MyDrums.h
```

- `--mode natural`: each sound keeps its natural length (until it has decayed
  by `--natural-db`) up to a common maximum which fits into the budget.
  Short sounds are stored completely, long ones (cymbals) get a loop with
  decay
- `--keys`: only use these keys (e.g. the General MIDI percussion keys)
- `--max-layers`: reduce the number of velocity layers
- `--choke`: choke groups, e.g. `46:42,44` = 46 is silenced by 42 and 44
- `--normalize-layers`: the velocity layers of a key get the same level and
  the player creates the dynamics from the velocity
- `--layer-index 1,2`: keep only these velocity layers (0 = softest)
- `--transpose -12`: shift all keys, e.g. if the sfz maps an octave above the
  sounding pitch

Round robin alternatives are skipped (only the first one is used) unless
`--keep-round-robin` is given.

## Sustained instruments (wind instruments, organs, ...)

```
python3 tools/sfz2header.py Sax.sfz --name MySax --mode sustain \
    --loop-fraction 0.65 --normalize-layers --tune-correct --pitch-marks \
    --budget 700000 -o src/samples/MySax.h
```

- `--mode sustain`: all zones get the same length. The loop holds a constant
  level as long as the key is pressed
- `--loop-fraction`: share of the zone used for the loop: long loops keep the
  natural fluctuations of the tone. Use samples without vibrato: the player
  adds the vibrato
- `--pitch-marks`: stores the start of each period, so that the instrument
  can use `PitchShift::Psola` (2 bytes per period, included in the budget)
- `--tune-correct`: measures the pitch of each zone and stores a correction
  in cents. Sustained sounds (`--mode sustain`) are measured in the loop,
  decaying sounds (e.g. plucked strings, whose pitch drifts) 0.1 - 0.5 s after
  the start, which is the pitch that is heard. Corrections of more than 50
  cents are not applied: they are rather wrong measurements (e.g. an octave
  error) than a wrong tuning. The converter warns if a zone seems to be an
  octave above or below its root key, or if the root key is off by a semitone

## Binary files

With `--binary data/X.bin` the converter also writes the sample set as binary
file which can be [loaded at runtime](loading.md). An existing header can be
converted with `tools/header2bin.py src/samples/X.h data/X.bin`, and a binary
file into a header with `tools/bin2header.py data/X.bin src/samples/X.h
--like src/samples/Full.h` (source and license are taken from the `--like`
header).

## Smaller sample sets

- `--single-layer`: keep only the loudest velocity layer of each key
- `--step N`: keep only recordings which are at least N semitones apart
  (`--step 12`: one per octave). The key ranges of the kept zones are extended
  to the middle between their neighbours, so each one is transposed by up to
  +/- N/2 semitones. Not for drums: each key is a different sound
- `--rate 22050`: lower sample rate (less treble)
- `--quality`: measures the result: the playback of each zone (loop, decay,
  ADPCM) is compared with the original recording (third octave band levels and
  envelope in dB). With `--step` also the transposed notes are compared with
  their own recordings. Use `--quality-max-freq 5000` for instruments which
  are played through an amplifier

How many recordings are needed? Created notes differ from their real
recordings by 6 - 9 dB, nearly independent of the distance (e.g. upright
piano: 5.9 dB at +/- 3 semitones, 6.0 dB at +/- 6 semitones), because
neighboring notes of a real instrument differ that much anyway. With fewer
recordings each one can be longer: one recording per octave can be a good use
of the memory, but listen to it: the measurement does not capture everything
(e.g. the attack length with `Resample`).

Example, an octave version of a piano:

```
python3 tools/sfz2header.py MyPiano.sfz --name MyPianoOctave --single-layer \
    --step 12 --rate 22050 --budget 150000 --quality \
    --binary data/MyPianoOctave.bin -o MyPianoOctave.h
```

## Sample libraries without SFZ

`tools/files2sfz.py` creates a simple SFZ file from sample files whose names
contain the note and the velocity layer, e.g. the VCSL library:

```
python3 tools/files2sfz.py samples/*.wav -o grand.sfz \
    --pattern '_(?P<note>[A-G]#?-?\d)_vl(?P<vel>\d)_rr1'
```

Use `--transpose 12` if the note names are an octave too low (e.g. the VCSL
tenor saxophone) and `--every 2` to use only every second recorded note.

## Sources of the included sample sets

All included sample sets can be rebuilt from the original recordings with
[tools/build_samples.sh](../tools/build_samples.sh). The script is the exact
record of how they were made: it downloads the recordings, prepares the SFZ
mappings and runs the converter for the full, small and octave versions:

```
tools/build_samples.sh                         # all instruments
tools/build_samples.sh grand-piano upright-bass
tools/build_samples.sh -w /data/sources sax    # download directory
```

It needs python3 with numpy, ffmpeg, curl, 7z, unzip and git. The downloads
are kept in `~/.cache/tinyinstruments-sources` (or `-w DIR`) and are not
repeated.

| Instrument (headers) | Original recordings | What is used | License |
|---|---|---|---|
| Upright piano (`UprightPianoKW*`) | [FreePats Upright Piano KW](http://freepats.zenvoid.org/Piano/acoustic-grand-piano.html), [UprightPianoKW-SFZ+FLAC-20220221.7z](https://freepats.zenvoid.org/Piano/UprightPianoKW/UprightPianoKW-SFZ+FLAC-20220221.7z) | `UprightPianoKW-20220221.sfz` as it is | CC0 1.0 |
| Grand piano (`GrandPiano*`) | [VCSL](https://github.com/sgossner/VCSL) `Chordophones/Zithers/Grand Piano, Steinway B/NoSus/` | `JHPiano_NoSus_Close_*`: every second recorded note and the highest one, all velocity layers, round robin 1, first 1.5 MB of each file; SFZ by `files2sfz.py` | CC0 1.0 |
| Electric piano (`Wurlitzer*`) | [GregSullivan.E-Pianos](https://github.com/sfzinstruments/GregSullivan.E-Pianos) (git) | `Wurlitzer EP200/Wurlitzer EP200.sfz` as it is | CC-BY 3.0 (Greg Sullivan; SFZ mapping by kinwie) |
| Drum kit (`DrumKit*`) | [Versilian Studios Virtuosity Drums](https://versilian-studios.com/Distro/Virtuosity_Drums_v0.925.zip) v0.925 | `Programs/06-mid-mic.sfz`, General MIDI keys 35-84 | CC0 1.0 |
| Tenor saxophone (`TenorSax*`) | [VCSL](https://github.com/sgossner/VCSL) `Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/` | all files (round robin 1), first 0.9 MB of each; SFZ by `files2sfz.py --transpose 12` (the names are an octave too low), keys 36-96 | CC0 1.0 |
| Clarinet (`Clarinet*`) | [VSCO 2 Community Edition](https://github.com/sgossner/VSCO-2-CE) `Woodwinds/Clarinet/susLong/` | all files (round robin 1), first 1.2 MB of each; SFZ by `files2sfz.py --transpose 12`, keys 48-96 | CC0 1.0 |
| Electric guitar (`ElectricGuitar*`) | [FreePats Electric Guitar FSBS direct](https://github.com/freepats/electric-guitar-FSBS-direct/releases/download/2022-09-11/EGuitarFSBS-direct-SFZ+FLAC-20220911.7z) 2022-09-11 | `EGuitarFSBS-direct bridge 20220911.sfz` (bridge pickup) | CC0 1.0 |
| Electric bass (`ElectricBass*`) | [Karoryfer Growlybass](https://github.com/sfzinstruments/karoryfer.growlybass) | `growlybass_clean.sfz`, `sustain/*_p_rr1.wav` and `*_f_rr1.wav` of 14 notes (db2 - eb5), transposed one octave down, keys 30-80 | CC0 1.0 |
| Upright bass (`UprightBass*`) | [Karoryfer Meatbass](https://github.com/sfzinstruments/karoryfer.meatbass) (1958 Otto Rubner double bass) | `Programs/pizz_basic.sfz` (pizzicato) with the samples it uses; `c4_vl4_rr1` (wrong pitch) replaced by `c4_vl3_rr1` (`pizz_tinyinstruments.sfz`), keys 12-71 | CC0 1.0 |

Converter settings (all with `--license`, see the script for the exact
commands):

| Instrument | Full (700 KB, 32 kHz) | Small (250 KB, 22.05 kHz) | Octave (150 KB, 22.05 kHz, `--step 12`) |
|---|---|---|---|
| Upright piano, grand piano | `--mode keyboard` | + `--single-layer` | + `--single-layer` |
| Electric piano | default | `--single-layer` | `--single-layer` |
| Drum kit | `--mode natural --keys 35-84 --choke 46:42,44 --normalize-layers --max-layers 2` | `--single-layer` instead of `--max-layers 2` | - |
| Saxophone, clarinet | `--mode sustain --loop-fraction 0.65 --normalize-layers --tune-correct --pitch-marks` | + `--single-layer` | + `--single-layer` |
| Electric guitar | `--mode keyboard --bass 2.0 --treble 0.9 --normalize-layers --tune-correct` | + `--single-layer` | + `--single-layer` |
| Electric bass | `--mode keyboard --keys 30-80 --transpose -12 --bass 2.2 --treble 1.2 --normalize-layers --tune-correct --pitch-marks --layer-index 1,2` | `--layer-index 2` | `--layer-index 2` |
| Upright bass | `--keys 12-71 --pitch-marks --tune-correct --max-layers 2` | `--single-layer` instead of `--max-layers 2` | `--single-layer` |

The tonewheel organ is synthesized and has no sample data.

## Tips

- Use a bigger `--budget` (e.g. 2000000) and `--rate 44100` for a better
  quality if you use a partition scheme with more space for the application
- With `--wav-dir` the decoded zones are written as wav files for listening
- Check the license of the samples: the included sets are all CC0
