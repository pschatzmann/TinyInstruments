#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "Config.h"
#include "EventQueue.h"
#include "Instrument.h"
#include "InstrumentBank.h"
#include "ChannelEffect.h"
#include "SamplerVoice.h"

TI_OPTIMIZE_BEGIN

namespace tinyinstruments {

/**
 * @brief Polyphonic, multi timbral sample player which is controlled with
 * midi events. The midi methods (noteOn() etc) only queue the events, so they
 * can be called from a different task than render(). The events are applied
 * at the start of the next render() call.
 *
 * Supported: note on/off, modulation wheel (CC1: vibrato), breath controller
 * (CC2), volume (CC7), expression (CC11), sustain pedal (CC64), all sound off
 * (CC120), reset all controllers (CC121), all notes off (CC123) and pitch
 * bend (+/- 2 semitones by default).
 *
 * Polyphony: max maxNotes (default TI_MAX_VOICES = 10) notes sound at the
 * same time, and max Instrument::maxNotes per channel. If another note is
 * played, the quietest released note (or the oldest note) is faded out within
 * a few milliseconds using one of the additional TI_FADE_VOICES voices.
 * If you combine several instruments (e.g. piano and drums), increase
 * TI_MAX_VOICES.
 */
class SampleSynthesizer {
 public:
  SampleSynthesizer() { resetControllers(); }

  /// Defines the instrument for a midi channel (0..15) or all channels (-1)
  void setInstrument(int channel, const Instrument& instrument) {
    for (int ch = 0; ch < TI_MIDI_CHANNELS; ch++) {
      if (channel < 0 || channel == ch) channels[ch].instrument = &instrument;
    }
  }

  /// Assigns an effect (e.g. GuitarAmp) to a midi channel (0..15) or all
  /// channels (-1); nullptr removes it. Channels which share the same effect
  /// object are processed together. Call it before the audio processing starts
  /// or from the audio task.
  void setEffect(int channel, ChannelEffect* effect) {
    for (int ch = 0; ch < TI_MIDI_CHANNELS; ch++) {
      if (channel < 0 || channel == ch) channels[ch].effect = effect;
    }
  }

  /// Starts the processing with the output sample rate and the max number
  /// of notes which can sound at the same time (<= TI_MAX_VOICES)
  bool begin(uint32_t sampleRate = 44100, int maxNotes = TI_MAX_VOICES) {
    sample_rate = sampleRate;
    max_notes = maxNotes > TI_MAX_VOICES ? TI_MAX_VOICES : maxNotes;
    if (max_notes < 1) max_notes = 1;
    for (int j = 0; j < VOICE_COUNT; j++) voices[j].stop();
    queue.clear();
    resetControllers();
    return sample_rate > 0;
  }

  uint32_t sampleRate() const { return sample_rate; }

  /// Master gain (default 0.5)
  void setVolume(float volume) { master = volume; }
  float volume() const { return master; }

  /// Pitch bend range in semitones (default 2)
  void setPitchBendRange(float semitones) { bend_range = semitones; }

  // ---- midi input: thread safe, events are queued ----

  bool noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (velocity == 0) return noteOff(channel, note);
    return push(SynthEvent::NoteOn, channel, note, velocity);
  }

  bool noteOff(uint8_t channel, uint8_t note, uint8_t velocity = 0) {
    return push(SynthEvent::NoteOff, channel, note, velocity);
  }

  bool controlChange(uint8_t channel, uint8_t controller, uint8_t value) {
    return push(SynthEvent::ControlChange, channel, controller, value);
  }

  /// Pitch bend: 0..16383, center is 8192
  bool pitchBend(uint8_t channel, uint16_t value) {
    return push(SynthEvent::PitchBend, channel, 0, 0, value);
  }

  /// Program change: selects the instrument (and effect) of the channel from
  /// the InstrumentBank. Unknown programs are ignored.
  bool programChange(uint8_t channel, uint8_t program) {
    return push(SynthEvent::ProgramChange, channel, program, 0);
  }

  /// Defines the instruments which are selected with program changes
  void setInstrumentBank(const InstrumentBank* bank) { p_bank = bank; }

  /// Current program of the channel (-1 = not selected by a program change)
  int program(uint8_t channel) const { return channels[channel & 0x0F].program; }

  /// Stops all notes on all channels (with release)
  void allNotesOff() {
    for (int ch = 0; ch < TI_MIDI_CHANNELS; ch++) controlChange(ch, 123, 0);
  }

  // ---- audio output ----

  /// Renders interleaved 16 bit stereo frames
  size_t render(int16_t* out, size_t frames) {
    processEvents();
    float left[TI_BLOCK_SIZE];
    float right[TI_BLOCK_SIZE];
    size_t done = 0;
    while (done < frames) {
      int n = frames - done > TI_BLOCK_SIZE ? TI_BLOCK_SIZE : frames - done;
      renderBlock(left, right, n);
      int16_t* dest = out + done * 2;
      const float gain = master * 32767.0f;
      for (int j = 0; j < n; j++) {
        dest[2 * j] = toInt16(left[j] * gain);
        dest[2 * j + 1] = toInt16(right[j] * gain);
      }
      done += n;
    }
    return frames;
  }

  /// Renders (overwrites) float output in the range -1..1 (not limited)
  void renderFloat(float* left, float* right, size_t frames) {
    processEvents();
    size_t done = 0;
    while (done < frames) {
      int n = frames - done > TI_BLOCK_SIZE ? TI_BLOCK_SIZE : frames - done;
      renderBlock(left + done, right + done, n);
      for (int j = 0; j < n; j++) {
        left[done + j] *= master;
        right[done + j] *= master;
      }
      done += n;
    }
  }

  /// Number of sounding notes (without the notes which are fading out)
  int activeNotes() const {
    int result = 0;
    for (int j = 0; j < VOICE_COUNT; j++)
      if (voices[j].isActive() && !voices[j].fading) result++;
    return result;
  }

  /// Number of sounding voices (including the notes which are fading out)
  int activeVoices() const {
    int result = 0;
    for (int j = 0; j < VOICE_COUNT; j++)
      if (voices[j].isActive()) result++;
    return result;
  }

  /// Max number of notes which can sound at the same time
  int maxNotes() const { return max_notes; }

  /// Provides access to the voices (e.g. for tests): 0 .. voiceCount()-1
  const SamplerVoice& voice(int idx) const { return voices[idx]; }
  static constexpr int voiceCount() { return VOICE_COUNT; }

  /// Fade out time in seconds for stolen notes (default 5 ms)
  void setStealFadeTime(float seconds) { steal_fade = seconds; }

 protected:
  struct ChannelState {
    const Instrument* instrument = nullptr;
    float volume = 100.0f / 127.0f;
    float expression = 1.0f;
    float bend = 1.0f;
    bool sustain = false;
    bool mute = false;  // palm mute
    int program = -1;   // selected with a program change
    ChannelEffect* effect = nullptr;
    float modulation = 0.0f;  // CC1: 0..1
    float breath = 1.0f;      // CC2: 0..1
    // held keys of monophonic instruments (last = sounding)
    uint8_t held_notes[16];
    uint8_t held_velocity[16];
    uint8_t held_count = 0;
  };

  static constexpr int VOICE_COUNT = TI_MAX_VOICES + TI_FADE_VOICES;
  SamplerVoice voices[VOICE_COUNT];
  ChannelState channels[TI_MIDI_CHANNELS];
  EventQueue<TI_EVENT_QUEUE_SIZE> queue;
  uint32_t sample_rate = 44100;
  int max_notes = TI_MAX_VOICES;
  float steal_fade = 0.005f;
  uint32_t note_counter = 0;
  float master = 0.5f;
  float bend_range = 2.0f;
  const InstrumentBank* p_bank = nullptr;

  /// Selects the instrument and effect of the channel: sounding notes
  /// continue with their samples, new notes use the new instrument
  void doProgramChange(uint8_t ch, uint8_t program) {
    if (p_bank == nullptr) return;
    const InstrumentBank::Entry* entry = p_bank->find(ch, program);
    if (entry == nullptr) return;
    ChannelState& cs = channels[ch];
    // monophonic state and palm mute belong to the old instrument
    cs.held_count = 0;
    cs.mute = false;
    // the notes of the old instrument fade out (an effect could change)
    for (int j = 0; j < VOICE_COUNT; j++) {
      SamplerVoice& v = voices[j];
      if (v.isActive() && v.channel == ch && cs.effect != entry->effect) {
        v.release(0.05f * sample_rate);
        v.fading = true;
        v.key_down = false;
      }
    }
    cs.instrument = entry->instrument;
    cs.effect = entry->effect;
    cs.program = program;
  }

  bool push(SynthEvent::Type type, uint8_t channel, uint8_t a, uint8_t b,
            uint16_t value = 0) {
    SynthEvent event{type, (uint8_t)(channel & 0x0F), a, b, value};
    return queue.push(event);
  }

  void processEvents() {
    SynthEvent e;
    while (queue.pop(e)) {
      switch (e.type) {
        case SynthEvent::NoteOn:
          doNoteOn(e.channel, e.a, e.b);
          break;
        case SynthEvent::NoteOff:
          doNoteOff(e.channel, e.a);
          break;
        case SynthEvent::ControlChange:
          doControlChange(e.channel, e.a, e.b);
          break;
        case SynthEvent::PitchBend:
          doPitchBend(e.channel, e.value);
          break;
        case SynthEvent::ProgramChange:
          doProgramChange(e.channel, e.a);
          break;
        default:
          break;
      }
    }
  }

  void renderBlock(float* left, float* right, int n) {
    memset(left, 0, n * sizeof(float));
    memset(right, 0, n * sizeof(float));
    // channels without effect: directly into the mix
    for (int j = 0; j < VOICE_COUNT; j++) {
      if (voices[j].isActive() && channels[voices[j].channel].effect == nullptr)
        voices[j].render(left, right, n);
    }
    // each effect processes the voices of its channels
    for (int ch = 0; ch < TI_MIDI_CHANNELS; ch++) {
      ChannelEffect* effect = channels[ch].effect;
      if (effect == nullptr) continue;
      bool done = false;  // effect shared with a previous channel
      for (int prev = 0; prev < ch; prev++)
        if (channels[prev].effect == effect) done = true;
      if (done) continue;
      float fx_left[TI_BLOCK_SIZE];
      float fx_right[TI_BLOCK_SIZE];
      memset(fx_left, 0, n * sizeof(float));
      memset(fx_right, 0, n * sizeof(float));
      bool active = effect->hasTail();
      for (int j = 0; j < VOICE_COUNT; j++) {
        SamplerVoice& v = voices[j];
        if (v.isActive() && channels[v.channel].effect == effect) {
          v.render(fx_left, fx_right, n);
          active = true;
        }
      }
      if (!active) continue;
      effect->setup(sample_rate);
      effect->process(fx_left, fx_right, n);
      for (int j = 0; j < n; j++) {
        left[j] += fx_left[j];
        right[j] += fx_right[j];
      }
    }
  }

  /// Soft limiter for values scaled to 16 bit: linear up to 90% of the full
  /// scale, then smooth saturation
  static inline int16_t toInt16(float x) {
    const float knee = 0.9f * 32767.0f;
    if (x > knee || x < -knee) {
      float a = fabsf(x);
      float over = (a - knee) * (10.0f / 32767.0f);
      a = knee + 0.1f * 32767.0f * over / (1.0f + over);
      x = x < 0 ? -a : a;
    }
    return (int16_t)x;
  }

  void doNoteOn(uint8_t ch, uint8_t note, uint8_t velocity) {
    ChannelState& cs = channels[ch];
    const Instrument* inst = cs.instrument;
    if (inst == nullptr || inst->samples == nullptr) return;
    if (inst->monophonic) {
      bool legato = inst->legato && cs.held_count > 0;
      removeHeld(cs, note);
      if (cs.held_count < 16) {
        cs.held_notes[cs.held_count] = note;
        cs.held_velocity[cs.held_count] = velocity;
        cs.held_count++;
      }
      playMono(ch, note, velocity, legato);
      return;
    }
    startNote(ch, note, velocity, false);
  }

  /// Monophonic: fades out the sounding note and starts the new one
  void playMono(uint8_t ch, uint8_t note, uint8_t velocity, bool legato) {
    const Instrument* inst = channels[ch].instrument;
    float fade = (legato ? inst->legatoTime : inst->restrikeTime) * sample_rate;
    for (int j = 0; j < VOICE_COUNT; j++) {
      SamplerVoice& v = voices[j];
      if (v.isActive() && !v.fading && v.channel == ch) {
        v.release(fade);
        v.key_down = false;
        v.sustained = false;
        v.fading = true;
      }
    }
    startNote(ch, note, velocity, legato);
  }

  void removeHeld(ChannelState& cs, uint8_t note) {
    int out = 0;
    for (int j = 0; j < cs.held_count; j++) {
      if (cs.held_notes[j] != note) {
        cs.held_notes[out] = cs.held_notes[j];
        cs.held_velocity[out] = cs.held_velocity[j];
        out++;
      }
    }
    cs.held_count = out;
  }

  /// Starts a voice; legato: without attack (from the loop start) with a fade in
  void startNote(uint8_t ch, uint8_t note, uint8_t velocity, bool legato) {
    ChannelState& cs = channels[ch];
    const Instrument* inst = cs.instrument;
    const SampleZone* zone = inst->samples->findZone(note, velocity, inst->exactKeys);
    if (zone == nullptr) return;

    // choke group: e.g. a closed hi-hat silences an open one
    if (zone->group != 0) {
      for (int j = 0; j < VOICE_COUNT; j++) {
        SamplerVoice& v = voices[j];
        if (v.isActive() && v.channel == ch && v.zone()->offBy == zone->group) {
          v.release(inst->chokeTime * sample_rate);
          v.fading = true;
        }
      }
    }

    // a note which is played again: fade out the old one
    for (int j = 0; j < VOICE_COUNT; j++) {
      SamplerVoice& v = voices[j];
      if (v.isActive() && !v.fading && v.channel == ch && v.note == note) {
        v.release(inst->restrikeTime * sample_rate);
        v.key_down = false;
        v.sustained = false;
        v.fading = true;
      }
    }

    SamplerVoice& v = allocateVoice(ch, inst->maxNotes);
    // rate conversion and transposition: the transposition is done by the
    // resampling or with PSOLA (which keeps the length of the sound)
    float rate_ratio = (float)inst->samples->sampleRate / (float)sample_rate;
    float transpose = powf(2.0f, (note - zone->rootKey + zone->tune * 0.01f) / 12.0f);
    bool psola = inst->pitchShift == PitchShift::Psola && PsolaSource::isSupported(zone) &&
                 fabsf(transpose - 1.0f) > 0.0001f;
    float step = psola ? rate_ratio : rate_ratio * transpose;

    // loudness: absolute velocity (the layers only change the timbre)
    float gain = powf(velocity / 127.0f, inst->velocityCurve) * inst->volume;

    // brightness: lowpass which opens with the velocity inside of the layer
    float rel = (float)velocity / (float)zone->hiVel;
    if (rel > 1.0f) rel = 1.0f;
    float cutoff = inst->minCutoff + (20000.0f - inst->minCutoff) * rel * rel;
    bool muted = inst->muteController != 0 && cs.mute;
    if (muted && cutoff > inst->muteCutoff) cutoff = inst->muteCutoff;
    float lowpass = 1.0f;
    if (cutoff < 0.45f * sample_rate) {
      lowpass = 1.0f - expf(-6.2831853f * cutoff / sample_rate);
    }

    // stereo position of the key: constant power panning
    float pos = ((float)note - 64.5f) / 44.0f * inst->stereoWidth;
    if (pos < -1.0f) pos = -1.0f;
    if (pos > 1.0f) pos = 1.0f;
    float angle = (pos + 1.0f) * 0.78539816f;
    float pan_left = cosf(angle) * 1.41421356f;
    float pan_right = sinf(angle) * 1.41421356f;

    v.start(zone, step, gain, lowpass, pan_left, pan_right, legato,
            legato ? inst->legatoTime * sample_rate : 0.0f, psola ? transpose : 0.0f);
    if (muted) {
      // dB per second of the output -> ln per sample of the zone
      v.setExtraDecay(-inst->muteDecay * 0.11512925f / (step * sample_rate));
    }
    v.setPitchFactor(cs.bend);
    v.setChannelGain(channelGain(cs), true);
    v.setVibrato(inst->vibratoRate, inst->vibratoDepth * cs.modulation, inst->autoVibratoDepth,
                 inst->autoVibratoDelay * sample_rate, inst->autoVibratoRise * sample_rate,
                 sample_rate);
    v.channel = ch;
    v.note = note;
    v.velocity = velocity;
    v.key_down = true;
    v.sustained = false;
    v.fading = false;
    v.age = ++note_counter;
  }

  void doNoteOff(uint8_t ch, uint8_t note) {
    ChannelState& cs = channels[ch];
    const Instrument* inst = cs.instrument;
    if (inst != nullptr && inst->monophonic) {
      bool was_sounding = cs.held_count > 0 && cs.held_notes[cs.held_count - 1] == note;
      removeHeld(cs, note);
      // return to the last key which is still held (e.g. trill)
      if (was_sounding && cs.held_count > 0 && !cs.sustain) {
        uint8_t last = cs.held_count - 1;
        playMono(ch, cs.held_notes[last], cs.held_velocity[last], inst->legato);
        return;
      }
    }
    for (int j = 0; j < VOICE_COUNT; j++) {
      SamplerVoice& v = voices[j];
      if (!v.isActive() || !v.key_down || v.channel != ch || v.note != note) continue;
      v.key_down = false;
      if (cs.sustain) {
        v.sustained = true;
      } else {
        releaseVoice(v, cs);
      }
    }
  }

  void releaseVoice(SamplerVoice& v, ChannelState& cs) {
    const Instrument* inst = cs.instrument;
    if (inst == nullptr) {
      v.release(0.1f * sample_rate);
      return;
    }
    // keys without damper keep on sounding
    if (v.note >= inst->undampedKey) return;
    v.release(inst->releaseTime(v.note) * sample_rate);
  }

  void doControlChange(uint8_t ch, uint8_t controller, uint8_t value) {
    ChannelState& cs = channels[ch];
    if (cs.effect != nullptr) cs.effect->controlChange(controller, value);
    if (cs.instrument != nullptr && controller == cs.instrument->muteController &&
        controller != 0) {
      cs.mute = value >= 64;
      return;
    }
    switch (controller) {
      case 1:  // modulation wheel: vibrato depth
        cs.modulation = value / 127.0f;
        for (int j = 0; j < VOICE_COUNT; j++) {
          SamplerVoice& v = voices[j];
          if (v.isActive() && v.channel == ch && cs.instrument != nullptr)
            v.setVibratoDepth(cs.instrument->vibratoDepth * cs.modulation);
        }
        break;
      case 2:  // breath controller
        cs.breath = value / 127.0f;
        break;
      case 7:
        cs.volume = value / 127.0f;
        break;
      case 11:
        cs.expression = value / 127.0f;
        break;
      case 64:
        setSustain(ch, value >= 64);
        break;
      case 120:  // all sound off
        for (int j = 0; j < VOICE_COUNT; j++)
          if (voices[j].channel == ch) voices[j].stop();
        break;
      case 121:  // reset all controllers
        cs.expression = 1.0f;
        cs.breath = 1.0f;
        cs.modulation = 0.0f;
        setSustain(ch, false);
        doPitchBend(ch, 8192);
        break;
      case 123:  // all notes off
        setSustain(ch, false);
        cs.held_count = 0;
        for (int j = 0; j < VOICE_COUNT; j++) {
          SamplerVoice& v = voices[j];
          if (v.isActive() && v.channel == ch) {
            v.key_down = false;
            v.release(0.1f * sample_rate);
          }
        }
        break;
      default:
        break;
    }
    updateChannelGain(ch);
  }

  void setSustain(uint8_t ch, bool on) {
    ChannelState& cs = channels[ch];
    cs.sustain = on;
    if (on) return;
    for (int j = 0; j < VOICE_COUNT; j++) {
      SamplerVoice& v = voices[j];
      if (v.isActive() && v.channel == ch && v.sustained) {
        v.sustained = false;
        releaseVoice(v, cs);
      }
    }
  }

  void doPitchBend(uint8_t ch, uint16_t value) {
    float semitones = ((float)value - 8192.0f) / 8192.0f * bend_range;
    channels[ch].bend = powf(2.0f, semitones / 12.0f);
    for (int j = 0; j < VOICE_COUNT; j++) {
      if (voices[j].isActive() && voices[j].channel == ch)
        voices[j].setPitchFactor(channels[ch].bend);
    }
  }

  static float channelGain(const ChannelState& cs) {
    return cs.volume * cs.expression * cs.breath;
  }

  void updateChannelGain(uint8_t ch) {
    float value = channelGain(channels[ch]);
    for (int j = 0; j < VOICE_COUNT; j++) {
      if (voices[j].isActive() && voices[j].channel == ch)
        voices[j].setChannelGain(value);
    }
  }

  /// Number of sounding notes of a channel (without fading notes)
  int activeNotes(int ch) const {
    int result = 0;
    for (int j = 0; j < VOICE_COUNT; j++)
      if (voices[j].isActive() && !voices[j].fading && voices[j].channel == ch) result++;
    return result;
  }

  /// Fades out the quietest released note (or the oldest note); ch < 0: any channel
  void stealNote(int ch) {
    int victim = -1;
    float best_level = 2.0f;
    for (int j = 0; j < VOICE_COUNT; j++) {
      const SamplerVoice& v = voices[j];
      if (!v.isActive() || v.fading || (ch >= 0 && v.channel != ch)) continue;
      if (!v.key_down && v.level() < best_level) {
        best_level = v.level();
        victim = j;
      }
    }
    if (victim < 0) {
      for (int j = 0; j < VOICE_COUNT; j++) {
        const SamplerVoice& v = voices[j];
        if (!v.isActive() || v.fading || (ch >= 0 && v.channel != ch)) continue;
        if (victim < 0 || v.age < voices[victim].age) victim = j;
      }
    }
    if (victim >= 0) {
      voices[victim].release(steal_fade * sample_rate);
      voices[victim].fading = true;
    }
  }

  /// Provides a voice for a new note: if the max number of notes of the
  /// instrument or of the synthesizer is reached, the quietest released note
  /// (or the oldest note) is faded out.
  SamplerVoice& allocateVoice(uint8_t ch, int instrumentMaxNotes) {
    if (activeNotes(ch) >= instrumentMaxNotes) {
      stealNote(ch);
    } else if (activeNotes() >= max_notes) {
      stealNote(-1);
    }
    for (int j = 0; j < VOICE_COUNT; j++)
      if (!voices[j].isActive()) return voices[j];
    // all voices busy (only fading ones are left): take the quietest
    int quietest = 0;
    for (int j = 1; j < VOICE_COUNT; j++) {
      bool better = voices[j].fading && (!voices[quietest].fading ||
                                         voices[j].level() < voices[quietest].level());
      if (better) quietest = j;
    }
    return voices[quietest];
  }

  void resetControllers() {
    for (int ch = 0; ch < TI_MIDI_CHANNELS; ch++) {
      channels[ch].volume = 100.0f / 127.0f;
      channels[ch].expression = 1.0f;
      channels[ch].bend = 1.0f;
      channels[ch].sustain = false;
      channels[ch].modulation = 0.0f;
      channels[ch].breath = 1.0f;
      channels[ch].held_count = 0;
      channels[ch].mute = false;
    }
  }
};

}  // namespace tinyinstruments

TI_OPTIMIZE_END
