#pragma once
/**
 * Forwarding of midi messages from the desktop to a microcontroller which
 * runs arduino-midi:
 *  - serial: MidiStreamIn on a serial port (raw midi bytes)
 *  - tcp:    MidiIpServer (raw midi bytes, default port 5008)
 *  - udp:    MidiUdpServer (one datagram per message, default port 5008)
 * Midi output ports (USB midi devices, BLE midi devices paired by the
 * operating system) are handled by the player with RtMidi.
 *
 * Only system APIs are used: POSIX (Linux, macOS) and Win32.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <chrono>
#include <memory>
#include <string>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <termios.h>
#  include <unistd.h>
#endif

#if defined(__linux__)
/// Sets any baud rate (e.g. 31250 for standard midi): serial_baud_linux.cpp
bool ti_set_custom_baud(int fd, int baud);
#endif

namespace tinyinstruments {

/// Sends midi messages to a microcontroller
class MidiForwarder {
 public:
  virtual ~MidiForwarder() = default;
  /// Opens the connection
  virtual bool begin() = 0;
  /// Sends a complete midi message
  virtual bool send(const uint8_t* data, size_t len) = 0;
  /// Description for log messages
  virtual std::string name() const = 0;
};

/// Target specification: "serial:PATH[:BAUD]", "tcp:HOST[:PORT]",
/// "udp:HOST[:PORT]", "midi:PORT", "ble:NAME"
struct ForwardTarget {
  std::string type;  // serial, tcp, udp, midi, ble
  std::string address;
  int number = 0;    // baud rate or port
  bool valid = false;

  static ForwardTarget parse(const std::string& spec) {
    ForwardTarget t;
    size_t colon = spec.find(':');
    if (colon == std::string::npos) return t;
    t.type = spec.substr(0, colon);
    std::string rest = spec.substr(colon + 1);
    if (rest.empty()) return t;
    if (t.type == "midi" || t.type == "ble") {
      t.address = rest;
      t.valid = true;
      return t;
    }
    if (t.type != "serial" && t.type != "tcp" && t.type != "udp") return t;
    t.number = t.type == "serial" ? 115200 : 5008;
    // optional number after the last colon
    size_t last = rest.rfind(':');
    if (last != std::string::npos && last + 1 < rest.size() &&
        rest.find_first_not_of("0123456789", last + 1) == std::string::npos) {
      t.number = atoi(rest.c_str() + last + 1);
      rest = rest.substr(0, last);
    }
    t.address = rest;
    t.valid = !rest.empty() && t.number > 0;
    return t;
  }
};

// ---------------------------------------------------------------------------
// Serial
// ---------------------------------------------------------------------------

/// Raw midi bytes to a serial port (e.g. MidiStreamIn(Serial) at 115200 baud)
class SerialForwarder : public MidiForwarder {
 public:
  SerialForwarder(const std::string& path, int baud) : path(path), baud(baud) {}
  ~SerialForwarder() { close(); }

  bool begin() override {
#ifdef _WIN32
    std::string device = "\\\\.\\" + path;
    handle = CreateFileA(device.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb)) return false;
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;  // no reset of the microcontroller
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    return SetCommState(handle, &dcb) != 0;
#else
    fd = ::open(path.c_str(), O_RDWR | O_NOCTTY);
    if (fd < 0) return false;
    struct termios tio;
    if (tcgetattr(fd, &tio) != 0) return false;
    cfmakeraw(&tio);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~HUPCL;  // keep DTR: no reset when the port is closed
    speed_t speed = standardSpeed(baud);
    if (speed != 0) {
      cfsetispeed(&tio, speed);
      cfsetospeed(&tio, speed);
    }
    if (tcsetattr(fd, TCSANOW, &tio) != 0) return false;
    if (speed == 0) {
#  if defined(__linux__)
      return ti_set_custom_baud(fd, baud);
#  else
      return false;  // non standard baud rate
#  endif
    }
    return true;
#endif
  }

  bool send(const uint8_t* data, size_t len) override {
#ifdef _WIN32
    DWORD written = 0;
    return WriteFile(handle, data, (DWORD)len, &written, NULL) && written == len;
#else
    size_t done = 0;
    while (done < len) {
      ssize_t n = ::write(fd, data + done, len - done);
      if (n < 0) {
        if (errno == EINTR) continue;
        return false;
      }
      done += n;
    }
    return true;
#endif
  }

  std::string name() const override { return "serial " + path + " " + std::to_string(baud) + " baud"; }

 protected:
  std::string path;
  int baud;
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
  void close() {
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    handle = INVALID_HANDLE_VALUE;
  }
#else
  int fd = -1;
  void close() {
    if (fd >= 0) ::close(fd);
    fd = -1;
  }
  static speed_t standardSpeed(int baud) {
    switch (baud) {
      case 9600: return B9600;
      case 19200: return B19200;
      case 38400: return B38400;
      case 57600: return B57600;
      case 115200: return B115200;
      case 230400: return B230400;
#  ifdef B460800
      case 460800: return B460800;
#  endif
#  ifdef B921600
      case 921600: return B921600;
#  endif
      default: return 0;
    }
  }
#endif
};

// ---------------------------------------------------------------------------
// TCP and UDP
// ---------------------------------------------------------------------------

#ifdef _WIN32
typedef SOCKET ti_socket_t;
static const ti_socket_t TI_INVALID_SOCKET = INVALID_SOCKET;
inline void ti_close_socket(ti_socket_t s) { closesocket(s); }
#else
typedef int ti_socket_t;
static const ti_socket_t TI_INVALID_SOCKET = -1;
inline void ti_close_socket(ti_socket_t s) { ::close(s); }
#endif

/// Initializes the sockets (Winsock) once
inline bool ti_init_sockets() {
#ifdef _WIN32
  static bool initialized = false;
  if (!initialized) {
    WSADATA data;
    initialized = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }
  return initialized;
#else
  return true;
#endif
}

/// Raw midi bytes over a TCP connection to MidiIpServer (default port 5008).
/// If the connection is lost, it is opened again (at most once per second).
class TcpForwarder : public MidiForwarder {
 public:
  TcpForwarder(const std::string& host, int port) : host(host), port(port) {}
  ~TcpForwarder() { disconnect(); }

  bool begin() override { return connect(); }

  bool send(const uint8_t* data, size_t len) override {
    if (sock == TI_INVALID_SOCKET) {
      // reconnect at most once per second
      auto now = std::chrono::steady_clock::now();
      if (now - last_attempt < std::chrono::seconds(1)) return false;
      if (!connect()) return false;
      fprintf(stderr, "reconnected to %s\n", name().c_str());
    }
    size_t done = 0;
    while (done < len) {
#ifdef _WIN32
      int n = ::send(sock, (const char*)data + done, (int)(len - done), 0);
#else
      ssize_t n = ::send(sock, data + done, len - done, MSG_NOSIGNAL);
#endif
      if (n <= 0) {
        fprintf(stderr, "connection to %s lost\n", name().c_str());
        disconnect();
        return false;
      }
      done += n;
    }
    return true;
  }

  std::string name() const override { return "tcp " + host + ":" + std::to_string(port); }

  bool isConnected() const { return sock != TI_INVALID_SOCKET; }

 protected:
  std::string host;
  int port;
  ti_socket_t sock = TI_INVALID_SOCKET;
  std::chrono::steady_clock::time_point last_attempt;

  bool connect() {
    last_attempt = std::chrono::steady_clock::now();
    if (!ti_init_sockets()) return false;
    struct addrinfo hints, *result = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result) != 0)
      return false;
    for (struct addrinfo* a = result; a != nullptr; a = a->ai_next) {
      ti_socket_t s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
      if (s == TI_INVALID_SOCKET) continue;
      if (::connect(s, a->ai_addr, (int)a->ai_addrlen) == 0) {
        int flag = 1;  // send each midi message immediately
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&flag, sizeof(flag));
        sock = s;
        break;
      }
      ti_close_socket(s);
    }
    freeaddrinfo(result);
    return sock != TI_INVALID_SOCKET;
  }

  void disconnect() {
    if (sock != TI_INVALID_SOCKET) ti_close_socket(sock);
    sock = TI_INVALID_SOCKET;
  }
};

/// One UDP datagram per midi message to MidiUdpServer (default port 5008)
class UdpForwarder : public MidiForwarder {
 public:
  UdpForwarder(const std::string& host, int port) : host(host), port(port) {}
  ~UdpForwarder() {
    if (sock != TI_INVALID_SOCKET) ti_close_socket(sock);
    if (target) freeaddrinfo(target);
  }

  bool begin() override {
    if (!ti_init_sockets()) return false;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &target) != 0)
      return false;
    sock = socket(target->ai_family, target->ai_socktype, target->ai_protocol);
    return sock != TI_INVALID_SOCKET;
  }

  bool send(const uint8_t* data, size_t len) override {
    if (sock == TI_INVALID_SOCKET) return false;
    return sendto(sock, (const char*)data, (int)len, 0, target->ai_addr, (int)target->ai_addrlen) ==
           (int)len;
  }

  std::string name() const override { return "udp " + host + ":" + std::to_string(port); }

 protected:
  std::string host;
  int port;
  ti_socket_t sock = TI_INVALID_SOCKET;
  struct addrinfo* target = nullptr;
};

/// Creates the forwarder for serial, tcp and udp targets (nullptr otherwise)
inline std::unique_ptr<MidiForwarder> createForwarder(const ForwardTarget& t) {
  if (t.type == "serial") return std::unique_ptr<MidiForwarder>(new SerialForwarder(t.address, t.number));
  if (t.type == "tcp") return std::unique_ptr<MidiForwarder>(new TcpForwarder(t.address, t.number));
  if (t.type == "udp") return std::unique_ptr<MidiForwarder>(new UdpForwarder(t.address, t.number));
  return nullptr;
}

}  // namespace tinyinstruments
