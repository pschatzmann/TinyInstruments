#pragma once
#include "Organ.h"
#include "SampleSet.h"

namespace tinyinstruments {

/// How notes between the recorded notes are created
enum class PitchShift : uint8_t {
  /// play the sample faster or slower: changes the length and the formants
  /// (best for pianos, guitars: crisp attacks)
  Resample,
  /// PSOLA: keeps the length and the formants (best for wind instruments and
  /// basses); needs pitch marks in the sample set, otherwise Resample is used
  Psola
};

/**
 * @brief A sample based instrument: the sample set and the parameters
 * which define how it is played.
 */
struct Instrument {
  Instrument() = default;
  explicit Instrument(const SampleSet& set) : samples(&set) {}
  explicit Instrument(TonewheelOrgan& tonewheels) : organ(&tonewheels) {}

  const SampleSet* samples = nullptr;
  /// synthesized tonewheel organ instead of the samples
  TonewheelOrgan* organ = nullptr;

  /// release time (to -60dB) in seconds for the lowest key (21) ...
  float releaseLow = 0.6f;
  /// ... and the highest damped key
  float releaseHigh = 0.15f;
  /// keys >= this key have no damper: note off is ignored (piano: 89,
  /// drums: 0)
  uint8_t undampedKey = 128;
  /// only play notes which are inside of the key range of a zone (drums);
  /// otherwise the closest zone is transposed
  bool exactKeys = false;
  /// max number of notes of this instrument which sound at the same time
  uint8_t maxNotes = 10;
  /// fade out time (seconds) of a zone which is silenced by its choke group
  float chokeTime = 0.01f;
  /// velocity -> gain: gain = (velocity / 127) ^ velocityCurve
  float velocityCurve = 1.6f;
  /// lowpass cutoff (Hz) for the softest velocity of a layer
  float minCutoff = 1200.0f;
  /// stereo spread of the keys: 0 = mono, 1 = full
  float stereoWidth = 0.5f;
  /// fade out time (seconds) of a sounding note which is played again
  float restrikeTime = 0.03f;
  /// transposition in semitones, e.g. -24: the instrument plays two octaves
  /// lower (added to the transposition of the channel)
  int8_t transpose = 0;
  /// gain of the instrument
  float volume = 1.0f;

  /// pitch shifting method for notes between the recorded notes
  PitchShift pitchShift = PitchShift::Resample;

  /// only one note at the same time (wind instruments)
  bool monophonic = false;
  /// monophonic: a note which is played while another key is held, starts
  /// without attack (from the loop start) and replaces the old note smoothly.
  /// Releasing a note returns to a key which is still held.
  bool legato = false;
  /// crossfade time (seconds) between legato notes
  float legatoTime = 0.04f;

  /// controller which mutes the strings (palm mute, e.g. 20 for the guitar):
  /// notes played while it is >= 64 decay faster and are darker (0 = none)
  uint8_t muteController = 0;
  /// additional decay of muted notes in dB per second
  float muteDecay = 40.0f;
  /// lowpass cutoff (Hz) of muted notes
  float muteCutoff = 900.0f;

  /// vibrato speed in Hz
  float vibratoRate = 5.0f;
  /// vibrato depth (cents) with the modulation wheel (CC1) fully open
  float vibratoDepth = 0.0f;
  /// vibrato depth (cents) which fades in automatically on long notes
  float autoVibratoDepth = 0.0f;
  /// seconds after the note start until the automatic vibrato starts
  float autoVibratoDelay = 0.4f;
  /// seconds until the automatic vibrato has reached its full depth
  float autoVibratoRise = 0.6f;

  /// Release time in seconds for the indicated note
  float releaseTime(uint8_t note) const {
    int hi = undampedKey < 128 ? undampedKey - 1 : 108;
    float t = (float)(note - 21) / (float)(hi - 21);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return releaseLow + (releaseHigh - releaseLow) * t;
  }
};

/// Instrument with the parameters of a drum kit (General MIDI channel 10)
inline Instrument DrumInstrument(const SampleSet& set) {
  Instrument drums(set);
  drums.undampedKey = 0;  // one shot: note off is ignored
  drums.exactKeys = true;
  drums.velocityCurve = 1.4f;
  drums.minCutoff = 2500.0f;
  drums.stereoWidth = 0.0f;
  drums.restrikeTime = 0.08f;
  drums.maxNotes = 8;
  return drums;
}

/// Instrument with the parameters of a saxophone: monophonic, legato, vibrato
/// (automatic and with the modulation wheel), breath controller (CC2)
inline Instrument SaxophoneInstrument(const SampleSet& set) {
  Instrument sax(set);
  sax.monophonic = true;
  sax.legato = true;
  sax.legatoTime = 0.04f;
  sax.maxNotes = 1;
  sax.releaseLow = 0.12f;
  sax.releaseHigh = 0.12f;
  sax.velocityCurve = 1.2f;
  sax.minCutoff = 1500.0f;
  sax.stereoWidth = 0.0f;
  sax.restrikeTime = 0.03f;
  sax.vibratoRate = 5.2f;
  sax.vibratoDepth = 35.0f;
  sax.autoVibratoDepth = 12.0f;
  sax.autoVibratoDelay = 0.45f;
  sax.autoVibratoRise = 0.8f;
  sax.pitchShift = PitchShift::Psola;
  return sax;
}

/// Instrument with the parameters of an electric guitar (use it with a
/// GuitarAmp effect on the same channel): 6 notes (strings), palm mute with
/// CC20, vibrato with the modulation wheel
inline Instrument GuitarInstrument(const SampleSet& set) {
  Instrument guitar(set);
  guitar.maxNotes = 6;
  guitar.releaseLow = 0.15f;
  guitar.releaseHigh = 0.08f;
  guitar.velocityCurve = 1.3f;
  guitar.minCutoff = 1800.0f;
  guitar.stereoWidth = 0.0f;
  guitar.restrikeTime = 0.02f;
  guitar.vibratoRate = 5.5f;
  guitar.vibratoDepth = 30.0f;
  guitar.muteController = 20;
  guitar.muteDecay = 40.0f;
  guitar.muteCutoff = 900.0f;
  return guitar;
}

/// Instrument with the parameters of a clarinet: monophonic, legato, no
/// automatic vibrato (classical style), vibrato with the modulation wheel,
/// breath controller (CC2)
inline Instrument ClarinetInstrument(const SampleSet& set) {
  Instrument clarinet = SaxophoneInstrument(set);
  clarinet.releaseLow = 0.1f;
  clarinet.releaseHigh = 0.1f;
  clarinet.minCutoff = 1800.0f;
  clarinet.vibratoDepth = 20.0f;
  clarinet.autoVibratoDepth = 0.0f;
  return clarinet;
}

/// Instrument with the parameters of an electric bass (optionally with a
/// GuitarAmp(GuitarAmp::Bass) effect): 4 notes (strings), palm mute with CC20
inline Instrument BassInstrument(const SampleSet& set) {
  Instrument bass(set);
  bass.maxNotes = 4;
  bass.releaseLow = 0.12f;
  bass.releaseHigh = 0.08f;
  bass.velocityCurve = 1.3f;
  bass.minCutoff = 800.0f;
  bass.stereoWidth = 0.0f;
  bass.restrikeTime = 0.02f;
  bass.vibratoRate = 5.0f;
  bass.vibratoDepth = 20.0f;
  bass.muteController = 20;
  bass.muteDecay = 30.0f;
  bass.muteCutoff = 500.0f;
  bass.pitchShift = PitchShift::Psola;
  return bass;
}

/// Instrument with the parameters of a tonewheel organ (Hammond style, use it
/// with a RotarySpeaker effect on the same channel): synthesized, no sample
/// data; not velocity sensitive, sustains while the key is held
inline Instrument OrganInstrument(TonewheelOrgan& organ) {
  Instrument result(organ);
  result.maxNotes = 10;
  result.releaseLow = 0.03f;
  result.releaseHigh = 0.03f;
  result.velocityCurve = 0.0f;
  result.minCutoff = 20000.0f;
  result.stereoWidth = 0.0f;
  result.restrikeTime = 0.01f;
  return result;
}

/// Instrument with the parameters of an acoustic upright (double) bass played
/// pizzicato: 4 notes (strings), no amplifier, soft notes are dark, vibrato
/// with the modulation wheel
inline Instrument UprightBassInstrument(const SampleSet& set) {
  Instrument bass(set);
  bass.maxNotes = 4;
  bass.releaseLow = 0.18f;
  bass.releaseHigh = 0.12f;
  bass.velocityCurve = 1.2f;
  bass.minCutoff = 600.0f;
  bass.stereoWidth = 0.0f;
  bass.restrikeTime = 0.02f;
  bass.vibratoRate = 5.0f;
  bass.vibratoDepth = 20.0f;
  bass.pitchShift = PitchShift::Psola;
  return bass;
}

/// Instrument with the parameters of an electric piano (Wurlitzer, use it
/// optionally with a TremoloEffect): all keys have dampers, short release,
/// little stereo spread
inline Instrument ElectricPianoInstrument(const SampleSet& set) {
  Instrument ep(set);
  ep.releaseLow = 0.25f;
  ep.releaseHigh = 0.12f;
  ep.velocityCurve = 1.4f;
  ep.minCutoff = 1500.0f;
  ep.stereoWidth = 0.2f;
  ep.restrikeTime = 0.02f;
  ep.volume = 0.6f;  // the recordings are louder than the other instruments
  return ep;
}

/// Instrument with the parameters of an acoustic piano
inline Instrument PianoInstrument(const SampleSet& set) {
  Instrument piano(set);
  piano.releaseLow = 0.5f;
  piano.releaseHigh = 0.15f;
  piano.undampedKey = 89;  // the highest keys have no damper
  piano.velocityCurve = 1.6f;
  piano.minCutoff = 1200.0f;
  piano.stereoWidth = 0.5f;
  return piano;
}

}  // namespace tinyinstruments
