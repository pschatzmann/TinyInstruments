/**
 * @file piano-ble-midi.ino
 * @brief Sampled piano which is played via BLE midi (ESP32). The output goes
 * to an I2S DAC.
 *
 * BLE needs a lot of flash: select the "Huge APP (3MB No OTA/1MB SPIFFS)"
 * partition scheme.
 */
#include "AudioTools.h"
#include "Midi.h"
#include "TinyInstrumentsAudioTools.h"
#include "TinyInstrumentsMidi.h"
#include "samples/UprightPianoKW.h"

SampleSynthesizer synth;
Instrument piano = PianoInstrument(UprightPianoKW());
SampleSynthesizerStream in(synth);
I2SStream out;
StreamCopy copier(out, in);
SampleSynthesizerMidiAction action(synth);
MidiBleServer ble("TinyPiano", &action);

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);

  synth.setInstrument(-1, piano);  // piano on all midi channels

  auto cfg = out.defaultConfig(TX_MODE);
  cfg.copyFrom(in.defaultConfig());  // 44100 Hz, stereo, 16 bits
  out.begin(cfg);
  in.begin(cfg);
  ble.begin();
}

void loop() {
  copier.copy();
}
