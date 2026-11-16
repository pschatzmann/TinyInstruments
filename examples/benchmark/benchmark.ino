/**
 * @file benchmark.ino
 * @brief Measures the CPU load of the synthesizer for 0..16 voices: piano,
 * guitar without and with the GuitarAmp presets. No audio output is needed:
 * the result is printed to the serial monitor.
 *
 * ESP32-S3 (16MB flash, PSRAM): Partition Scheme "16M Flash (3MB APP/9.9MB
 * FATFS)", PSRAM enabled.
 */
#define TI_MAX_VOICES 16
#include "TinyInstruments.h"
#include "samples/ElectricGuitar.h"
#include "samples/TenorSax.h"
#include "samples/UprightPianoKW.h"

const int RATE = 44100;
const int FRAMES = 256;   // frames per block (typical I2S buffer)
const int BLOCKS = 200;   // measured blocks (1.16 s of audio)
int16_t buffer[FRAMES * 2];

/// Renders the blocks and returns the average and max time per block in us
void measure(SampleSynthesizer& synth, float& avg_us, float& max_us) {
  for (int j = 0; j < 20; j++) synth.render(buffer, FRAMES);  // warm up
  uint32_t total = 0, worst = 0;
  for (int j = 0; j < BLOCKS; j++) {
    uint32_t start = micros();
    synth.render(buffer, FRAMES);
    uint32_t t = micros() - start;
    total += t;
    if (t > worst) worst = t;
  }
  avg_us = (float)total / BLOCKS;
  max_us = worst;
}

/// Measures 1..16 notes; effect: optional channel effect (e.g. GuitarAmp)
void benchmark(const char* name, Instrument inst, const int* notes, ChannelEffect* effect) {
  const float realtime_us = 1000000.0f * FRAMES / RATE;
  inst.maxNotes = 16;
  inst.monophonic = false;
  for (int voices : {0, 1, 2, 4, 6, 8, 10, 12, 16}) {
    static SampleSynthesizer synth;  // static: too big for the stack
    synth.setInstrument(0, inst);
    synth.setEffect(0, effect);
    synth.begin(RATE, 16);
    for (int v = 0; v < voices; v++) synth.noteOn(0, notes[v], 100);
    float avg, worst;
    measure(synth, avg, worst);
    Serial.printf("%s;%d;%d;%.0f;%.0f;%.1f;%.1f\n", name, voices, synth.activeVoices(), avg,
                  worst, 100.0f * avg / realtime_us, 100.0f * worst / realtime_us);
  }
}

const int notes[16] = {40, 47, 52, 55, 59, 64, 67, 72, 76, 79, 84, 45, 50, 57, 62, 69};
// odd notes: between the recorded notes of the saxophone (whole tones)
const int sax_notes[16] = {45, 47, 49, 51, 53, 55, 57, 59, 61, 63, 65, 67, 69, 71, 73, 75};

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.printf("\nCPU %d MHz, free heap %u, free PSRAM %u\n", (int)getCpuFrequencyMhz(),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  Serial.printf("block: %d frames = %.0f us real time\n", FRAMES, 1000000.0f * FRAMES / RATE);
  Serial.println("instrument;notes;voices;avg_us;max_us;avg_cpu%;max_cpu%");

  benchmark("UprightPiano", PianoInstrument(UprightPianoKW()), notes, nullptr);
  // saxophone: transposition with resampling and with PSOLA
  Instrument sax = SaxophoneInstrument(TenorSax());
  sax.monophonic = false;  // several voices for the measurement
  sax.pitchShift = PitchShift::Resample;
  benchmark("Sax-Resample", sax, sax_notes, nullptr);
  sax.pitchShift = PitchShift::Psola;
  benchmark("Sax-Psola", sax, sax_notes, nullptr);
  benchmark("Guitar", GuitarInstrument(ElectricGuitar()), notes, nullptr);
  static GuitarAmp clean(GuitarAmp::Clean), crunch(GuitarAmp::Crunch), lead(GuitarAmp::Lead);
  benchmark("Guitar+Clean", GuitarInstrument(ElectricGuitar()), notes, &clean);
  benchmark("Guitar+Crunch", GuitarInstrument(ElectricGuitar()), notes, &crunch);
  benchmark("Guitar+Lead", GuitarInstrument(ElectricGuitar()), notes, &lead);
  Serial.println("done");
}

void loop() { delay(1000); }
