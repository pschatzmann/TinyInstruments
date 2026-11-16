/**
 * @file piano-drums-serial-midi.ino
 * @brief Grand piano on all midi channels and the drum kit on channel 10,
 * played via serial midi (31250 baud). The output goes to an I2S DAC.
 *
 * Both sample sets together need 1.4 MB: select the "Huge APP (3MB No
 * OTA/1MB SPIFFS)" partition scheme.
 */
// piano and drums share the voices: allow more notes at the same time
#define TI_MAX_VOICES 16

#include "AudioTools.h"
#include "Midi.h"
#include "TinyInstrumentsAudioTools.h"
#include "TinyInstrumentsMidi.h"
#include "samples/DrumKit.h"
#include "samples/GrandPiano.h"

#define MIDI_RX_PIN 21
#define MIDI_TX_PIN 22

SampleSynthesizer synth;
Instrument piano = PianoInstrument(GrandPiano());
Instrument drums = DrumInstrument(DrumKit());
SampleSynthesizerStream in(synth);
I2SStream out;
StreamCopy copier(out, in);
SampleSynthesizerMidiAction action(synth);
MidiStreamIn midi_in(Serial2, action);

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
  Serial2.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

  synth.setInstrument(-1, piano);  // piano on all channels ...
  synth.setInstrument(9, drums);   // ... except channel 10

  auto cfg = out.defaultConfig(TX_MODE);
  cfg.copyFrom(in.defaultConfig());  // 44100 Hz, stereo, 16 bits
  out.begin(cfg);
  in.begin(cfg);
}

void loop() {
  midi_in.loop();
  copier.copy();
}
