/**
 * @file organ-serial-midi.ino
 * @brief Tonewheel organ (Hammond style) with rotary speaker (Leslie), played
 * via serial midi (31250 baud). The output goes to an I2S DAC.
 *
 * The organ is synthesized: no sample data is needed, so it fits on any
 * ESP32 partition scheme.
 *
 * Midi controllers: CC1 (modulation wheel) rotary speaker fast/slow, CC12-20
 * drawbars 16' ... 1', CC21 percussion on/off, CC22 percussion 3rd/2nd
 * harmonic, CC23 percussion fast/slow, CC24 percussion soft/normal, CC25 key
 * click, CC26 overdrive, CC27 rotor stop, pitch bend.
 */
#include "AudioTools.h"
#include "Midi.h"
#include "TinyInstrumentsAudioTools.h"
#include "TinyInstrumentsMidi.h"

#define MIDI_RX_PIN 21
#define MIDI_TX_PIN 22

SampleSynthesizer synth;
TonewheelOrgan organ("888000000");  // drawbars 16' ... 1'
Instrument instrument = OrganInstrument(organ);
RotarySpeaker leslie(0.2f);  // a little overdrive
SampleSynthesizerStream in(synth);
I2SStream out;
StreamCopy copier(out, in);
SampleSynthesizerMidiAction action(synth);
MidiStreamIn midi_in(Serial2, action);

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
  Serial2.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

  organ.setPercussion(true, true, true);  // 3rd harmonic, fast, normal
  synth.setInstrument(-1, instrument);
  synth.setEffect(-1, &leslie);

  auto cfg = out.defaultConfig(TX_MODE);
  cfg.copyFrom(in.defaultConfig());  // 44100 Hz, stereo, 16 bits
  out.begin(cfg);
  in.begin(cfg);
}

void loop() {
  midi_in.loop();
  copier.copy();
}
