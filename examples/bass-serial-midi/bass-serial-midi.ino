/**
 * @file bass-serial-midi.ino
 * @brief Electric bass (direct recordings of a Jazz bass) with a bass
 * amplifier, played via serial midi (31250 baud). The output goes to an I2S DAC.
 *
 * The notes are mapped at the sounding pitch: the low E string is note 28 (E1).
 * Midi controllers: CC20 palm mute, CC16 drive, CC17 tone, CC18 level, CC1
 * vibrato, pitch bend. The samples (700 KB) fit into the default ESP32
 * partition scheme.
 */
#include "AudioTools.h"
#include "Midi.h"
#include "TinyInstrumentsAudioTools.h"
#include "TinyInstrumentsMidi.h"
#include "samples/ElectricBass.h"

#define MIDI_RX_PIN 21
#define MIDI_TX_PIN 22

SampleSynthesizer synth;
Instrument bass = BassInstrument(ElectricBass());
GuitarAmp amp(GuitarAmp::Bass);
SampleSynthesizerStream in(synth);
I2SStream out;
StreamCopy copier(out, in);
SampleSynthesizerMidiAction action(synth);
MidiStreamIn midi_in(Serial2, action);

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
  Serial2.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

  synth.setInstrument(-1, bass);
  synth.setEffect(-1, &amp);

  auto cfg = out.defaultConfig(TX_MODE);
  cfg.copyFrom(in.defaultConfig());  // 44100 Hz, stereo, 16 bits
  out.begin(cfg);
  in.begin(cfg);
}

void loop() {
  midi_in.loop();
  copier.copy();
}
