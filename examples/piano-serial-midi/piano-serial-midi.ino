/**
 * @file piano-serial-midi.ino
 * @brief Sampled piano which is played with a midi keyboard connected to a
 * serial port (standard midi: 31250 baud). The output goes to an I2S DAC.
 */
#include "AudioTools.h"
#include "Midi.h"
#include "TinyInstrumentsAudioTools.h"
#include "TinyInstrumentsMidi.h"
#include "samples/UprightPianoKW.h"

#define MIDI_RX_PIN 21
#define MIDI_TX_PIN 22

SampleSynthesizer synth;
Instrument piano = PianoInstrument(UprightPianoKW());
SampleSynthesizerStream in(synth);
I2SStream out;
StreamCopy copier(out, in);
SampleSynthesizerMidiAction action(synth);
MidiStreamIn midi_in(Serial2, action);

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
  Serial2.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

  synth.setInstrument(-1, piano);  // piano on all midi channels

  auto cfg = out.defaultConfig(TX_MODE);
  cfg.copyFrom(in.defaultConfig());  // 44100 Hz, stereo, 16 bits
  out.begin(cfg);
  in.begin(cfg);
}

void loop() {
  midi_in.loop();
  copier.copy();
}
