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
  in cents (+/- 127). The converter warns if a zone seems to be an octave
  above or below its root key, or if the root key is off by a semitone

## Binary files

With `--binary data/X.bin` the converter also writes the sample set as binary
file which can be [loaded at runtime](loading.md). An existing header can be
converted with `tools/header2bin.py src/samples/X.h data/X.bin`, and a binary
file into a header with `tools/bin2header.py data/X.bin src/samples/X.h
--like src/samples/Full.h` (source and license are taken from the `--like`
header). The octave headers (`src/samples/*Octave.h`) were created this way
from the data files.

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

## Tips

- Use a bigger `--budget` (e.g. 2000000) and `--rate 44100` for a better
  quality if you use a partition scheme with more space for the application
- With `--wav-dir` the decoded zones are written as wav files for listening
- Check the license of the samples: the included sets are all CC0
