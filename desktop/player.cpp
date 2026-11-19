/**
 * TinyInstruments desktop player: plays the instruments with a (USB) midi
 * keyboard or a midi file on the sound card of the computer. The midi messages are parsed with
 * arduino-midi and passed to the SampleSynthesizer with the same
 * SampleSynthesizerMidiAction which is used on the microcontrollers.
 *
 *   tinyinstruments-player --list
 *   tinyinstruments-player --instrument grand --drums
 *   tinyinstruments-player --file data/TenorSax.bin --type sax
 *   tinyinstruments-player --instrument guitar --amp lead
 *   tinyinstruments-player --instrument organ --drawbars 888800000 --leslie fast
 *   tinyinstruments-player --instrument epiano --tremolo 0.5
 *   tinyinstruments-player --virtual --wav out.wav --seconds 10
 *   tinyinstruments-player --send tcp:192.168.1.40 --no-audio
 *   tinyinstruments-player --midi-file song.mid --loop
 *   tinyinstruments-player --port Keystation --send midi:UM-ONE --thru --no-audio
 *
 * Midi files (--midi-file) are parsed with arduino-midi-fileparser
 * (MidiFileSong.h: the tracks are merged), the General MIDI program changes
 * select the instruments (GeneralMidiBank).
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

#include "MidiFileSong.h"
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
#include "samples/UprightBass.h"
#include "samples/UprightPianoKW.h"
#include "samples/Wurlitzer.h"

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
  float tremolo = -1;                  // electric piano tremolo depth (< 0: default)
  std::vector<std::string> send;  // forward targets
  std::string midi_file;          // midi file to play
  bool loop = false;              // repeat the midi file
  bool gm = false;                // General MIDI instruments (program changes)
  bool thru = false;              // forward all message types (sysex, clock...)
  bool instrument_selected = false;  // --instrument or --file was given
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
      "TinyInstruments desktop player: plays the instruments with a midi keyboard or a midi file\n\n"
      "Usage: tinyinstruments-player [options]\n"
      "  --list                 list the midi input and output ports\n"
      "  --port N|NAME          midi input port number or part of the name (default: all)\n"
      "  --virtual              open a virtual midi input port 'TinyInstruments'\n"
      "  --midi-file PATH       play a midi file (.mid); the program exits at the end\n"
      "  --loop                 play the midi file again and again (pause of 2 s)\n"
      "  --gm                   General MIDI: the program changes select the instruments\n"
      "                         (default for --midi-file, unless --instrument or --file\n"
      "                         is given)\n"
      "  --instrument NAME      upright | grand | epiano | drums | sax | clarinet | guitar |\n"
      "                         bass | upright-bass | organ (synthesized tonewheel organ)\n"
      "  --tremolo DEPTH        tremolo 0..1 (default: 0.3 for the electric piano, otherwise\n"
      "                         off; modulation wheel: depth)\n"
      "  --drawbars DIGITS      organ registration, 9 digits 0..8 (default: 888000000)\n"
      "  --leslie MODE          organ rotary speaker: slow | fast | off (default: slow,\n"
      "                         modulation wheel: fast/slow)\n"
      "                         (default: grand)\n"
      "  --file PATH            load the sample set from a file (e.g. data/GrandPiano.bin)\n"
      "  --type TYPE            piano | epiano | drums | sax | clarinet | guitar | bass |\n"
      "                         upright-bass: playing\n"
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
      "  --no-audio             do not play the instruments on the desktop (only forward)\n"
      "  --thru                 midi thru: forward all messages unchanged, also sysex, clock,\n"
      "                         start/stop and active sensing (default: they are ignored)\n");
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
    else if (a == "--instrument") {
      opt.instrument = next();
      opt.instrument_selected = true;
    }
    else if (a == "--midi-file") opt.midi_file = next();
    else if (a == "--loop") opt.loop = true;
    else if (a == "--gm") opt.gm = true;
    else if (a == "--drawbars") opt.drawbars = next();
    else if (a == "--leslie") opt.leslie = next();
    else if (a == "--tremolo") opt.tremolo = atof(next().c_str());
    else if (a == "--file") {
      opt.file = next();
      opt.instrument_selected = true;
    }
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
    else if (a == "--thru") opt.thru = true;
    else {
      usage();
      return false;
    }
  }
  // a midi file: General MIDI instruments, unless an instrument was selected
  if (!opt.midi_file.empty() && !opt.instrument_selected) opt.gm = true;
  return true;
}

/// Passes the raw midi bytes of all input ports and of the midi file to the
/// parser and to the forwarders (single producer for the lock free queue of
/// the synthesizer: RtMidi uses one thread per port and the midi file has its
/// own thread, so the messages are serialized with a mutex)
struct MidiInput {
  midi::MidiParser parser;
  std::mutex mutex;
  bool verbose = false;
  bool local = true;  // play on the desktop
  std::vector<std::unique_ptr<MidiForwarder>> forwarders;
  std::vector<std::unique_ptr<RtMidiOut>> outputs;

  static void callback(double, std::vector<unsigned char>* msg, void* user) {
    if (msg->empty()) return;
    ((MidiInput*)user)->process(msg->data(), msg->size());
  }

  /// Plays and forwards a complete midi message
  void process(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex);
    if (verbose) {
      for (size_t j = 0; j < len; j++) printf("%02X ", data[j]);
      printf("\n");
    }
    if (local) parser.parse((uint8_t*)data, (int)len);
    for (auto& f : forwarders) f->send(data, len);
    for (auto& out : outputs) {
      try {
        out->sendMessage(data, len);
      } catch (RtMidiError& e) {
        fprintf(stderr, "midi output error: %s\n", e.getMessage().c_str());
      }
    }
  }

  /// All notes off and sustain off on all channels (also on the forward
  /// targets)
  void allNotesOff() {
    for (uint8_t ch = 0; ch < 16; ch++) {
      uint8_t sustain_off[3] = {(uint8_t)(0xB0 | ch), 64, 0};
      uint8_t notes_off[3] = {(uint8_t)(0xB0 | ch), 123, 0};
      process(sustain_off, 3);
      process(notes_off, 3);
    }
  }
};

/// Names of the opened midi input ports: an output port with the same name
/// would send the messages back to the input (endless loop, e.g. "Midi
/// Through")
static std::vector<std::string> input_names;
static bool output_is_input = false;

/// Opens the midi output port whose name contains the indicated text
static std::unique_ptr<RtMidiOut> openMidiOutput(const std::string& name) {
  std::unique_ptr<RtMidiOut> out(new RtMidiOut(RtMidi::UNSPECIFIED, "TinyInstruments Out"));
  for (unsigned j = 0; j < out->getPortCount(); j++) {
    std::string port = out->getPortName(j);
    if (port.find(name) != std::string::npos && port.find("TinyInstruments") == std::string::npos) {
      for (const std::string& input : input_names) {
        if (input == port) {
          fprintf(stderr,
                  "midi output '%s' is also an input: this would be an endless loop (select the "
                  "input with --port)\n",
                  port.c_str());
          output_is_input = true;
          return nullptr;
        }
      }
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
        if (output_is_input) return false;  // reported by openMidiOutput()
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

/// Waits until the time point: returns false if the program is stopped
static bool waitUntil(std::chrono::steady_clock::time_point due) {
  while (running) {
    auto now = std::chrono::steady_clock::now();
    if (now >= due) return true;
    std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(
        due - now, std::chrono::milliseconds(20)));
  }
  return false;
}

/// Plays the midi file in real time (own thread). Without --loop the program
/// is stopped at the end, after the notes have faded out.
static void playMidiFile(const MidiFileSong& song, MidiInput& input, bool loop) {
  using namespace std::chrono;
  while (running) {
    auto start = steady_clock::now();
    for (const MidiFileSong::Event& e : song.events) {
      auto due = start + duration_cast<steady_clock::duration>(duration<double, std::milli>(e.time_ms));
      if (!waitUntil(due)) break;
      input.process(e.data, e.len);
    }
    input.allNotesOff();
    if (!running) return;
    printf("end of the midi file\n");
    // pause of 2 s: the release of the notes, then again or stop
    if (!waitUntil(steady_clock::now() + seconds(2))) return;
    if (!loop) {
      running = false;
      return;
    }
  }
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
      input_names.push_back(name);
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
  if (inputs.empty() && opt.midi_file.empty())
    printf("no midi input: use --virtual or connect a midi keyboard\n");

  // midi file
  MidiFileSong song;
  if (!opt.midi_file.empty()) {
    if (!song.load(opt.midi_file)) {
      fprintf(stderr, "could not play %s: %s\n", opt.midi_file.c_str(), song.error.c_str());
      return 1;
    }
    int seconds = (int)(song.durationMs() / 1000);
    printf("midi file: %s (format %d, %d tracks, %d notes, %d:%02d)%s\n", opt.midi_file.c_str(), song.format,
           song.track_count, song.noteCount(), seconds / 60, seconds % 60, opt.loop ? ", loop" : "");
  }

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
             : opt.file.find("UprightBass") != std::string::npos ? "upright-bass"
             : opt.file.find("Wurlitzer") != std::string::npos ? "epiano"
             : opt.file.find("Bass") != std::string::npos   ? "bass"
             : opt.file.find("Guitar") != std::string::npos ? "guitar"
                                                             : "piano";
    }
  } else if (opt.instrument == "organ") {
    type = "organ";
  } else if (opt.instrument == "epiano") {
    set = &Wurlitzer();
    type = "epiano";
  } else if (opt.instrument == "upright-bass") {
    set = &UprightBass();
    type = "upright-bass";
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
                          : type == "epiano"   ? ElectricPianoInstrument(*set)
                          : type == "upright-bass" ? UprightBassInstrument(*set)
                          : type == "drums"    ? DrumInstrument(*set)
                          : type == "sax"    ? SaxophoneInstrument(*set)
                          : type == "guitar" ? GuitarInstrument(*set)
                          : type == "bass"   ? BassInstrument(*set)
                          : type == "clarinet" ? ClarinetInstrument(*set)
                                             : PianoInstrument(*set);
  Instrument drums = DrumInstrument(DrumKit());
  SampleSynthesizer synth;
  int channel = opt.channel > 0 ? opt.channel - 1 : (type == "drums" ? 9 : -1);
  if (!opt.gm) {
    synth.setInstrument(channel, instrument);
    if (opt.drums) synth.setInstrument(9, drums);
  }
  // General MIDI: all instruments, selected by the program changes
  GeneralMidiBank gm_bank;
  if (opt.gm) {
    GeneralMidiBank::SampleSets sets;
    sets.piano = &GrandPiano();
    sets.electricPiano = &Wurlitzer();
    sets.guitar = &ElectricGuitar();
    sets.uprightBass = &UprightBass();
    sets.bass = &ElectricBass();
    sets.saxophone = &TenorSax();
    sets.clarinet = &Clarinet();
    sets.drums = &DrumKit();
    gm_bank.begin(synth, sets);
  }

  // guitar amplifier (also usable with the other instruments)
  std::string amp_name = opt.amp;
  if (amp_name.empty()) amp_name = type == "guitar" ? "crunch" : (type == "bass" ? "bass" : "off");
  GuitarAmp amp(amp_name == "clean"  ? GuitarAmp::Clean
                : amp_name == "lead" ? GuitarAmp::Lead
                : amp_name == "bass" ? GuitarAmp::Bass
                                     : GuitarAmp::Crunch);
  if (amp_name != "off" && !opt.gm) synth.setEffect(channel, &amp);
  // rotary speaker of the organ
  RotarySpeaker rotary;
  rotary.setFast(opt.leslie == "fast");
  if (type == "organ" && opt.leslie != "off" && !opt.gm) synth.setEffect(channel, &rotary);
  // tremolo (electric piano)
  float tremolo_depth = opt.tremolo >= 0 ? opt.tremolo : (type == "epiano" ? 0.3f : 0.0f);
  TremoloEffect tremolo(5.5f, tremolo_depth);
  if (tremolo_depth > 0 && !opt.gm) synth.setEffect(channel, &tremolo);
  synth.setVolume(opt.volume);
  synth.begin(opt.rate);
  if (opt.gm)
    printf("instruments: General MIDI (program changes), drums on channel 10\n");
  else if (type == "organ")
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
    // sysex, timing (clock, start/stop), active sensing: only with --thru;
    // the parser of the synthesizer skips them
    in->ignoreTypes(!opt.thru, !opt.thru, !opt.thru);
    in->setCallback(&MidiInput::callback, &midi_input);
  }

  signal(SIGINT, onSignal);
  signal(SIGTERM, onSignal);
  auto start = std::chrono::steady_clock::now();
  auto elapsed = [&]() {
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
  };

  // plays the midi file in its own thread: started when the output is ready,
  // stopped and joined before midi_input is destroyed
  struct FilePlayer {
    std::thread thread;
    void start(const MidiFileSong& song, MidiInput& input, bool loop) {
      if (!song.events.empty()) thread = std::thread(playMidiFile, std::cref(song), std::ref(input), loop);
    }
    ~FilePlayer() {
      running = false;
      if (thread.joinable()) thread.join();
    }
  } file_player;

  if (!opt.audio) {
    // only forward the midi messages
    printf("forwarding only (no audio) - press Ctrl+C to stop\n");
    file_player.start(song, midi_input, opt.loop);
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
    file_player.start(song, midi_input, opt.loop);
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
  file_player.start(song, midi_input, opt.loop);
  while (running && (opt.seconds <= 0 || elapsed() < opt.seconds)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  ma_device_uninit(&device);
  return 0;
}
