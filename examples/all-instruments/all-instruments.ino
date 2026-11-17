/**
 * @file all-instruments.ino
 * @brief All instruments of TinyInstruments, selected with midi program
 * changes (General MIDI): the sample sets are loaded from LittleFS into PSRAM.
 *
 * | Program (General MIDI)          | Instrument                     |
 * |---------------------------------|--------------------------------|
 * | 0, 2-7 pianos                   | grand piano                    |
 * | 1 bright piano                  | upright piano                  |
 * | 16, 19, 20 drawbar organs       | tonewheel organ, rotary speaker|
 * | 17 percussive organ             | organ with percussion          |
 * | 18 rock organ                   | organ, all drawbars, overdrive |
 * | 24-28, 31 guitars               | electric guitar, clean amp     |
 * | 29 overdriven guitar            | electric guitar, crunch amp    |
 * | 30 distortion guitar            | electric guitar, lead amp      |
 * | 32-39 basses                    | electric bass, bass amp        |
 * | 64-67 saxophones                | tenor saxophone                |
 * | 68-71 oboe, horn, bassoon, clar.| clarinet                       |
 * | channel 10                      | drum kit                       |
 *
 * Requirements:
 * - ESP32 with 16 MB flash and PSRAM (e.g. ESP32-S3 N16R8): the sample sets
 *   need 4.9 MB of PSRAM (the organ is synthesized). Enable PSRAM in the Tools
 *   menu.
 * - partitions.csv of this folder: 3 MB application, 12.9 MB LittleFS
 * - copy the files of the data folder of the library (*.bin) into the data
 *   folder of this sketch and upload them to LittleFS (e.g. with the
 *   arduino-littlefs-upload plugin of the Arduino IDE)
 *
 * The midi messages are received on the USB serial port (115200 baud), so you
 * can play it with the desktop player:
 *   tinyinstruments-player --send serial:/dev/ttyUSB0 --no-audio
 * For a midi keyboard with a DIN connector use Serial2 at 31250 baud.
 */

// several instruments play at the same time
#define TI_MAX_VOICES 16

#include <LittleFS.h>

#include "AudioTools.h"
#include "Midi.h"
#include "TinyInstrumentsAudioTools.h"
#include "TinyInstrumentsMidi.h"

// the sample sets in PSRAM
DynamicSampleSet upright_samples, grand_samples, drum_samples, sax_samples, clarinet_samples,
    guitar_samples, bass_samples;

// the instruments: playing parameters for the sample sets
Instrument upright, grand, drums, sax, clarinet, guitar, bass;
GuitarAmp clean_amp(GuitarAmp::Clean), crunch_amp(GuitarAmp::Crunch),
    lead_amp(GuitarAmp::Lead), bass_amp(GuitarAmp::Bass);
// the synthesized organs: registration and rotary speaker (CC1: fast/slow)
TonewheelOrgan drawbar_organ("888000000"), percussive_organ("888000000"), rock_organ("888888888");
Instrument drawbar, percussive, rock;
RotarySpeaker leslie(0.1f), rock_leslie(0.6f);
InstrumentBank bank;

SampleSynthesizer synth;
SampleSynthesizerStream in(synth);
I2SStream out;
StreamCopy copier(out, in);
SampleSynthesizerMidiAction action(synth);
MidiStreamIn midi_in(Serial, action);
int last_program[16];

/// Loads a sample set from LittleFS into PSRAM
bool load(DynamicSampleSet& samples, const char* path) {
  File file = LittleFS.open(path, "r");
  bool ok = file && samples.begin(file);
  if (ok) {
    Serial.printf("loaded %s: %u bytes, free PSRAM: %u\n", path,
                  (unsigned)samples.sampleSet().dataSize, (unsigned)ESP.getFreePsram());
  } else {
    Serial.printf("could not load %s: %s\n", path, file ? samples.errorText() : "not found");
  }
  return ok;
}

void setupInstruments() {
  if (load(grand_samples, "/GrandPiano.bin")) {
    grand = PianoInstrument(grand_samples.sampleSet());
    bank.add(0, 7, grand);
    synth.setInstrument(-1, grand);  // default for all channels
  }
  if (load(upright_samples, "/UprightPianoKW.bin")) {
    upright = PianoInstrument(upright_samples.sampleSet());
    bank.add(1, 1, upright);
  }
  if (load(guitar_samples, "/ElectricGuitar.bin")) {
    guitar = GuitarInstrument(guitar_samples.sampleSet());
    bank.add(24, 31, guitar, &clean_amp);
    bank.add(29, 29, guitar, &crunch_amp);
    bank.add(30, 30, guitar, &lead_amp);
  }
  if (load(bass_samples, "/ElectricBass.bin")) {
    bass = BassInstrument(bass_samples.sampleSet());
    bank.add(32, 39, bass, &bass_amp);
  }
  if (load(sax_samples, "/TenorSax.bin")) {
    sax = SaxophoneInstrument(sax_samples.sampleSet());
    bank.add(64, 67, sax);
  }
  if (load(clarinet_samples, "/Clarinet.bin")) {
    clarinet = ClarinetInstrument(clarinet_samples.sampleSet());
    bank.add(68, 71, clarinet);
  }
  if (load(drum_samples, "/DrumKit.bin")) {
    drums = DrumInstrument(drum_samples.sampleSet());
    bank.setDrumKit(drums);
    synth.setInstrument(9, drums);  // General MIDI channel 10
  }
  // organs: no sample data
  percussive_organ.setPercussion(true, true, true);
  drawbar = OrganInstrument(drawbar_organ);
  percussive = OrganInstrument(percussive_organ);
  rock = OrganInstrument(rock_organ);
  bank.add(16, 20, drawbar, &leslie);
  bank.add(17, 17, percussive, &leslie);
  bank.add(18, 18, rock, &rock_leslie);
  synth.setInstrumentBank(&bank);
}

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
  if (!LittleFS.begin()) {
    Serial.println("LittleFS failed: upload the data folder");
    while (true) delay(1000);
  }
  setupInstruments();

  auto cfg = out.defaultConfig(TX_MODE);
  cfg.copyFrom(in.defaultConfig());  // 44100 Hz, stereo, 16 bits
  out.begin(cfg);
  in.begin(cfg);

  for (int ch = 0; ch < 16; ch++) last_program[ch] = -1;
  Serial.println("ready");
}

/// Name of an instrument: the sample set or "organ"
const char* name(const Instrument& inst) {
  if (inst.organ != nullptr) return "organ";
  return inst.samples != nullptr ? inst.samples->name : "-";
}

/// Reports the program changes
void printProgramChanges() {
  for (int ch = 0; ch < 16; ch++) {
    int program = synth.program(ch);
    if (program != last_program[ch]) {
      const InstrumentBank::Entry* entry = bank.find(ch, program);
      Serial.printf("channel %d: program %d = %s\n", ch + 1, program,
                    entry ? name(*entry->instrument) : "-");
      last_program[ch] = program;
    }
  }
}

void loop() {
  midi_in.loop();
  copier.copy();
  printProgramChanges();
}
