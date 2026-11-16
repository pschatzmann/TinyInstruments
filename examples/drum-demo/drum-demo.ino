/**
 * @file drum-demo.ino
 * @brief Plays a groove with the sampled drum kit on an I2S DAC (no midi
 * input needed). The drum samples (700 KB) fit into the default ESP32
 * partition scheme.
 */
#include "AudioTools.h"
#include "TinyInstrumentsAudioTools.h"
#include "samples/DrumKit.h"

const int DRUMS = 9;  // General MIDI percussion channel 10

SampleSynthesizer synth;
Instrument drums = DrumInstrument(DrumKit());
SampleSynthesizerStream in(synth);
I2SStream out;
StreamCopy copier(out, in);

uint32_t next_event = 0;
int step = 0;

void playNextStep() {
  int pos = step % 16;
  if (pos == 0 && (step / 16) % 4 == 0) synth.noteOn(DRUMS, 49, 115);  // crash
  synth.noteOn(DRUMS, pos == 14 ? 46 : 42, pos % 2 ? 70 : 100);    // hi-hat
  if (pos == 0 || pos == 10) synth.noteOn(DRUMS, 36, 120);          // kick
  if (pos == 4 || pos == 12) synth.noteOn(DRUMS, 38, 110);          // snare
  step++;
}

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

  synth.setInstrument(DRUMS, drums);

  auto cfg = out.defaultConfig(TX_MODE);
  cfg.copyFrom(in.defaultConfig());  // 44100 Hz, stereo, 16 bits
  out.begin(cfg);
  in.begin(cfg);
}

void loop() {
  if (millis() >= next_event) {
    playNextStep();
    next_event = millis() + 125;  // 16th notes at 120 bpm
  }
  copier.copy();
}
