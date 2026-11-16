#pragma once
/**
 * Integration with the arduino-midi library: a MidiAction which forwards
 * the midi events to the SampleSynthesizer. It can be used with any midi input
 * of arduino-midi (MidiStreamIn, MidiBleServer, AppleMidiServer ...).
 */
#include "MidiAction.h"
#include "TinyInstruments.h"

namespace tinyinstruments {

/**
 * @brief midi::MidiAction which plays the received midi events with the
 * SampleSynthesizer. The events are queued, so the callbacks can be called from a
 * different task than the audio output.
 */
class SampleSynthesizerMidiAction : public midi::MidiAction {
 public:
  explicit SampleSynthesizerMidiAction(SampleSynthesizer& synthesizer) : p_synth(&synthesizer) {}

  void onNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) override {
    p_synth->noteOn(channel, note, velocity);
  }

  void onNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) override {
    p_synth->noteOff(channel, note, velocity);
  }

  void onControlChange(uint8_t channel, uint8_t controller, uint8_t value) override {
    p_synth->controlChange(channel, controller, value);
  }

  void onPitchBend(uint8_t channel, uint8_t value) override {
    p_synth->pitchBend(channel, (uint16_t)value << 7);
  }

  void onPitchBend14(uint8_t channel, uint16_t value) override {
    p_synth->pitchBend(channel, value);
  }

  void onProgramChange(uint8_t channel, uint8_t program) override {
    p_synth->programChange(channel, program);
  }

 protected:
  SampleSynthesizer* p_synth;
};

}  // namespace tinyinstruments
