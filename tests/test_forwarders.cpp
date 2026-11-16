// Tests of the midi forwarders of the desktop player with real sockets and a
// pseudo terminal (POSIX only)
#include <pty.h>
#include <sys/select.h>

#include "../desktop/MidiForwarders.h"
#include "TinyInstruments.h"
#include "TestHelper.h"

using namespace tinyinstruments;

static const uint8_t NOTE_ON[] = {0x90, 60, 100};
static const uint8_t NOTE_OFF[] = {0x80, 60, 0};

/// Reads up to len bytes with a timeout
static std::vector<uint8_t> readWithTimeout(int fd, size_t len, int timeout_ms = 1000) {
  std::vector<uint8_t> result;
  while (result.size() < len) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    if (select(fd + 1, &set, nullptr, nullptr, &tv) <= 0) break;
    uint8_t buffer[64];
    ssize_t n = ::read(fd, buffer, std::min(sizeof(buffer), len - result.size()));
    if (n <= 0) break;
    result.insert(result.end(), buffer, buffer + n);
  }
  return result;
}

typedef std::vector<uint8_t> Bytes;

static void testParseTargets() {
  ForwardTarget t = ForwardTarget::parse("serial:/dev/ttyUSB0");
  CHECK(t.valid && t.type == "serial" && t.address == "/dev/ttyUSB0" && t.number == 115200);
  t = ForwardTarget::parse("serial:/dev/ttyUSB0:31250");
  CHECK(t.valid && t.address == "/dev/ttyUSB0" && t.number == 31250);
  t = ForwardTarget::parse("serial:COM3:115200");
  CHECK(t.valid && t.address == "COM3" && t.number == 115200);
  t = ForwardTarget::parse("tcp:192.168.1.40");
  CHECK(t.valid && t.type == "tcp" && t.address == "192.168.1.40" && t.number == 5008);
  t = ForwardTarget::parse("udp:esp32.local:6000");
  CHECK(t.valid && t.type == "udp" && t.address == "esp32.local" && t.number == 6000);
  t = ForwardTarget::parse("midi:USB MIDI:0");  // port names can contain colons
  CHECK(t.valid && t.type == "midi" && t.address == "USB MIDI:0");
  t = ForwardTarget::parse("ble:TinyPiano");
  CHECK(t.valid && t.type == "ble" && t.address == "TinyPiano");
  CHECK(!ForwardTarget::parse("ftp:host").valid);
  CHECK(!ForwardTarget::parse("tcp:").valid);
  CHECK(!ForwardTarget::parse("nonsense").valid);
}

/// Listening TCP socket on localhost: returns the port
static int listenTcp(int& server) {
  server = socket(AF_INET, SOCK_STREAM, 0);
  int flag = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  bind(server, (struct sockaddr*)&addr, sizeof(addr));
  listen(server, 1);
  socklen_t len = sizeof(addr);
  getsockname(server, (struct sockaddr*)&addr, &len);
  return ntohs(addr.sin_port);
}

static void testTcp() {
  int server;
  int port = listenTcp(server);
  TcpForwarder tcp("localhost", port);
  CHECK(tcp.begin());
  int client = accept(server, nullptr, nullptr);
  CHECK(tcp.send(NOTE_ON, 3));
  CHECK(tcp.send(NOTE_OFF, 3));
  Bytes received = readWithTimeout(client, 6);
  CHECK(received == Bytes({0x90, 60, 100, 0x80, 60, 0}));

  // the microcontroller restarts: the connection is opened again
  close(client);
  usleep(100000);
  bool failed = false;
  for (int j = 0; j < 5 && !failed; j++) {  // the first sends may still succeed
    failed = !tcp.send(NOTE_ON, 3);
    usleep(20000);
  }
  CHECK(failed);
  CHECK(!tcp.isConnected());
  usleep(1100000);  // reconnect at most once per second
  CHECK(tcp.send(NOTE_OFF, 3));
  client = accept(server, nullptr, nullptr);
  CHECK(readWithTimeout(client, 3) == Bytes({0x80, 60, 0}));
  close(client);
  close(server);

  // nobody listening
  TcpForwarder nothing("localhost", port);
  CHECK(!nothing.begin());
}

static void testUdp() {
  int server = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  bind(server, (struct sockaddr*)&addr, sizeof(addr));
  socklen_t len = sizeof(addr);
  getsockname(server, (struct sockaddr*)&addr, &len);
  int port = ntohs(addr.sin_port);

  UdpForwarder udp("127.0.0.1", port);
  CHECK(udp.begin());
  CHECK(udp.send(NOTE_ON, 3));
  CHECK(udp.send(NOTE_OFF, 3));
  // one datagram per message
  uint8_t buffer[16];
  CHECK(recv(server, buffer, sizeof(buffer), 0) == 3);
  CHECK(Bytes(buffer, buffer + 3) == Bytes({0x90, 60, 100}));
  CHECK(recv(server, buffer, sizeof(buffer), 0) == 3);
  CHECK(Bytes(buffer, buffer + 3) == Bytes({0x80, 60, 0}));
  close(server);
}

static void testSerial() {
  for (int baud : {115200, 31250}) {
    int master, slave;
    char path[256];
    CHECK(openpty(&master, &slave, path, nullptr, nullptr) == 0);
    SerialForwarder serial(path, baud);
    bool ok = serial.begin();
    if (!ok) printf("  serial %d baud: %s\n", baud, strerror(errno));
    CHECK(ok);
    CHECK(serial.send(NOTE_ON, 3));
    CHECK(readWithTimeout(master, 3) == Bytes({0x90, 60, 100}));
    // the line settings are raw: bytes like 0x0D are not translated
    const uint8_t cc[] = {0xB0, 0x0D, 0x0A};
    CHECK(serial.send(cc, 3));
    CHECK(readWithTimeout(master, 3) == Bytes({0xB0, 0x0D, 0x0A}));
    close(slave);
    close(master);
  }
  SerialForwarder missing("/dev/does-not-exist", 115200);
  CHECK(!missing.begin());
}

int main() {
  testParseTargets();
  testTcp();
  testUdp();
  testSerial();
  return testResult("test_forwarders");
}
