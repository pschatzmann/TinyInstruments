/**
 * @file guitar-serial-midi.ino
 * @brief Electric guitar (direct recordings) with a guitar amplifier, played
 * via serial midi (31250 baud). The output goes to an I2S DAC.
 *
 * Midi controllers: CC19 amp preset (0-42 clean, 43-85 crunch, 86-127 lead),
 * CC16 drive, CC17 tone, CC18 level, CC93 chorus, CC91 echo, CC20 palm mute,
 * CC1 vibrato, pitch bend for string bends.
 * The samples (700 KB) fit into the default ESP32 partition scheme.
 */
#include "AudioTools.h"
#include "Midi.h"
#include "TinyInstrumentsAudioTools.h"
#include "TinyInstrumentsMidi.h"
#include "samples/ElectricGuitar.h"

#define MIDI_RX_PIN 21
#define MIDI_TX_PIN 22

SampleSynthesizer synth;
Instrument guitar = GuitarInstrument(ElectricGuitar());
GuitarAmp amp(GuitarAmp::Crunch);
SampleSynthesizerStream in(synth);
I2SStream out;
StreamCopy copier(out, in);
SampleSynthesizerMidiAction action(synth);
MidiStreamIn midi_in(Serial2, action);

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
  Serial2.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

  synth.setInstrument(-1, guitar);
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
