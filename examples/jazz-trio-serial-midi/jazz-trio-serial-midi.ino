/**
 * @file jazz-trio-serial-midi.ino
 * @brief Jazz trio played via serial midi (31250 baud): electric piano
 * (Wurlitzer) with tremolo on midi channel 1, upright bass (pizzicato) on
 * channel 2 and the drum kit on channel 10. The output goes to an I2S DAC.
 *
 * The small versions of the sample sets (3 x 250 KB) fit into the default
 * ESP32 partition scheme.
 *
 * Midi controllers: electric piano CC1 tremolo depth, CC76 tremolo rate,
 * sustain pedal; upright bass CC1 vibrato; pitch bend.
 */
// three instruments share the voices: allow more notes at the same time
#define TI_MAX_VOICES 16

#include "AudioTools.h"
#include "Midi.h"
#include "TinyInstrumentsAudioTools.h"
#include "TinyInstrumentsMidi.h"
#include "samples/DrumKitSmall.h"
#include "samples/UprightBassSmall.h"
#include "samples/WurlitzerSmall.h"

#define MIDI_RX_PIN 21
#define MIDI_TX_PIN 22

SampleSynthesizer synth;
Instrument epiano = ElectricPianoInstrument(WurlitzerSmall());
Instrument bass = UprightBassInstrument(UprightBassSmall());
Instrument drums = DrumInstrument(DrumKitSmall());
Tremolo tremolo(5.5f, 0.3f);  // the built-in "vibrato" of the Wurlitzer
SampleSynthesizerStream in(synth);
I2SStream out;
StreamCopy copier(out, in);
SampleSynthesizerMidiAction action(synth);
MidiStreamIn midi_in(Serial2, action);

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
  Serial2.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

  synth.setInstrument(0, epiano);  // midi channel 1
  synth.setEffect(0, &tremolo);
  synth.setInstrument(1, bass);    // midi channel 2
  synth.setInstrument(9, drums);   // midi channel 10

  auto cfg = out.defaultConfig(TX_MODE);
  cfg.copyFrom(in.defaultConfig());  // 44100 Hz, stereo, 16 bits
  out.begin(cfg);
  in.begin(cfg);
}

void loop() {
  midi_in.loop();
  copier.copy();
}
