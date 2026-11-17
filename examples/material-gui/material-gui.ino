/**
 * @file material-gui.ino
 * @brief Touch GUI (Material Design, TinyMaterialDesign) for all instruments:
 * play on the rendered piano keyboard or receive midi via serial, TCP or UDP,
 * and output the sound via I2S or send the midi via serial, TCP or UDP.
 *
 * Default: on-screen keyboard -> I2S (the WiFi is only started when a TCP or
 * UDP input or output is selected).
 *
 * - "Play" page: instrument, octave, sustain, volume and the keyboard. The
 *   keys light up for the notes which are played and also for the notes
 *   received from the midi inputs.
 * - "Setup" page: the midi inputs (the on-screen keyboard is always active),
 *   the output and the network status.
 * - Menu (app bar): "Exit" stops the sound and switches the device off (deep
 *   sleep: the reset button starts it again).
 *
 * Midi inputs (raw midi bytes, as sent by the desktop player
 * `tinyinstruments-player --send serial:/dev/ttyACM0|tcp:IP|udp:IP`):
 * - Serial: USB serial, 115200 baud
 * - TCP: port 5008 (one client)
 * - UDP: port 5008
 *
 * Midi outputs: forward the midi of the keyboard and of the other inputs to
 * Serial, to TCP or UDP of TARGET_HOST:TARGET_PORT (e.g. another
 * microcontroller with this sketch or with MidiIpServer/MidiUdpServer). A
 * message is never sent back to the input it came from.
 *
 * Instruments (compiled in, the small versions with 250 KB each): grand
 * piano, upright piano, electric piano (Wurlitzer, tremolo), electric guitar
 * (clean amp), upright bass, electric bass (bass amp), tenor saxophone,
 * clarinet, drum kit, and the synthesized tonewheel organ with rotary speaker
 * (no sample data). They are selected with General
 * MIDI program changes, so the selection is also forwarded.
 *
 * Requirements:
 * - ESP32-S3 2.8" display board (ILI9341, FT6336 touch, ES8311 codec), e.g.
 *   the Hosyond ESP32-S3 2.8" display; for another board change `board` and
 *   `out`.
 * - 16 MB flash: select the partition scheme "Custom", which uses the
 *   partitions.csv of this folder (6 MB application: the sketch needs about
 *   3.6 MB). PSRAM is recommended.
 * - Libraries: TinyMaterialDesign, TinyGPU, arduino-audio-tools,
 *   arduino-audio-driver, arduino-midi
 */

#define TI_MAX_VOICES 16

#include <TinyGPU.h>
#include <TinyGPU/Boards/LCDBoards.h>
#include <TinyMaterialDesign.h>
#include <WiFi.h>

#include "AudioTools.h"
#include "AudioTools/AudioLibs/I2SCodecStream.h"
#include "AudioBoards/ESP32S3HosyondDisplay.h"
#include "Midi.h"
#include "PianoKeyboard.h"
#include "TinyInstrumentsAudioTools.h"
#include "samples/ClarinetSmall.h"
#include "samples/DrumKitSmall.h"
#include "samples/ElectricBassSmall.h"
#include "samples/ElectricGuitarSmall.h"
#include "samples/GrandPianoSmall.h"
#include "samples/TenorSaxSmall.h"
#include "samples/UprightBassSmall.h"
#include "samples/UprightPianoKWSmall.h"
#include "samples/WurlitzerSmall.h"

// ---------------------------------------------------------------- settings
// WiFi: with an empty SSID an access point TinyInstruments is opened
const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";
const char* AP_PASSWORD = "instruments";
// midi inputs
const uint16_t LISTEN_PORT = 5008;
// midi output via TCP or UDP
const char* TARGET_HOST = "192.168.1.100";
const uint16_t TARGET_PORT = 5008;

// ---------------------------------------------------------------- display
const int kWidth = 240, kHeight = 320;
LCDBoardESP32S3_2_8Display board;
Surface<RGB565> surface(kWidth, kHeight, FontRGB565);
DeviceOutput<RGB565> display(board.display());
GestureDetector gestures;
MaterialTheme<RGB565> theme = defaultTheme<RGB565>();
Screen<RGB565> screen(theme);

/// Reads the touch controller once per loop: the gestures and the keyboard use
/// the same points
class CachedTouch : public TouchDriver {
 public:
  void poll(TouchDriver& driver) {
    count = 0;
    if (driver.isTouched() && driver.getPoint(points[0])) {
      count = 1;
      if (driver.getSecondPoint(points[1])) count = 2;
    }
  }
  bool begin() override { return true; }
  bool isTouched() override { return count > 0; }
  bool getPoint(Point& p) override {
    if (count < 1) return false;
    p = points[0];
    return true;
  }
  bool getSecondPoint(Point& p) override {
    if (count < 2) return false;
    p = points[1];
    return true;
  }
  Point points[2];
  int count = 0;
} touch;

// ---------------------------------------------------------------- audio
SampleSynthesizer synth;
SampleSynthesizerStream synth_stream(synth);
I2SCodecStream out(ESP32S3HosyondDisplay);
StreamCopy copier(out, synth_stream);
audio_tools::Task audio_task("audio", 8192, 10, 0);  // core 0: the GUI runs on core 1

Instrument grand = PianoInstrument(GrandPianoSmall());
Instrument upright = PianoInstrument(UprightPianoKWSmall());
Instrument guitar = GuitarInstrument(ElectricGuitarSmall());
Instrument bass = BassInstrument(ElectricBassSmall());
Instrument sax = SaxophoneInstrument(TenorSaxSmall());
Instrument clarinet = ClarinetInstrument(ClarinetSmall());
Instrument drums = DrumInstrument(DrumKitSmall());
Instrument epiano = ElectricPianoInstrument(WurlitzerSmall());
Instrument upright_bass = UprightBassInstrument(UprightBassSmall());
TremoloEffect tremolo(5.5f, 0.3f);
GuitarAmp clean_amp(GuitarAmp::Clean), bass_amp(GuitarAmp::Bass);
TonewheelOrgan organ("888000000");  // synthesized: no sample data
Instrument organ_instrument = OrganInstrument(organ);
RotarySpeaker leslie(0.2f);
InstrumentBank bank;

/// The instruments of the GUI: General MIDI program or channel 10 (drums)
struct InstrumentInfo {
  const char* name;
  uint8_t program;
  uint8_t channel;
  uint8_t first_note;  // lowest key of the keyboard
};
const InstrumentInfo instruments[] = {
    {"Grand Piano", 0, 0, 48},  {"Upright Piano", 1, 0, 48}, {"E-Piano", 4, 0, 48},
    {"Organ", 16, 0, 48},       {"E-Guitar", 27, 0, 48},     {"Upright Bass", 32, 0, 36},
    {"E-Bass", 33, 0, 36},      {"Tenor Sax", 66, 0, 48},    {"Clarinet", 71, 0, 55},
    {"Drum Kit", 0, 9, 36},
};
const int instrument_count = sizeof(instruments) / sizeof(instruments[0]);
int instrument = 0;

// ---------------------------------------------------------------- midi routing
enum Port { KeyboardPort, SerialPort, TcpPort, UdpPort, I2SPort };
const char* port_names[] = {"Keyboard", "Serial", "TCP", "UDP", "I2S"};
Port output = I2SPort;
bool input_enabled[4] = {true, false, false, false};

WiFiServer tcp_server(LISTEN_PORT);
WiFiClient tcp_in;   // client connected to our server
WiFiClient tcp_out;  // connection to the target
WiFiUDP udp_in, udp_out;
bool wifi_started = false, servers_started = false;
uint32_t tcp_retry_ms = 0;

void showStatus(const char* text);
void onRemoteNote(uint8_t channel, uint8_t note, bool on);
void onRemoteProgram(uint8_t channel, uint8_t program);

/// Sends the midi of all inputs to the synthesizer or forwards it
class MidiRouter : public MidiAction {
 public:
  Port source = KeyboardPort;

  void onNoteOn(uint8_t ch, uint8_t note, uint8_t velocity) override {
    if (source != KeyboardPort) onRemoteNote(ch, note, true);
    if (output == I2SPort) synth.noteOn(ch, note, velocity);
    else send(0x90 | ch, note, velocity);
  }
  void onNoteOff(uint8_t ch, uint8_t note, uint8_t velocity) override {
    if (source != KeyboardPort) onRemoteNote(ch, note, false);
    if (output == I2SPort) synth.noteOff(ch, note, velocity);
    else send(0x80 | ch, note, velocity);
  }
  void onControlChange(uint8_t ch, uint8_t controller, uint8_t value) override {
    if (output == I2SPort) synth.controlChange(ch, controller, value);
    else send(0xB0 | ch, controller, value);
  }
  void onPitchBend(uint8_t ch, uint8_t value) override { onPitchBend14(ch, value << 7); }
  void onPitchBend14(uint8_t ch, uint16_t value) override {
    if (output == I2SPort) synth.pitchBend(ch, value);
    else send(0xE0 | ch, value & 0x7F, (value >> 7) & 0x7F);
  }
  void onProgramChange(uint8_t ch, uint8_t program) override {
    if (source != KeyboardPort) onRemoteProgram(ch, program);
    if (output == I2SPort) synth.programChange(ch, program);
    else send(0xC0 | ch, program, 0, 2);
  }

 protected:
  void send(uint8_t status, uint8_t a, uint8_t b, int len = 3) {
    if (source == output) return;  // never back to the sender
    uint8_t msg[3] = {status, a, b};
    switch (output) {
      case SerialPort:
        Serial.write(msg, len);
        break;
      case TcpPort:
        if (connectTcp()) tcp_out.write(msg, len);
        break;
      case UdpPort:
        if (networkReady()) {
          udp_out.beginPacket(TARGET_HOST, TARGET_PORT);
          udp_out.write(msg, len);
          udp_out.endPacket();
        }
        break;
      default:
        break;
    }
  }

  static bool networkReady() { return WiFi.status() == WL_CONNECTED || WiFi.getMode() == WIFI_AP; }

  static bool connectTcp() {
    if (tcp_out.connected()) return true;
    if (!networkReady() || millis() < tcp_retry_ms) return false;
    if (tcp_out.connect(TARGET_HOST, TARGET_PORT, 300)) {
      tcp_out.setNoDelay(true);
      showStatus("TCP connected");
      return true;
    }
    tcp_retry_ms = millis() + 3000;  // the connect blocks: not too often
    showStatus("TCP connect failed");
    return false;
  }
} router;

MidiParser serial_parser(&router), tcp_parser(&router), udp_parser(&router);

// ---------------------------------------------------------------- widgets
AppBar<RGB565> app_bar(Bounds(0, 0, kWidth, 36), "TinyInstruments");
IconButton<RGB565> menu_button;
Menu<RGB565> menu(Bounds(kWidth - 164, 36, 160, 44));
ListItem<RGB565> exit_item;
Dialog<RGB565> exit_dialog(Bounds(16, (kHeight - 150) / 2, kWidth - 32, 150), "Exit?",
                           "The sound is stopped and the device is switched off.");
Button<RGB565> exit_cancel, exit_confirm;
TabBar<RGB565> tabs(Bounds(0, 36, kWidth, 32));

// play page
Button<RGB565> prev_button(Bounds(8, 74, 40, 30), "<", ButtonVariant::kTonal);
Label<RGB565> instrument_label(Bounds(52, 74, 136, 30), "", TypographyRole::kTitle, TextAlign::kCenter);
Button<RGB565> next_button(Bounds(192, 74, 40, 30), ">", ButtonVariant::kTonal);
Button<RGB565> octave_down(Bounds(8, 110, 40, 30), "-", ButtonVariant::kOutlined);
Label<RGB565> range_label(Bounds(52, 110, 76, 30), "", TypographyRole::kBody, TextAlign::kCenter);
Button<RGB565> octave_up(Bounds(132, 110, 40, 30), "+", ButtonVariant::kOutlined);
Chip<RGB565> sustain_chip(Bounds(178, 110, 56, 30), "Sus", true);
SliderRow<RGB565> volume_row("Volume", 0, 100, 5, [](float v, char* buf, size_t len) {
  snprintf(buf, len, "%d %%", (int)v);
});
PianoKeyboard<RGB565> piano(Bounds(2, 192, kWidth - 4, kHeight - 194), 10);
Widget<RGB565>* play_page[] = {&prev_button, &instrument_label, &next_button, &octave_down, &range_label,
                               &octave_up,   &sustain_chip,     &volume_row,  &piano};

// setup page
Label<RGB565> inputs_label(Bounds(12, 74, 216, 16), "Midi inputs (+ keyboard)", TypographyRole::kLabel);
SwitchRow<RGB565> serial_row("Serial (115200 baud)");
SwitchRow<RGB565> tcp_row("TCP");
SwitchRow<RGB565> udp_row("UDP");
Label<RGB565> output_label(Bounds(12, 180, 216, 16), "Output", TypographyRole::kLabel);
SegmentedButton<RGB565> output_select(Bounds(8, 198, kWidth - 16, 32));
KeyValueRow<RGB565> wifi_row("WiFi", "off");
KeyValueRow<RGB565> listen_row("Listen");
KeyValueRow<RGB565> target_row("Target");
KeyValueRow<RGB565> status_row("Status", "ready");
Widget<RGB565>* setup_page[] = {&inputs_label, &serial_row,    &tcp_row,    &udp_row,   &output_label,
                                &output_select, &wifi_row, &listen_row, &target_row, &status_row};

// ---------------------------------------------------------------- GUI logic
char range_text[16], listen_text[32], target_text[40], wifi_text[40];

void showStatus(const char* text) {
  status_row.setValue(text);
  screen.invalidate();
}

void showPage(int page) {
  for (auto* w : play_page) w->visible = page == 0;
  for (auto* w : setup_page) w->visible = page == 1;
  if (page != 0) piano.releaseAll();
  screen.invalidate();
}

int keyboardChannel() { return instruments[instrument].channel; }

void updateLabels() {
  instrument_label.setText(instruments[instrument].name);
  static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  int lo = piano.firstNote(), hi = piano.lastNote();
  snprintf(range_text, sizeof(range_text), "%s%d-%s%d", names[lo % 12], lo / 12 - 1, names[hi % 12], hi / 12 - 1);
  range_label.setText(range_text);
  screen.invalidate();
}

void selectInstrument(int index) {
  piano.releaseAll();
  instrument = (index + instrument_count) % instrument_count;
  const InstrumentInfo& info = instruments[instrument];
  router.source = KeyboardPort;
  if (info.channel != 9) router.onProgramChange(info.channel, info.program);
  piano.setFirstNote(info.first_note);
  piano.clearMarks();
  updateLabels();
}

/// A note from a midi input: show it on the keyboard
void onRemoteNote(uint8_t channel, uint8_t note, bool on) {
  if (channel == keyboardChannel()) piano.markNote(note, on);
}

/// A program change from a midi input: show the instrument
void onRemoteProgram(uint8_t channel, uint8_t program) {
  for (int j = 0; j < instrument_count; j++) {
    if (instruments[j].channel == channel && instruments[j].program == program && j != instrument) {
      instrument = j;
      updateLabels();
    }
  }
}

void startNetwork() {
  if (wifi_started) return;
  wifi_started = true;
  if (strlen(WIFI_SSID) > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // low latency
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    snprintf(wifi_text, sizeof(wifi_text), "connecting %s", WIFI_SSID);
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("TinyInstruments", AP_PASSWORD);
    snprintf(wifi_text, sizeof(wifi_text), "AP TinyInstruments");
  }
  wifi_row.setValue(wifi_text);
  screen.invalidate();
}

/// Starts the servers when the network is up and shows the address
void updateNetwork() {
  if (!wifi_started || servers_started) return;
  bool ap = WiFi.getMode() == WIFI_AP;
  if (!ap && WiFi.status() != WL_CONNECTED) return;
  tcp_server.begin();
  tcp_server.setNoDelay(true);
  udp_in.begin(LISTEN_PORT);
  servers_started = true;
  IPAddress ip = ap ? WiFi.softAPIP() : WiFi.localIP();
  snprintf(wifi_text, sizeof(wifi_text), "%s", ap ? "AP TinyInstruments" : WIFI_SSID);
  snprintf(listen_text, sizeof(listen_text), "%s:%u", ip.toString().c_str(), LISTEN_PORT);
  wifi_row.setValue(wifi_text);
  listen_row.setValue(listen_text);
  showStatus("network ready");
}

void setOutput(Port port) {
  piano.releaseAll();
  synth.allNotesOff();
  if (output == TcpPort) tcp_out.stop();
  output = port;
  if (port == TcpPort || port == UdpPort) startNetwork();
  if (port == TcpPort) tcp_retry_ms = 0;
  char text[32];
  snprintf(text, sizeof(text), "output: %s", port_names[port]);
  showStatus(text);
  // the selected instrument is also needed by the new output
  selectInstrument(instrument);
}

/// Power icon of the "Exit" menu item
template <typename RGB_T>
void drawPowerIcon(tinygpu::ISurface<RGB_T>& target, const Bounds& r, RGB_T color, uint8_t thickness = 2) {
  const int32_t radius = r.w * 3 / 8;
  const int32_t cx = r.centerX(), cy = r.centerY() + 1;
  target.drawArc(toPx(cx), toPx(cy), toPx(radius), 300.0f, 600.0f, color, thickness);
  drawLineThick(target, cx, cy - radius - 1, cx, cy - 1, color, thickness);
}

/// "Exit" in the menu: stops the sound and switches the device off (deep
/// sleep without wake-up source: only the reset button starts it again)
void shutdown() {
  screen.dismissDialog();
  piano.releaseAll();
  synth.allNotesOff();
  audio_task.end();
  out.end();
  tcp_out.stop();
  tcp_in.stop();
  if (board.backlightPin() >= 0) {
    const gpio_num_t backlight = (gpio_num_t)board.backlightPin();
    digitalWrite(backlight, LOW);
    gpio_hold_en(backlight);  // keeps the backlight off during the deep sleep
    gpio_deep_sleep_hold_en();
  }
  Serial.flush();
  esp_deep_sleep_start();
}

void setupWidgets() {
  menu_button = IconButton<RGB565>(app_bar.trailingRect(), drawMenu<RGB565>);
  menu_button.onClick = []() { screen.presentDialog(menu); };
  app_bar.trailing = &menu_button;
  screen.addFixedWidget(app_bar);
  exit_item = ListItem<RGB565>(menu.itemRect(0, 44), "Exit", drawPowerIcon<RGB565>);
  exit_item.onClick = []() {
    screen.dismissDialog();
    screen.presentDialog(exit_dialog);
  };
  menu.addItem(exit_item);
  menu.onOutsideTap = []() { screen.dismissDialog(); };
  exit_cancel = Button<RGB565>(exit_dialog.actionRect(0, 2), "Cancel", ButtonVariant::kText);
  exit_cancel.onClick = []() { screen.dismissDialog(); };
  exit_confirm = Button<RGB565>(exit_dialog.actionRect(1, 2), "Exit");
  exit_confirm.onClick = []() { shutdown(); };
  exit_dialog.addAction(exit_cancel);
  exit_dialog.addAction(exit_confirm);
  tabs.addTab("Play");
  tabs.addTab("Setup");
  tabs.onChange = [](int index) { showPage(index); };
  screen.addFixedWidget(tabs);

  prev_button.onClick = []() { selectInstrument(instrument - 1); };
  next_button.onClick = []() { selectInstrument(instrument + 1); };
  octave_down.onClick = []() {
    if (piano.firstNote() >= 12) piano.setFirstNote(piano.firstNote() - 12);
    piano.clearMarks();
    updateLabels();
  };
  octave_up.onClick = []() {
    if (piano.lastNote() + 12 <= 127) piano.setFirstNote(piano.firstNote() + 12);
    piano.clearMarks();
    updateLabels();
  };
  sustain_chip.onChange = [](bool on) {
    router.source = KeyboardPort;
    router.onControlChange(keyboardChannel(), 64, on ? 127 : 0);
  };
  volume_row.bounds = Bounds(12, 146, 216, SliderRow<RGB565>::kHeight);
  volume_row.setValue(80);
  volume_row.onChange = [](float v) { synth.setVolume(v / 100.0f); };
  piano.onKey = [](uint8_t note, uint8_t velocity) {
    router.source = KeyboardPort;
    if (velocity > 0) router.onNoteOn(keyboardChannel(), note, velocity);
    else router.onNoteOff(keyboardChannel(), note, 0);
  };
  for (auto* w : play_page) screen.addFixedWidget(*w);

  int y = 92;
  for (SwitchRow<RGB565>* row : {&serial_row, &tcp_row, &udp_row}) {
    row->bounds = Bounds(12, y, 216, SwitchRow<RGB565>::kHeight);
    y += SwitchRow<RGB565>::kHeight;
  }
  serial_row.onChange = [](bool on) { input_enabled[SerialPort] = on; };
  tcp_row.onChange = [](bool on) {
    input_enabled[TcpPort] = on;
    if (on) startNetwork();
    else tcp_in.stop();
  };
  udp_row.onChange = [](bool on) {
    input_enabled[UdpPort] = on;
    if (on) startNetwork();
  };
  for (Port p : {I2SPort, SerialPort, TcpPort, UdpPort}) output_select.addSegment(port_names[p]);
  output_select.setSelected(0, true);
  output_select.onChange = [](uint32_t mask) {
    const Port ports[] = {I2SPort, SerialPort, TcpPort, UdpPort};
    for (int j = 0; j < 4; j++)
      if (mask & (1u << j)) setOutput(ports[j]);
  };
  y = 238;
  snprintf(target_text, sizeof(target_text), "%s:%u", TARGET_HOST, TARGET_PORT);
  target_row.setValue(target_text);
  listen_row.setValue("-");
  for (KeyValueRow<RGB565>* row : {&wifi_row, &listen_row, &target_row, &status_row}) {
    row->bounds = Bounds(12, y, 216, 18);
    y += 20;
  }
  for (auto* w : setup_page) screen.addFixedWidget(*w);

  gestures.onGesture = [](GestureEvent& e) { screen.handleGesture(e); };
  gestures.isDraggable = [](int16_t x, int16_t y) { return screen.isDraggableAt(x, y); };
  showPage(0);
}

// ---------------------------------------------------------------- midi inputs
void readInputs() {
  uint8_t buffer[64];
  if (input_enabled[SerialPort]) {
    int n = Serial.available();
    if (n > 0) {
      n = Serial.readBytes(buffer, n > (int)sizeof(buffer) ? sizeof(buffer) : n);
      router.source = SerialPort;
      serial_parser.parse(buffer, n);
    }
  }
  if (!servers_started) return;
  if (input_enabled[TcpPort]) {
    if (!tcp_in.connected()) {
      WiFiClient client = tcp_server.available();
      if (client) {
        tcp_in = client;
        tcp_in.setNoDelay(true);
        showStatus("TCP client connected");
      }
    }
    while (tcp_in.connected() && tcp_in.available() > 0) {
      int n = tcp_in.read(buffer, sizeof(buffer));
      if (n <= 0) break;
      router.source = TcpPort;
      tcp_parser.parse(buffer, n);
    }
  }
  if (input_enabled[UdpPort]) {
    while (udp_in.parsePacket() > 0) {
      int n = udp_in.read(buffer, sizeof(buffer));
      router.source = UdpPort;
      if (n > 0) udp_parser.parse(buffer, n);
    }
  }
}

// ---------------------------------------------------------------- setup/loop
void setupAudio() {
  bank.add(0, 7, grand);
  bank.add(1, 1, upright);
  bank.add(24, 31, guitar, &clean_amp);
  bank.add(32, 39, bass, &bass_amp);
  bank.add(32, 32, upright_bass);
  bank.add(4, 5, epiano, &tremolo);
  bank.add(64, 67, sax);
  bank.add(68, 71, clarinet);
  bank.add(16, 20, organ_instrument, &leslie);
  bank.setDrumKit(drums);
  synth.setInstrumentBank(&bank);
  synth.setInstrument(-1, grand);
  synth.setInstrument(9, drums);

  auto cfg = out.defaultConfig(TX_MODE);
  cfg.copyFrom(synth_stream.defaultConfig());  // 44100 Hz, stereo, 16 bits
  out.begin(cfg);
  out.setVolume(0.7f);  // codec; the slider changes the synthesizer volume
  synth_stream.begin(cfg);
  synth.setVolume(0.8f);
  audio_task.begin([]() { copier.copy(); });
}

void setup() {
  Serial.begin(115200);
  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Warning);
  if (board.backlightPin() >= 0) gpio_hold_dis((gpio_num_t)board.backlightPin());  // see shutdown()
  board.begin();
  display.begin();
  surface.begin();
  setupAudio();
  setupWidgets();
  selectInstrument(0);
}

void loop() {
  touch.poll(*board.touch());
  gestures.update(touch);
  // no keys while the menu or a dialog is open
  piano.setTouches(touch.points, screen.hasDialog() ? 0 : touch.count);
  readInputs();
  updateNetwork();
  screen.update(millis());
  if (screen.isDirty()) {
    screen.draw(surface);
    display.writeData(surface);
  }
}
