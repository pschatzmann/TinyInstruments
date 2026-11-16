// Any serial baud rate on Linux (e.g. 31250 for standard midi). This needs
// the kernel termios2 interface, which can not be combined with <termios.h>
// in the same file.
#if defined(__linux__)
#include <asm/termbits.h>
#include <sys/ioctl.h>

bool ti_set_custom_baud(int fd, int baud) {
  struct termios2 tio;
  if (ioctl(fd, TCGETS2, &tio) != 0) return false;
  tio.c_cflag &= ~CBAUD;
  tio.c_cflag |= BOTHER;
  tio.c_ispeed = baud;
  tio.c_ospeed = baud;
  return ioctl(fd, TCSETS2, &tio) == 0;
}
#endif
