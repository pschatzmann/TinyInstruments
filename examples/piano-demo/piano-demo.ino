/**
 * @file piano-demo.ino
 * @brief Plays some chords and arpeggios with the sampled piano on an I2S DAC
 * (no midi input needed). The piano samples (700 KB) fit into the default
 * ESP32 partition scheme.
 */
#include "AudioTools.h"
#include "TinyInstrumentsAudioTools.h"
#include "samples/UprightPianoKW.h"  // or: GrandPiano.h, UprightPianoKWSmall.h ...

SampleSynthesizer synth;
Instrument piano = PianoInstrument(UprightPianoKW());  // or: GrandPiano()
SampleSynthesizerStream in(synth);
I2SStream out;
StreamCopy copier(out, in);

// C - Am - F - G arpeggios
const uint8_t chords[4][4] = {{48, 55, 60, 64}, {45, 52, 57, 60}, {41, 48, 53, 57}, {43, 50, 55, 59}};
uint32_t next_event = 0;
int step = 0;

void playNextStep() {
  const uint8_t* chord = chords[(step / 8) % 4];
  int pos = step % 8;
  if (pos == 0) {
    synth.controlChange(0, 64, 0);    // pedal up: release the last chord
    synth.controlChange(0, 64, 127);  // pedal down
    synth.noteOn(0, chord[0] - 12, 90);
  }
  // up and down the chord
  int idx = pos < 4 ? pos : 7 - pos;
  uint8_t note = chord[idx] + 12;
  synth.noteOn(0, note, 60 + (pos == 0 ? 30 : 0));
  synth.noteOff(0, note);  // held by the pedal
  step++;
}

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

  synth.setInstrument(-1, piano);

  auto cfg = out.defaultConfig(TX_MODE);
  cfg.copyFrom(in.defaultConfig());  // 44100 Hz, stereo, 16 bits
  out.begin(cfg);
  in.begin(cfg);
}

void loop() {
  if (millis() >= next_event) {
    playNextStep();
    next_event = millis() + 200;
  }
  copier.copy();
}
