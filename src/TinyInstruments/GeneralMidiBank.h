#pragma once
#include "GuitarAmp.h"
#include "Instrument.h"
#include "InstrumentBank.h"
#include "Organ.h"
#include "RotarySpeaker.h"
#include "SampleSet.h"
#include "SampleSynthesizer.h"
#include "Tremolo.h"

namespace tinyinstruments {

/**
 * @brief All instruments of TinyInstruments selected by General MIDI program
 * changes, e.g. to play standard midi files. The sample sets are provided by
 * the caller (e.g. the small versions on a microcontroller, the full versions
 * on the desktop); the tonewheel organ is synthesized.
 *
 * | Program (General MIDI)          | Instrument                          |
 * |---------------------------------|-------------------------------------|
 * | 0-3, 6-15 pianos, mallets       | piano                               |
 * | 4, 5 electric pianos            | electric piano (Wurlitzer), tremolo |
 * | 16-23 organs                    | tonewheel organ, rotary speaker     |
 * | 24-31 guitars                   | electric guitar, clean amp          |
 * | 32 acoustic bass                | upright bass                        |
 * | 33-39 basses                    | electric bass, bass amp             |
 * | 40-55 strings, choirs           | tonewheel organ (sustained sound)   |
 * | 56-63 brass                     | tenor saxophone                     |
 * | 64-67 saxophones                | tenor saxophone                     |
 * | 68-79 reeds, pipes              | clarinet (+6 dB; piccolo: 1 octave  |
 * |                                 | lower, whistle: 2 octaves lower)    |
 * | 80-103 synthesizer leads, pads  | tonewheel organ                     |
 * | 104-111 ethnic instruments      | electric guitar, clean amp          |
 * | 112-127 sound effects           | silent                              |
 * | channel 10                      | drum kit                            |
 *
 * A sample set which is not provided (nullptr) is replaced: the electric
 * piano and the upright bass by the piano and the electric bass, the other
 * instruments are silent.
 *
 * ```cpp
 * GeneralMidiBank gm;
 * gm.begin(synth, {&GrandPianoSmall(), &WurlitzerSmall(), ...});
 * ```
 * The object must stay alive while the synthesizer uses it (e.g. a global).
 */
class GeneralMidiBank {
 public:
  /// The sample sets of the instruments
  struct SampleSets {
    const SampleSet* piano = nullptr;
    const SampleSet* electricPiano = nullptr;
    const SampleSet* guitar = nullptr;
    const SampleSet* uprightBass = nullptr;
    const SampleSet* bass = nullptr;
    const SampleSet* saxophone = nullptr;
    const SampleSet* clarinet = nullptr;
    const SampleSet* drums = nullptr;
  };

  /// Clarinet volume: it plays the melodies of the flutes (+6 dB)
  float clarinetVolume = 2.0f;

  GeneralMidiBank() = default;
  // the instruments refer to the members (e.g. the organ)
  GeneralMidiBank(const GeneralMidiBank&) = delete;
  GeneralMidiBank& operator=(const GeneralMidiBank&) = delete;

  /// Defines the instruments and assigns the bank to the synthesizer: the
  /// piano plays until the first program change, the drums on channel 10
  void begin(SampleSynthesizer& synth, const SampleSets& sets) {
    setup(sets);
    synth.setInstrumentBank(&bank_);
    synth.setInstrument(-1, piano_);
    synth.setInstrument(9, drums_);
  }

  /// Provides the bank (e.g. to change the mapping)
  InstrumentBank& bank() { return bank_; }

  /// The tonewheel organ (e.g. to change the drawbars)
  TonewheelOrgan& organ() { return organ_; }

 protected:
  TonewheelOrgan organ_{"888000000"};
  Instrument piano_, epiano_, organ_instrument_, guitar_, upright_bass_, bass_, sax_, clarinet_,
      piccolo_, whistle_, drums_, silent_;
  TremoloEffect tremolo_{5.5f, 0.3f};
  RotarySpeaker leslie_;
  GuitarAmp clean_amp_{GuitarAmp::Clean}, bass_amp_{GuitarAmp::Bass};
  InstrumentBank bank_;

  void setup(const SampleSets& s) {
    bank_.clear();
    piano_ = s.piano ? PianoInstrument(*s.piano) : Instrument();
    epiano_ = s.electricPiano ? ElectricPianoInstrument(*s.electricPiano) : piano_;
    organ_instrument_ = OrganInstrument(organ_);
    guitar_ = s.guitar ? GuitarInstrument(*s.guitar) : Instrument();
    bass_ = s.bass ? BassInstrument(*s.bass) : Instrument();
    upright_bass_ = s.uprightBass ? UprightBassInstrument(*s.uprightBass) : bass_;
    sax_ = s.saxophone ? SaxophoneInstrument(*s.saxophone) : Instrument();
    clarinet_ = s.clarinet ? ClarinetInstrument(*s.clarinet) : Instrument();
    clarinet_.volume = clarinetVolume;
    // the piccolo and the whistle sound higher than the range of the clarinet
    piccolo_ = clarinet_;
    piccolo_.transpose = -12;
    whistle_ = clarinet_;
    whistle_.transpose = -24;
    drums_ = s.drums ? DrumInstrument(*s.drums) : Instrument();
    silent_ = Instrument();  // no sample set: the notes are ignored

    bank_.add(0, 15, piano_);
    bank_.add(4, 5, epiano_, s.electricPiano ? &tremolo_ : nullptr);
    bank_.add(16, 23, organ_instrument_, &leslie_);
    bank_.add(24, 31, guitar_, &clean_amp_);
    bank_.add(32, 39, bass_, &bass_amp_);
    bank_.add(32, 32, upright_bass_, s.uprightBass ? nullptr : &bass_amp_);
    bank_.add(40, 55, organ_instrument_, &leslie_);
    bank_.add(56, 67, sax_);
    bank_.add(68, 79, clarinet_);
    bank_.add(72, 72, piccolo_);
    bank_.add(78, 78, whistle_);
    bank_.add(80, 103, organ_instrument_, &leslie_);
    bank_.add(104, 111, guitar_, &clean_amp_);
    bank_.add(112, 127, silent_);
    bank_.setDrumKit(drums_);
  }
};

}  // namespace tinyinstruments
