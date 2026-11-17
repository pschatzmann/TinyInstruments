#pragma once
/**
 * Desktop build of the example sketches (examples/CMakeLists.txt): USB midi
 * keyboards (and the virtual midi port "TinyInstruments") as input of the
 * sketch, without changing the sketch.
 *
 * On the desktop Serial reads stdin. At the start of the program stdin is
 * replaced by a pipe: the midi messages of all midi input ports (RtMidi) are
 * written into it, and the original stdin is forwarded, so that the "Serial"
 * midi input of a sketch also receives the USB midi keyboards. This header is
 * included automatically (-include) before the sketch.
 *
 * Environment variable TI_MIDI_PORT: only the ports which contain this text
 * (default: all ports except "Midi Through").
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "RtMidi.h"

namespace desktop_usb_midi {

class UsbMidiToStdin {
 public:
  UsbMidiToStdin() {
    int fds[2];
    if (pipe(fds) != 0) return;
    int original = dup(STDIN_FILENO);
    if (original < 0 || dup2(fds[0], STDIN_FILENO) < 0) return;
    close(fds[0]);
    write_fd = fds[1];
    // forward the original stdin (e.g. midi which is piped into the program)
    std::thread([this, original]() {
      char buffer[256];
      ssize_t n;
      while ((n = read(original, buffer, sizeof(buffer))) > 0) writeAll(buffer, n);
    }).detach();
    openPorts();
  }

 protected:
  int write_fd = -1;
  std::vector<std::unique_ptr<RtMidiIn>> inputs;

  void writeAll(const void* data, size_t len) {
    // the pipe is shared by the threads: a midi message is written at once
    // (atomic for < PIPE_BUF bytes)
    if (write_fd >= 0 && write(write_fd, data, len) < 0) return;
  }

  static void callback(double, std::vector<unsigned char>* message, void* user) {
    if (message != nullptr && !message->empty())
      static_cast<UsbMidiToStdin*>(user)->writeAll(message->data(), message->size());
  }

  void openPorts() {
    const char* filter = getenv("TI_MIDI_PORT");
    try {
      RtMidiIn probe;
      for (unsigned j = 0; j < probe.getPortCount(); j++) {
        std::string name = probe.getPortName(j);
        bool selected = filter != nullptr ? name.find(filter) != std::string::npos
                                          : name.find("Midi Through") == std::string::npos;
        if (!selected || name.find("TinyInstruments") != std::string::npos) continue;
        open(nullptr, j, name);
      }
      open("TinyInstruments", 0, "virtual port TinyInstruments");
    } catch (RtMidiError& e) {
      fprintf(stderr, "usb midi: %s\n", e.getMessage().c_str());
    }
  }

  void open(const char* virtual_name, unsigned port, const std::string& label) {
    std::unique_ptr<RtMidiIn> in(new RtMidiIn(RtMidi::UNSPECIFIED, "TinyInstruments"));
    if (virtual_name != nullptr) {
      in->openVirtualPort(virtual_name);
    } else {
      in->openPort(port, "TinyInstruments");
    }
    in->ignoreTypes(true, true, true);  // sysex, timing, active sensing
    in->setCallback(&UsbMidiToStdin::callback, this);
    fprintf(stderr, "usb midi input: %s (Serial)\n", label.c_str());
    inputs.push_back(std::move(in));
  }
};

/// started before setup(): the sketch reads the midi with Serial
inline UsbMidiToStdin usb_midi_to_stdin;

}  // namespace desktop_usb_midi
