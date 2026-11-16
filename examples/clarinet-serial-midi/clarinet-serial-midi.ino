/**
 * @file clarinet-serial-midi.ino
 * @brief Clarinet played via serial midi (31250 baud), output to an
 * I2S DAC. The clarinet is monophonic: play legato (overlapping notes) for
 * smooth transitions. Use the modulation wheel (CC1) for vibrato and a
 * breath controller (CC2) or expression pedal (CC11) for dynamics.
 * The samples (700 KB) fit into the default ESP32 partition scheme.
 */
#include "AudioTools.h"
#include "Midi.h"
#include "TinyInstrumentsAudioTools.h"
#include "TinyInstrumentsMidi.h"
#include "samples/Clarinet.h"

#define MIDI_RX_PIN 21
#define MIDI_TX_PIN 22

SampleSynthesizer synth;
Instrument clarinet = ClarinetInstrument(Clarinet());
SampleSynthesizerStream in(synth);
I2SStream out;
StreamCopy copier(out, in);
SampleSynthesizerMidiAction action(synth);
MidiStreamIn midi_in(Serial2, action);

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
  Serial2.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

  synth.setInstrument(-1, clarinet);

  auto cfg = out.defaultConfig(TX_MODE);
  cfg.copyFrom(in.defaultConfig());  // 44100 Hz, stereo, 16 bits
  out.begin(cfg);
  in.begin(cfg);
}

void loop() {
  midi_in.loop();
  copier.copy();
}
