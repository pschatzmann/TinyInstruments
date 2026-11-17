#pragma once
/**
 * Standard midi file (.mid) for the desktop player: the file is parsed with
 * arduino-midi-fileparser into a list of midi messages with their time in
 * milliseconds.
 *
 * - All tracks are merged (format 0 and 1): the ticks are counted per track.
 * - The time is calculated with the tempo map of the whole file (the Set
 *   Tempo events of all tracks, usually in the first track), so tempo changes
 *   also apply to the other tracks. Without tempo events: 120 BPM.
 * - SMPTE time divisions (frames per second) are supported.
 */
#include <stdint.h>
#include <stdio.h>

#include <algorithm>
#include <string>
#include <vector>

#include "MidiFileParser.h"

namespace tinyinstruments {

class MidiFileSong {
 public:
  /// A midi message (1..3 bytes) with its time
  struct Event {
    double time_ms;
    uint8_t data[3];
    uint8_t len;
  };

  /// Loads a midi file: returns false if it can not be read or parsed
  bool load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (f == nullptr) {
      error = "could not open " + path;
      return false;
    }
    std::vector<uint8_t> data;
    uint8_t buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) data.insert(data.end(), buffer, buffer + n);
    fclose(f);
    return load(data.data(), data.size());
  }

  /// Loads a midi file from memory
  bool load(const uint8_t* data, size_t size) {
    events.clear();
    error.clear();
    std::vector<RawEvent> raw;
    std::vector<TempoChange> tempos;
    midi::MidiFileParser parser;
    parser.begin(false);
    size_t pos = 0;
    int64_t ticks = 0;
    int16_t division = 0;
    bool header = false;
    while (true) {
      // keep the buffer of the parser filled
      while (pos < size && parser.availableForWrite() > 0) {
        size_t len = std::min(size - pos, (size_t)parser.availableForWrite());
        parser.write(data + pos, len);
        pos += len;
      }
      midi::midi_parser_state& state = parser.parse();
      if (state.status == midi::MIDI_PARSER_EOB) {
        // more data is needed (unless the buffer is full: e.g. an event
        // which is bigger than the buffer)
        if (pos < size && parser.availableForWrite() > 0) continue;
        break;
      }
      if (state.status == midi::MIDI_PARSER_ERROR) {
        if (!header || raw.empty()) {
          error = "invalid midi file";
          return false;
        }
        break;  // e.g. a truncated last track: play what we have
      }
      switch (state.status) {
        case midi::MIDI_PARSER_HEADER:
          header = true;
          division = state.header.time_division;
          format = state.header.format;
          track_count = state.header.tracks_count;
          break;
        case midi::MIDI_PARSER_TRACK:
          ticks = 0;  // the ticks are counted per track
          break;
        case midi::MIDI_PARSER_TRACK_MIDI: {
          ticks += state.timeInTicks();
          RawEvent e;
          e.ticks = ticks;
          e.order = raw.size();
          e.len = toBytes(state.midi, e.data);
          if (e.len > 0) raw.push_back(e);
          break;
        }
        case midi::MIDI_PARSER_TRACK_META:
          ticks += state.timeInTicks();
          if (state.meta.type == midi::MIDI_META_SET_TEMPO && state.meta.length == 3 &&
              state.meta.bytes != nullptr) {
            const uint8_t* b = state.meta.bytes;
            uint32_t tempo = (b[0] << 16) | (b[1] << 8) | b[2];
            if (tempo > 0) tempos.push_back(TempoChange{ticks, tempo});
          }
          break;
        case midi::MIDI_PARSER_TRACK_SYSEX:
          ticks += state.timeInTicks();
          break;
        default:
          break;
      }
    }
    if (!header) {
      error = "invalid midi file";
      return false;
    }
    // merge the tracks: by time, in the order of the file at the same time
    std::sort(raw.begin(), raw.end(), [](const RawEvent& a, const RawEvent& b) {
      return a.ticks != b.ticks ? a.ticks < b.ticks : a.order < b.order;
    });
    std::stable_sort(tempos.begin(), tempos.end(),
                     [](const TempoChange& a, const TempoChange& b) { return a.ticks < b.ticks; });
    // ticks -> milliseconds
    events.reserve(raw.size());
    if (division < 0) {
      // SMPTE: frames per second (high byte, negative) x ticks per frame
      int fps = -(int8_t)((uint16_t)division >> 8);
      int ticks_per_frame = division & 0xFF;
      double ms_per_tick = fps > 0 && ticks_per_frame > 0 ? 1000.0 / (fps * ticks_per_frame) : 1.0;
      for (auto& r : raw) events.push_back(toEvent(r, r.ticks * ms_per_tick));
    } else {
      double ticks_per_quarter = division > 0 ? division : 480;
      double us_per_quarter = 500000;  // 120 BPM
      int64_t last_ticks = 0;
      double last_ms = 0;
      size_t t = 0;
      for (auto& r : raw) {
        // apply the tempo changes up to this event
        while (t < tempos.size() && tempos[t].ticks <= r.ticks) {
          last_ms += (tempos[t].ticks - last_ticks) * us_per_quarter / ticks_per_quarter / 1000.0;
          last_ticks = tempos[t].ticks;
          us_per_quarter = tempos[t].us_per_quarter;
          t++;
        }
        double ms = last_ms + (r.ticks - last_ticks) * us_per_quarter / ticks_per_quarter / 1000.0;
        events.push_back(toEvent(r, ms));
      }
    }
    return true;
  }

  /// The midi messages, sorted by time
  std::vector<Event> events;
  /// Description of the problem if load() failed
  std::string error;
  int format = 0;
  int track_count = 0;

  /// The length of the song in milliseconds (time of the last message)
  double durationMs() const { return events.empty() ? 0 : events.back().time_ms; }

  /// The number of notes (note on with a velocity > 0)
  int noteCount() const {
    int count = 0;
    for (auto& e : events)
      if ((e.data[0] & 0xF0) == 0x90 && e.len == 3 && e.data[2] > 0) count++;
    return count;
  }

 protected:
  struct RawEvent {
    int64_t ticks;
    size_t order;
    uint8_t data[3];
    uint8_t len;
  };
  struct TempoChange {
    int64_t ticks;
    uint32_t us_per_quarter;
  };

  static Event toEvent(const RawEvent& r, double ms) {
    Event e;
    e.time_ms = ms;
    e.len = r.len;
    for (int j = 0; j < 3; j++) e.data[j] = r.data[j];
    return e;
  }

  /// The channel message as midi bytes: returns the length (0 = unknown)
  static uint8_t toBytes(const midi::midi_midi_event& m, uint8_t* out) {
    out[0] = (uint8_t)((m.status << 4) | m.channel);
    out[1] = m.param1 & 0x7F;
    out[2] = m.param2 & 0x7F;
    switch (m.status) {
      case midi::MIDI_STATUS_NOTE_OFF:
      case midi::MIDI_STATUS_NOTE_ON:
      case midi::MIDI_STATUS_NOTE_AT:
      case midi::MIDI_STATUS_CC:
      case midi::MIDI_STATUS_PITCH_BEND:
        return 3;
      case midi::MIDI_STATUS_PGM_CHANGE:
      case midi::MIDI_STATUS_CHANNEL_AT:
        return 2;
      default:
        return 0;
    }
  }
};

}  // namespace tinyinstruments
