/**
 * TinyInstruments desktop player: plays the instruments with a (USB) midi
 * keyboard on the sound card of the computer. The midi messages are parsed with
 * arduino-midi and passed to the SampleSynthesizer with the same
 * SampleSynthesizerMidiAction which is used on the microcontrollers.
 *
 *   tinyinstruments-player --list
 *   tinyinstruments-player --instrument grand --drums
 *   tinyinstruments-player --file data/TenorSax.bin --type sax
 *   tinyinstruments-player --instrument guitar --amp lead
 *   tinyinstruments-player --instrument organ --drawbars 888800000 --leslie fast
 *   tinyinstruments-player --virtual --wav out.wav --seconds 10
 *   tinyinstruments-player --send tcp:192.168.1.40 --no-audio
 *
 * The received midi messages can also be forwarded to microcontrollers
 * (--send): serial, tcp and udp (arduino-midi MidiStreamIn, MidiIpServer,
 * MidiUdpServer) and midi output ports (USB midi devices, BLE midi devices
 * paired by the operating system).
 */
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <signal.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "MidiForwarders.h"
#include "MidiParser.h"
#include "RtMidi.h"
#include "TinyInstruments.h"
#include "TinyInstrumentsMidi.h"
#include "samples/Clarinet.h"
#include "samples/DrumKit.h"
#include "samples/ElectricBass.h"
#include "samples/ElectricGuitar.h"
#include "samples/GrandPiano.h"
#include "samples/TenorSax.h"
#include "samples/UprightPianoKW.h"

using namespace tinyinstruments;

static std::atomic<bool> running{true};
static void onSignal(int) { running = false; }

struct Options {
  std::string instrument = "grand";
  std::string file;
  std::string type;
  std::string port;
  std::string wav;
  std::string amp;  // clean, crunch, lead, off (default: crunch for the guitar)
  std::string drawbars = "888000000";  // organ registration
  std::string leslie = "slow";         // organ rotary speaker: slow, fast, off
  std::vector<std::string> send;  // forward targets
  bool audio = true;
  int channel = 0;  // 1..16, 0 = all
  bool drums = false;
  bool list = false;
  bool virtual_port = false;
  bool verbose = false;
  int rate = 44100;
  int buffer = 256;
  float seconds = 0;
  float volume = 0.5f;
};

static void usage() {
  printf(
      "TinyInstruments desktop player: plays the instruments with a midi keyboard\n\n"
      "Usage: tinyinstruments-player [options]\n"
      "  --list                 list the midi input and output ports\n"
      "  --port N|NAME          midi input port number or part of the name (default: all)\n"
      "  --virtual              open a virtual midi input port 'TinyInstruments'\n"
      "  --instrument NAME      upright | grand | drums | sax | clarinet | guitar | bass |\n"
      "                         organ (synthesized tonewheel organ)\n"
      "  --drawbars DIGITS      organ registration, 9 digits 0..8 (default: 888000000)\n"
      "  --leslie MODE          organ rotary speaker: slow | fast | off (default: slow,\n"
      "                         modulation wheel: fast/slow)\n"
      "                         (default: grand)\n"
      "  --file PATH            load the sample set from a file (e.g. data/GrandPiano.bin)\n"
      "  --type TYPE            piano | drums | sax | clarinet | guitar | bass: playing\n"
      "                         parameters for --file\n"
      "                         (default: from the file name)\n"
      "  --amp PRESET           amplifier: clean | crunch | lead | bass | off\n"
      "                         (default: crunch for the guitar, bass for the bass)\n"
      "  --channel N            midi channel 1..16 of the instrument (default: all)\n"
      "  --drums                also play the drum kit on midi channel 10\n"
      "  --volume V             master volume 0..2 (default: 0.5)\n"
      "  --rate N               sample rate (default: 44100)\n"
      "  --buffer N             audio buffer in frames (default: 256)\n"
      "  --wav FILE             write the output to a wav file instead of the sound card\n"
      "  --seconds S            stop after S seconds\n"
      "  --verbose              print the received midi messages\n"
      "  --send TARGET          forward the midi messages to a microcontroller (repeatable):\n"
      "                           serial:PATH[:BAUD]  e.g. serial:/dev/ttyUSB0:115200, serial:COM3\n"
      "                           tcp:HOST[:PORT]     arduino-midi MidiIpServer (default 5008)\n"
      "                           udp:HOST[:PORT]     arduino-midi MidiUdpServer (default 5008)\n"
      "                           midi:NAME           midi output port, e.g. a USB midi device\n"
      "                           ble:NAME            BLE midi device paired by the operating system\n"
      "  --no-audio             do not play the instruments on the desktop (only forward)\n");
}

static bool parseArgs(int argc, char** argv, Options& opt) {
  for (int j = 1; j < argc; j++) {
    std::string a = argv[j];
    auto next = [&]() -> std::string {
      if (j + 1 >= argc) {
        fprintf(stderr, "missing value for %s\n", a.c_str());
        exit(1);
      }
      return argv[++j];
    };
    if (a == "--list") opt.list = true;
    else if (a == "--port") opt.port = next();
    else if (a == "--virtual") opt.virtual_port = true;
    else if (a == "--instrument") opt.instrument = next();
    else if (a == "--drawbars") opt.drawbars = next();
    else if (a == "--leslie") opt.leslie = next();
    else if (a == "--file") opt.file = next();
    else if (a == "--type") opt.type = next();
    else if (a == "--amp") opt.amp = next();
    else if (a == "--channel") opt.channel = atoi(next().c_str());
    else if (a == "--drums") opt.drums = true;
    else if (a == "--volume") opt.volume = atof(next().c_str());
    else if (a == "--rate") opt.rate = atoi(next().c_str());
    else if (a == "--buffer") opt.buffer = atoi(next().c_str());
    else if (a == "--wav") opt.wav = next();
    else if (a == "--seconds") opt.seconds = atof(next().c_str());
    else if (a == "--verbose") opt.verbose = true;
    else if (a == "--send") opt.send.push_back(next());
    else if (a == "--no-audio") opt.audio = false;
    else {
      usage();
      return false;
    }
  }
  return true;
}

/// Passes the raw midi bytes of all input ports to the parser (single
/// producer for the lock free queue of the synthesizer: RtMidi uses one
/// thread per port, so the messages are serialized with a mutex)
struct MidiInput {
  midi::MidiParser parser;
  std::mutex mutex;
  bool verbose = false;
  bool local = true;  // play on the desktop
  std::vector<std::unique_ptr<MidiForwarder>> forwarders;
  std::vector<std::unique_ptr<RtMidiOut>> outputs;

  static void callback(double, std::vector<unsigned char>* msg, void* user) {
    MidiInput* self = (MidiInput*)user;
    if (msg->empty()) return;
    std::lock_guard<std::mutex> lock(self->mutex);
    if (self->verbose) {
      for (unsigned char b : *msg) printf("%02X ", b);
      printf("\n");
    }
    if (self->local) self->parser.parse(msg->data(), (int)msg->size());
    for (auto& f : self->forwarders) f->send(msg->data(), msg->size());
    for (auto& out : self->outputs) {
      try {
        out->sendMessage(msg);
      } catch (RtMidiError& e) {
        fprintf(stderr, "midi output error: %s\n", e.getMessage().c_str());
      }
    }
  }
};

/// Opens the midi output port whose name contains the indicated text
static std::unique_ptr<RtMidiOut> openMidiOutput(const std::string& name) {
  std::unique_ptr<RtMidiOut> out(new RtMidiOut(RtMidi::UNSPECIFIED, "TinyInstruments Out"));
  for (unsigned j = 0; j < out->getPortCount(); j++) {
    std::string port = out->getPortName(j);
    if (port.find(name) != std::string::npos && port.find("TinyInstruments") == std::string::npos) {
      out->openPort(j, "TinyInstruments");
      printf("forwarding to midi output: %s\n", port.c_str());
      return out;
    }
  }
  return nullptr;
}

/// Creates the forwarders for the --send targets
static bool setupForwarders(const Options& opt, MidiInput& input) {
  for (const std::string& spec : opt.send) {
    ForwardTarget target = ForwardTarget::parse(spec);
    if (!target.valid) {
      fprintf(stderr, "invalid --send target: %s\n", spec.c_str());
      return false;
    }
    if (target.type == "midi" || target.type == "ble") {
      std::unique_ptr<RtMidiOut> out = openMidiOutput(target.address);
      if (!out) {
        fprintf(stderr, "midi output '%s' not found (see --list)\n", target.address.c_str());
        if (target.type == "ble")
          fprintf(stderr,
                  "BLE midi devices must be paired by the operating system first: see "
                  "docs/desktop.md\n");
        return false;
      }
      input.outputs.push_back(std::move(out));
      continue;
    }
    std::unique_ptr<MidiForwarder> forwarder = createForwarder(target);
    if (!forwarder->begin()) {
      fprintf(stderr, "could not open %s\n", forwarder->name().c_str());
      return false;
    }
    printf("forwarding to %s\n", forwarder->name().c_str());
    input.forwarders.push_back(std::move(forwarder));
  }
  return true;
}

static void writeWavHeader(FILE* f, int rate, uint32_t data_bytes) {
  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
  fseek(f, 0, SEEK_SET);
  fwrite("RIFF", 1, 4, f);
  u32(36 + data_bytes);
  fwrite("WAVEfmt ", 1, 8, f);
  u32(16);
  u16(1);
  u16(2);
  u32(rate);
  u32(rate * 4);
  u16(4);
  u16(16);
  fwrite("data", 1, 4, f);
  u32(data_bytes);
}

static void audioCallback(ma_device* device, void* output, const void*, ma_uint32 frames) {
  SampleSynthesizer* synth = (SampleSynthesizer*)device->pUserData;
  synth->render((int16_t*)output, frames);
}

int main(int argc, char** argv) {
  Options opt;
  if (!parseArgs(argc, argv, opt)) return 1;

  // midi ports
  std::vector<std::unique_ptr<RtMidiIn>> inputs;
  try {
    RtMidiIn probe;
    unsigned count = probe.getPortCount();
    if (opt.list) {
      printf("midi input ports:\n");
      for (unsigned j = 0; j < count; j++) printf("  %u: %s\n", j, probe.getPortName(j).c_str());
      if (count == 0) printf("  (none)\n");
      RtMidiOut out_probe;
      printf("midi output ports (--send midi:NAME or ble:NAME):\n");
      for (unsigned j = 0; j < out_probe.getPortCount(); j++)
        printf("  %u: %s\n", j, out_probe.getPortName(j).c_str());
      if (out_probe.getPortCount() == 0) printf("  (none)\n");
      return 0;
    }
    for (unsigned j = 0; j < count; j++) {
      std::string name = probe.getPortName(j);
      bool selected = opt.port.empty() || opt.port == std::to_string(j) ||
                      name.find(opt.port) != std::string::npos;
      // do not listen to our own virtual port
      if (!selected || name.find("TinyInstruments") != std::string::npos) continue;
      std::unique_ptr<RtMidiIn> in(new RtMidiIn(RtMidi::UNSPECIFIED, "TinyInstruments"));
      in->openPort(j, "TinyInstruments");
      printf("midi input: %s\n", name.c_str());
      inputs.push_back(std::move(in));
    }
    if (opt.virtual_port) {
      std::unique_ptr<RtMidiIn> in(new RtMidiIn(RtMidi::UNSPECIFIED, "TinyInstruments"));
      in->openVirtualPort("TinyInstruments");
      printf("midi input: virtual port 'TinyInstruments'\n");
      inputs.push_back(std::move(in));
    }
  } catch (RtMidiError& e) {
    fprintf(stderr, "midi error: %s\n", e.getMessage().c_str());
    return 1;
  }
  if (inputs.empty()) printf("no midi input: use --virtual or connect a midi keyboard\n");

  // instruments
  DynamicSampleSet file_set;
  const SampleSet* set = nullptr;
  std::string type = opt.type;
  if (!opt.file.empty()) {
    StdioFileStream file(opt.file.c_str());
    if (!file || !file_set.begin(file)) {
      fprintf(stderr, "could not load %s: %s\n", opt.file.c_str(), file_set.errorText());
      return 1;
    }
    set = &file_set.sampleSet();
    if (type.empty()) {
      type = opt.file.find("Drum") != std::string::npos     ? "drums"
             : opt.file.find("Sax") != std::string::npos    ? "sax"
             : opt.file.find("Clarinet") != std::string::npos ? "clarinet"
             : opt.file.find("Bass") != std::string::npos   ? "bass"
             : opt.file.find("Guitar") != std::string::npos ? "guitar"
                                                             : "piano";
    }
  } else if (opt.instrument == "organ") {
    type = "organ";
  } else if (opt.instrument == "upright") {
    set = &UprightPianoKW();
    type = "piano";
  } else if (opt.instrument == "grand") {
    set = &GrandPiano();
    type = "piano";
  } else if (opt.instrument == "drums") {
    set = &DrumKit();
    type = "drums";
  } else if (opt.instrument == "sax") {
    set = &TenorSax();
    type = "sax";
  } else if (opt.instrument == "guitar") {
    set = &ElectricGuitar();
    type = "guitar";
  } else if (opt.instrument == "bass") {
    set = &ElectricBass();
    type = "bass";
  } else if (opt.instrument == "clarinet") {
    set = &Clarinet();
    type = "clarinet";
  } else {
    fprintf(stderr, "unknown instrument: %s\n", opt.instrument.c_str());
    return 1;
  }

  TonewheelOrgan organ(opt.drawbars.c_str());
  Instrument instrument = type == "organ"    ? OrganInstrument(organ)
                          : type == "drums"    ? DrumInstrument(*set)
                          : type == "sax"    ? SaxophoneInstrument(*set)
                          : type == "guitar" ? GuitarInstrument(*set)
                          : type == "bass"   ? BassInstrument(*set)
                          : type == "clarinet" ? ClarinetInstrument(*set)
                                             : PianoInstrument(*set);
  Instrument drums = DrumInstrument(DrumKit());
  SampleSynthesizer synth;
  int channel = opt.channel > 0 ? opt.channel - 1 : (type == "drums" ? 9 : -1);
  synth.setInstrument(channel, instrument);
  if (opt.drums) synth.setInstrument(9, drums);

  // guitar amplifier (also usable with the other instruments)
  std::string amp_name = opt.amp;
  if (amp_name.empty()) amp_name = type == "guitar" ? "crunch" : (type == "bass" ? "bass" : "off");
  GuitarAmp amp(amp_name == "clean"  ? GuitarAmp::Clean
                : amp_name == "lead" ? GuitarAmp::Lead
                : amp_name == "bass" ? GuitarAmp::Bass
                                     : GuitarAmp::Crunch);
  if (amp_name != "off") synth.setEffect(channel, &amp);
  // rotary speaker of the organ
  RotarySpeaker rotary;
  rotary.setFast(opt.leslie == "fast");
  if (type == "organ" && opt.leslie != "off") synth.setEffect(channel, &rotary);
  synth.setVolume(opt.volume);
  synth.begin(opt.rate);
  if (type == "organ")
    printf("instrument: tonewheel organ, drawbars %s, leslie %s\n", opt.drawbars.c_str(), opt.leslie.c_str());
  else printf("instrument: %s (%s, %u bytes)%s%s%s\n", set->name, type.c_str(),
         (unsigned)set->dataSize, opt.drums ? " + drums on channel 10" : "",
         amp_name != "off" ? ", amp: " : "", amp_name != "off" ? amp_name.c_str() : "");

  SampleSynthesizerMidiAction action(synth);
  MidiInput midi_input;
  midi_input.verbose = opt.verbose;
  midi_input.local = opt.audio;
  midi_input.parser.begin(&action);
  try {
    if (!setupForwarders(opt, midi_input)) return 1;
  } catch (RtMidiError& e) {
    fprintf(stderr, "midi error: %s\n", e.getMessage().c_str());
    return 1;
  }
  for (auto& in : inputs) {
    in->ignoreTypes(true, true, true);  // sysex, timing, active sensing
    in->setCallback(&MidiInput::callback, &midi_input);
  }

  signal(SIGINT, onSignal);
  signal(SIGTERM, onSignal);
  auto start = std::chrono::steady_clock::now();
  auto elapsed = [&]() {
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
  };

  if (!opt.audio) {
    // only forward the midi messages
    printf("forwarding only (no audio) - press Ctrl+C to stop\n");
    while (running && (opt.seconds <= 0 || elapsed() < opt.seconds)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return 0;
  }

  if (!opt.wav.empty()) {
    // render in real time into a wav file
    FILE* f = fopen(opt.wav.c_str(), "wb");
    if (!f) {
      fprintf(stderr, "could not open %s\n", opt.wav.c_str());
      return 1;
    }
    writeWavHeader(f, opt.rate, 0);
    std::vector<int16_t> buffer(opt.buffer * 2);
    uint64_t frames = 0;
    printf("recording to %s - press Ctrl+C to stop\n", opt.wav.c_str());
    while (running && (opt.seconds <= 0 || elapsed() < opt.seconds)) {
      // keep pace with the real time
      while (frames > elapsed() * opt.rate) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      synth.render(buffer.data(), opt.buffer);
      fwrite(buffer.data(), 2, buffer.size(), f);
      frames += opt.buffer;
    }
    writeWavHeader(f, opt.rate, (uint32_t)(frames * 4));
    fclose(f);
    printf("written %s (%.1f s)\n", opt.wav.c_str(), (double)frames / opt.rate);
    return 0;
  }

  // sound card output
  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_s16;
  config.playback.channels = 2;
  config.sampleRate = opt.rate;
  config.periodSizeInFrames = opt.buffer;
  config.dataCallback = audioCallback;
  config.pUserData = &synth;
  ma_device device;
  if (ma_device_init(NULL, &config, &device) != MA_SUCCESS || ma_device_start(&device) != MA_SUCCESS) {
    fprintf(stderr, "could not open the audio device\n");
    return 1;
  }
  printf("audio: %s, %d Hz - press Ctrl+C to stop\n", device.playback.name, opt.rate);
  while (running && (opt.seconds <= 0 || elapsed() < opt.seconds)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ma_device_uninit(&device);
  return 0;
}
