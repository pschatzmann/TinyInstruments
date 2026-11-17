# Tonewheel Organ

`TonewheelOrgan` is a Hammond style organ: the central organ sound of blues,
jazz and rock. Unlike the other instruments it is **synthesized**: it needs no
sample data (no flash, no PSRAM), and the registration can be changed while
playing. Together with the `RotarySpeaker` (Leslie) effect:

```cpp
TonewheelOrgan organ("888000000");          // drawbars 16' ... 1'
Instrument instrument = OrganInstrument(organ);
RotarySpeaker leslie;                       // or RotarySpeaker leslie(0.5f): with overdrive
...
synth.setInstrument(-1, instrument);
synth.setEffect(-1, &leslie);
```

See the [organ-serial-midi](../examples/organ-serial-midi) example, or try it
with the [desktop player](desktop.md):

```
tinyinstruments-player --instrument organ --drawbars 888800000 --leslie fast
```

[Listen to an example](https://cdn.jsdelivr.net/gh/pschatzmann/TinyInstruments@main/docs/audio/TonewheelOrgan.mp3)
(blues in C, the rotary speaker changes from slow to fast, a lick with
percussion).

## Drawbars

The 9 drawbars add sine waves (tonewheels) at the pitches of the harmonics,
each with a level of 0 (off) to 8 (3 dB per step):

| Drawbar | 16' | 5 1/3' | 8' | 4' | 2 2/3' | 2' | 1 3/5' | 1 1/3' | 1' |
|---|---|---|---|---|---|---|---|---|---|
| Pitch | sub octave | fifth | key | octave | octave + fifth | 2 octaves | 2 oct. + third | 2 oct. + fifth | 3 octaves |
| CC | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 |

As on the original, the tonewheels are equal tempered (not pure harmonics),
and the tonewheels above 5.9 kHz fold back an octave. A few classic
registrations:

| Registration | Sound |
|---|---|
| `888000000` | the classic jazz and blues sound (default) |
| `888800000` | fuller, rock |
| `888888888` | full organ (rock, e.g. with overdrive) |
| `008400000` | soft, flute like |
| `888000000` + percussion | jazz |

```cpp
organ.setDrawbars("888800000");
organ.setDrawbar(3, 6);  // 4' to level 6
```

## Percussion and key click

```cpp
organ.setPercussion(true, true, true, false);  // on, 3rd harmonic, fast, normal volume
organ.setKeyClick(0.4f);                         // 0 .. 1
```

- The percussion is a decaying 2nd (4') or 3rd (2 2/3') harmonic with a fast
  (0.5 s) or slow (1.5 s) decay. It is single triggered: it only sounds when no
  other key of the channel is held, so it accents the first note of a phrase
  and is silent when playing legato.
- As on the original, the 1' drawbar is silent while the percussion is on, and
  the normal (loud) percussion lowers the drawbars by 3 dB.
- The key click is a short noise burst at the start of each note. The
  tonewheels are always turning, so each note starts with a random phase.

| CC | Function |
|---|---|
| 21 | percussion on (>= 64) / off |
| 22 | percussion harmonic: 3rd (>= 64) / 2nd |
| 23 | percussion decay: fast (>= 64) / slow |
| 24 | percussion volume: soft (>= 64) / normal |
| 25 | key click amount |

## Rotary speaker (Leslie)

The `RotarySpeaker` is a `ChannelEffect` like the `GuitarAmp`: it processes
the channels it is assigned to. The signal is split at 800 Hz:

- treble: a rotating horn, strong Doppler (pitch) and amplitude modulation
- bass: a rotating drum, less modulation

Two microphones at 90 degrees give the stereo image. The speed changes
between slow (chorale, horn 0.8 Hz) and fast (tremolo, horn 6.8 Hz): the
horn reaches the new speed within about a second, the heavier drum takes
several seconds, which gives the characteristic speed change. An optional
overdrive (tube preamplifier) is in front of the rotors.

```cpp
leslie.setFast(true);    // or with the modulation wheel (CC1)
leslie.setDrive(0.5f);   // 0 (clean) .. 1
leslie.setStopped(true); // brake
```

| CC | Function |
|---|---|
| 1 (modulation wheel) | fast (>= 64) / slow |
| 26 | overdrive 0 - 127 |
| 27 | rotor stop (>= 64) / run |

The rotary speaker can also be used for other instruments, e.g. a guitar.

## Playing

`OrganInstrument()` sets the parameters of an organ: not velocity sensitive,
the notes sustain while the key is held and stop within 30 ms, 10 notes, no
stereo spread (the stereo comes from the rotary speaker). Pitch bend, the
sustain pedal and the midi volume / expression (CC7 / CC11, e.g. a swell
pedal) work as with the other instruments.

In the [all-instruments](../examples/all-instruments) example the organ is
selected with the General MIDI programs 16 - 20 (17: with percussion, 18:
rock organ with all drawbars and overdrive), in
[material-gui](../examples/material-gui) as "Organ".

## Performance

A note computes one sine per active drawbar (table with interpolation) instead
of decoding samples. On the desktop, 10 notes need about 1.4 times the CPU of
the piano with 3 drawbars (`888000000`) and about 2.5 times with all 9
drawbars; the rotary speaker adds little. Measure it on your microcontroller
with the [benchmark](../examples/benchmark) example.
