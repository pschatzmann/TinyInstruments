/**
 * @file piano-usb-midi.ino
 * @brief Sampled piano which is played via USB midi (arduino-midi
 * MidiUsbDevice): the microcontroller is a USB midi device, so a DAW, a midi
 * file player or the desktop player (`tinyinstruments-player --send
 * midi:"TinyPiano" --no-audio`) on the computer can play it. The output goes
 * to an I2S DAC.
 *
 * - ESP32-S2/S3/P4: Tools - USB Mode: "USB-OTG (TinyUSB)". Serial is then
 *   also provided by TinyUSB (USB CDC).
 * - RP2040/RP2350: default "Pico SDK" USB stack (MIDIUSB) or "Adafruit
 *   TinyUSB".
 *
 * For a USB midi keyboard which is plugged into the microcontroller (USB
 * host, e.g. ESP32-S3 with an OTG adapter) replace MidiUsbDevice by
 * MidiUsbHost: `MidiUsbHost usb_midi(&action);`
 *
 * On the desktop (examples/CMakeLists.txt) the first midi device of the
 * computer (ALSA raw midi, e.g. a USB midi keyboard) is used.
 */
#if defined(USE_TINYUSB)
#include <Adafruit_TinyUSB.h>
#elif defined(ARDUINO_ARCH_RP2040)
#include <MIDIUSB.h>
#endif
#include "AudioTools.h"
#include "MidiUsb.h"
#include "TinyInstrumentsAudioTools.h"
#include "TinyInstrumentsMidi.h"
#include "samples/UprightPianoKW.h"

SampleSynthesizer synth;
Instrument piano = PianoInstrument(UprightPianoKW());
SampleSynthesizerStream in(synth);
I2SStream out;
StreamCopy copier(out, in);
SampleSynthesizerMidiAction action(synth);
// global: the USB interface is registered before the USB stack is started
MidiUsbDevice usb_midi("TinyPiano", &action);

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
  usb_midi.begin();

  synth.setInstrument(-1, piano);  // piano on all midi channels

  auto cfg = out.defaultConfig(TX_MODE);
  cfg.copyFrom(in.defaultConfig());  // 44100 Hz, stereo, 16 bits
  out.begin(cfg);
  in.begin(cfg);
}

void loop() {
  usb_midi.loop();
  copier.copy();
}
