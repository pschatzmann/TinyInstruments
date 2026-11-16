# Testing

The core does not depend on Arduino, so it can be built and tested on the
desktop with CMake:

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

| Test | Content |
|---|---|
| `test_adpcm` | ADPCM decoding, seeking to a saved decoder state |
| `test_voice` | pitch, loops, decay, release, stereo |
| `test_synthesizer` | midi handling, polyphony, voice stealing, sustain, choke groups, instrument bank and program changes |
| `test_piano` | both pianos: flash budget, all keys, pitch, velocity, release, 10 notes |
| `test_drums` | drum kit: General MIDI keys, one shots, hi-hat choke, groove |
| `test_sax` | saxophone: pitch and octave, sustain, monophonic, legato, vibrato, breath |
| `test_loader` | loading the `data/*.bin` files, chunked streams, errors |
| `test_clarinet` | clarinet: pitch and octave, corrected root key, sustain, legato, no automatic vibrato |
| `test_psola` | PSOLA: lossless at ratio 1, pitch, length kept (amplitude step), continuity, level, loops, one shots, legato start, formants kept with the real clarinet |
| `test_small` | small sample sets: size, binary files, every key compared with the full version, pitch |
| `test_forwarders` | midi forwarding of the desktop player: serial (pseudo terminal, 115200 and 31250 baud), TCP with reconnect, UDP, target parsing (POSIX only) |
| `test_bass` | bass: pitch and octave down to the low E, bass amp keeps the low end, 4 strings, palm mute |
| `test_guitar` | guitar: pitch, all keys, amp distortion per preset, echo tail, effect only on its channel, palm mute, midi control |

To listen to the result, render a demo of all instruments to a wav file:

```
./build/tests/render_demo demo.wav
```

The example recordings of the README (`docs/audio`) are rendered from the
data files with the same phrase for all versions of an instrument:

```
./build/tests/render_examples /tmp/examples
for f in /tmp/examples/*.wav; do
  ffmpeg -i "$f" -codec:a libmp3lame -b:a 96k "docs/audio/$(basename "${f%.wav}").mp3"
done
```

The CPU load on a microcontroller is measured with the
[benchmark](../examples/benchmark) example (see [Performance](performance.md)).
