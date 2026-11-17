#pragma once
/**
 * Desktop build of the example sketches (examples/CMakeLists.txt): the
 * display boards of TinyGPU which are used by the sketches are simulated with
 * an SDL window with the size of the panel and the mouse as touch, so that the
 * sketches run unchanged. This header is included automatically (-include)
 * before the sketch.
 */
#include <TinyGPU.h>
#include <TinyGPU/Boards/LCDBoards.h>

namespace tinygpu {

/// ESP32-S3 2.8" display board (ILI9341, 240x320) used by material-gui
class LCDBoardESP32S3_2_8Display : public LCDBoardDesktopSDL {
 public:
  LCDBoardESP32S3_2_8Display() : LCDBoardDesktopSDL(240, 320) {
    setTitle("ESP32-S3 2.8\" Display");
  }
};

}  // namespace tinygpu
