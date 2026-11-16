#pragma once
#include <stdint.h>

#include <atomic>

namespace tinyinstruments {

/// A midi event which is passed from the midi task to the audio task
struct SynthEvent {
  enum Type : uint8_t { NoteOn, NoteOff, ControlChange, PitchBend, ProgramChange };
  Type type;
  uint8_t channel;
  uint8_t a;
  uint8_t b;
  uint16_t value;
};

/**
 * @brief Lock free single producer / single consumer queue: the midi
 * callbacks can be called from a different task (e.g. BLE) than the
 * audio rendering.
 */
template <int N>
class EventQueue {
 public:
  bool push(const SynthEvent& event) {
    uint16_t head = write_pos.load(std::memory_order_relaxed);
    uint16_t next = (head + 1) % N;
    if (next == read_pos.load(std::memory_order_acquire)) return false;  // full
    buffer[head] = event;
    write_pos.store(next, std::memory_order_release);
    return true;
  }

  bool pop(SynthEvent& event) {
    uint16_t tail = read_pos.load(std::memory_order_relaxed);
    if (tail == write_pos.load(std::memory_order_acquire)) return false;  // empty
    event = buffer[tail];
    read_pos.store((tail + 1) % N, std::memory_order_release);
    return true;
  }

  void clear() { read_pos.store(write_pos.load()); }

 protected:
  SynthEvent buffer[N];
  std::atomic<uint16_t> write_pos{0};
  std::atomic<uint16_t> read_pos{0};
};

}  // namespace tinyinstruments
