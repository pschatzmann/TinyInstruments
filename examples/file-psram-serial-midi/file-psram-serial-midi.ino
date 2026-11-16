/**
 * @file file-psram-serial-midi.ino
 * @brief Loads an instrument at runtime from a file (LittleFS) into PSRAM and
 * plays it via serial midi (31250 baud) on an I2S DAC.
 *
 * The sample data is not compiled into the program, so the sketch stays small
 * and the instrument can be changed by replacing the file. Upload one of the
 * files of the data folder of the library (e.g. GrandPiano.bin) to LittleFS.
 * Any Stream can be used instead of a File (SD, HTTP client, ...).
 *
 * Needs an ESP32 with PSRAM (e.g. WROVER) and a partition scheme with a file
 * system that can hold the file (~700 KB).
 */
#include <LittleFS.h>

#include "AudioTools.h"
#include "Midi.h"
#include "TinyInstrumentsAudioTools.h"
#include "TinyInstrumentsMidi.h"

#define MIDI_RX_PIN 21
#define MIDI_TX_PIN 22
const char* SAMPLE_FILE = "/GrandPiano.bin";  // or UprightPianoKW.bin, TenorSax.bin, DrumKit.bin

DynamicSampleSet samples;
Instrument instrument;
SampleSynthesizer synth;
SampleSynthesizerStream in(synth);
I2SStream out;
StreamCopy copier(out, in);
SampleSynthesizerMidiAction action(synth);
MidiStreamIn midi_in(Serial2, action);

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
  Serial2.begin(31250, SERIAL_8N1, MIDI_RX_PIN, MIDI_TX_PIN);

  // load the sample set into PSRAM
  if (!LittleFS.begin()) {
    Serial.println("LittleFS failed");
    while (true) delay(1000);
  }
  File file = LittleFS.open(SAMPLE_FILE, "r");
  if (!file || !samples.begin(file)) {
    Serial.printf("Could not load %s: %s\n", SAMPLE_FILE, samples.errorText());
    while (true) delay(1000);
  }
  file.close();
  Serial.printf("Loaded %s: %u bytes, free PSRAM: %u\n", samples.sampleSet().name,
                (unsigned)samples.sampleSet().dataSize, (unsigned)ESP.getFreePsram());

  // select the playing parameters which fit to the sample set
  if (strstr(SAMPLE_FILE, "Drum")) {
    instrument = DrumInstrument(samples.sampleSet());
    synth.setInstrument(9, instrument);
  } else if (strstr(SAMPLE_FILE, "Sax")) {
    instrument = SaxophoneInstrument(samples.sampleSet());
    synth.setInstrument(-1, instrument);
  } else {
    instrument = PianoInstrument(samples.sampleSet());
    synth.setInstrument(-1, instrument);
  }

  auto cfg = out.defaultConfig(TX_MODE);
  cfg.copyFrom(in.defaultConfig());  // 44100 Hz, stereo, 16 bits
  out.begin(cfg);
  in.begin(cfg);
}

void loop() {
  midi_in.loop();
  copier.copy();
}
