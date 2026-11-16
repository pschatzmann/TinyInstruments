// Renders a demo of the instruments to a wav file (default: demo.wav):
// upright piano, grand piano, drum kit, saxophone, electric guitar, bass, clarinet
#include <stdio.h>

#include <vector>

#include "TinyInstruments.h"
#include "samples/Clarinet.h"
#include "samples/DrumKit.h"
#include "samples/ElectricBass.h"
#include "samples/ElectricGuitar.h"
#include "samples/GrandPiano.h"
#include "samples/TenorSax.h"
#include "samples/UprightPianoKW.h"

static void writeWav(const char* path, const std::vector<int16_t>& data, int rate) {
  FILE* f = fopen(path, "wb");
  if (!f) return;
  uint32_t bytes = data.size() * 2;
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
  u32(16); u16(1); u16(2); u32(rate); u32(rate * 4); u16(4); u16(16);
  fwrite("data", 1, 4, f); u32(bytes);
  fwrite(data.data(), 2, data.size(), f);
  fclose(f);
}

int main(int argc, char** argv) {
  const int rate = 44100;
  Instrument upright = PianoInstrument(UprightPianoKW());
  Instrument grand = PianoInstrument(GrandPiano());
  Instrument drums = DrumInstrument(DrumKit());
  Instrument sax = SaxophoneInstrument(TenorSax());
  Instrument guitar = GuitarInstrument(ElectricGuitar());
  GuitarAmp amp(GuitarAmp::Clean);
  Instrument bass = BassInstrument(ElectricBass());
  GuitarAmp bass_amp(GuitarAmp::Bass);
  Instrument clarinet = ClarinetInstrument(Clarinet());
  SampleSynthesizer synth;
  synth.setInstrument(9, drums);  // midi channel 10
  synth.begin(rate);
  std::vector<int16_t> out;

  auto play = [&](float seconds) {
    size_t frames = seconds * rate;
    size_t pos = out.size();
    out.resize(pos + frames * 2);
    synth.render(&out[pos], frames);
  };

  int chords[][4] = {{48, 55, 60, 64}, {45, 52, 57, 60}, {41, 48, 53, 57}, {43, 50, 55, 59}};

  for (const Instrument* piano : {&upright, &grand}) {
    synth.setInstrument(0, *piano);
    // chromatic run over the full keyboard
    for (int note = 21; note <= 108; note += 1) {
      synth.noteOn(0, note, 90);
      play(0.06f);
      synth.noteOff(0, note);
    }
    play(1.0f);
    // chords with pedal: soft and loud
    for (int vel : {50, 110}) {
      for (auto& chord : chords) {
        synth.controlChange(0, 64, 127);
        for (int n : chord) synth.noteOn(0, n, vel);
        play(0.25f);
        for (int n : chord) synth.noteOff(0, n);
        play(0.75f);
        synth.controlChange(0, 64, 0);
      }
    }
    play(0.5f);
    // long notes to hear the loops
    for (int note : {24, 48, 72}) {
      synth.noteOn(0, note, 100);
      play(2.5f);
      synth.noteOff(0, note);
      play(0.5f);
    }
  }

  // drum kit: each sound, then a groove with the grand piano
  for (int note : {36, 38, 40, 37, 42, 44, 46, 41, 43, 45, 47, 48, 50, 49, 57, 55, 51, 53, 59,
                   54, 56, 58, 60, 61, 62, 63, 64, 67, 68, 69, 70, 73, 75, 80, 81, 83, 84}) {
    synth.noteOn(9, note, 110);
    play(0.5f);
  }
  play(1.5f);
  synth.setInstrument(0, grand);
  for (int bar = 0; bar < 8; bar++) {
    auto& chord = chords[bar % 4];
    for (int step = 0; step < 8; step++) {
      if (step == 0) {
        synth.controlChange(0, 64, 0);
        synth.controlChange(0, 64, 127);
        synth.noteOn(0, chord[0] - 12, 90);
        for (int n : chord) synth.noteOn(0, n + 12, 75);
        for (int n : chord) synth.noteOff(0, n + 12);
        synth.noteOff(0, chord[0] - 12);
        if (bar % 4 == 0) synth.noteOn(9, 49, 115);
      }
      synth.noteOn(9, step == 7 && bar % 2 ? 46 : 42, step % 2 ? 70 : 100);
      if (step == 0 || step == 5) synth.noteOn(9, 36, 120);
      if (step == 2 || step == 6) synth.noteOn(9, 38, 105);
      play(0.15f);
    }
  }
  synth.controlChange(0, 64, 0);
  synth.noteOn(9, 49, 120);
  synth.noteOn(9, 36, 120);
  play(3.0f);

  // saxophone: detached notes, legato phrase, long note with vibrato
  synth.setInstrument(1, sax);
  for (int note : {56, 60, 63, 68}) {
    synth.noteOn(1, note, 90);
    play(0.35f);
    synth.noteOff(1, note);
    play(0.1f);
  }
  int phrase[] = {63, 65, 67, 68, 70, 72, 70, 68, 67, 65, 63};
  for (size_t j = 0; j < sizeof(phrase) / sizeof(int); j++) {
    synth.noteOn(1, phrase[j], 95);  // overlapping: legato
    if (j > 0) synth.noteOff(1, phrase[j - 1]);
    play(j + 1 == sizeof(phrase) / sizeof(int) ? 2.5f : 0.22f);
  }
  synth.noteOff(1, 63);
  play(0.4f);
  synth.noteOn(1, 68, 110);
  play(1.0f);
  for (int cc = 0; cc <= 127; cc += 8) {  // modulation wheel: more vibrato
    synth.controlChange(1, 1, cc);
    play(0.08f);
  }
  play(1.0f);
  synth.controlChange(1, 1, 0);
  synth.noteOff(1, 68);
  play(1.0f);

  // electric guitar: clean arpeggios with chorus
  synth.setInstrument(2, guitar);
  synth.setEffect(2, &amp);
  int arpeggio[][6] = {{40, 47, 52, 56, 59, 64}, {45, 52, 57, 61, 64, 69}};
  for (int rep = 0; rep < 2; rep++) {
    for (auto& chord : arpeggio) {
      for (int n : chord) {
        synth.noteOn(2, n, 85);
        play(0.18f);
      }
      play(0.4f);
      for (int n : chord) synth.noteOff(2, n);
    }
  }
  play(1.0f);
  // crunch: power chords with palm muted eighth notes
  amp.setPreset(GuitarAmp::Crunch);
  int roots[] = {40, 40, 43, 45};
  for (int root : roots) {
    synth.controlChange(2, 20, 127);  // palm mute
    for (int j = 0; j < 6; j++) {
      synth.noteOn(2, root, 100);
      synth.noteOn(2, root + 7, 100);
      play(0.14f);
      synth.noteOff(2, root);
      synth.noteOff(2, root + 7);
    }
    synth.controlChange(2, 20, 0);
    synth.noteOn(2, root, 115);
    synth.noteOn(2, root + 7, 115);
    synth.noteOn(2, root + 12, 115);
    play(0.6f);
    synth.noteOff(2, root);
    synth.noteOff(2, root + 7);
    synth.noteOff(2, root + 12);
  }
  play(0.8f);
  // lead: melody with a bend and vibrato, echo
  amp.setPreset(GuitarAmp::Lead);
  int lead[] = {64, 67, 69, 71, 74, 71, 69};
  for (int n : lead) {
    synth.noteOn(2, n, 105);
    play(0.25f);
    synth.noteOff(2, n);
  }
  synth.noteOn(2, 74, 115);
  play(0.3f);
  for (int b = 0; b <= 16; b++) {  // bend up a whole tone
    synth.pitchBend(2, 8192 + b * 511);
    play(0.02f);
  }
  synth.controlChange(2, 1, 100);  // vibrato
  play(1.5f);
  synth.controlChange(2, 1, 0);
  synth.noteOff(2, 74);
  synth.pitchBend(2, 8192);
  play(2.0f);

  // bass: a bass line alone, then with the drums
  synth.setInstrument(3, bass);
  synth.setEffect(3, &bass_amp);
  int line[] = {28, 28, 40, 28, 31, 33, 35, 38, 33, 33, 45, 33, 36, 38, 40, 43};
  for (int pass = 0; pass < 2; pass++) {
    for (int step = 0; step < 16; step++) {
      int note = line[step];
      synth.noteOn(3, note, step % 4 == 0 ? 110 : 90);
      if (pass == 1) {
        synth.noteOn(9, 42, step % 2 ? 70 : 100);
        if (step % 8 == 0 || step % 8 == 3) synth.noteOn(9, 36, 120);
        if (step % 8 == 4) synth.noteOn(9, 38, 110);
      }
      play(0.16f);
      synth.noteOff(3, note);
      play(0.04f);
    }
  }
  synth.noteOn(3, 28, 120);
  synth.noteOn(9, 49, 120);
  synth.noteOn(9, 36, 120);
  play(1.5f);
  synth.noteOff(3, 28);
  play(1.0f);

  // clarinet: legato phrase over the whole range
  synth.setInstrument(4, clarinet);
  int melody[] = {55, 60, 64, 67, 72, 71, 69, 67, 65, 64, 62, 60, 72, 79, 84, 79, 76, 72};
  int count = sizeof(melody) / sizeof(int);
  for (int j = 0; j < count; j++) {
    synth.noteOn(4, melody[j], 80 + (j % 4) * 8);
    if (j > 0) synth.noteOff(4, melody[j - 1]);
    play(j == 11 || j + 1 == count ? 1.2f : 0.3f);
  }
  synth.noteOff(4, melody[count - 1]);
  play(1.5f);

  const char* path = argc > 1 ? argv[1] : "demo.wav";
  writeWav(path, out, rate);
  printf("written %s (%.1f s)\n", path, out.size() / 2.0 / rate);
  return 0;
}
