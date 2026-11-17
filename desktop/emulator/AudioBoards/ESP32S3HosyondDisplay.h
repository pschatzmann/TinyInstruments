#pragma once
/**
 * Desktop build of the example sketches (examples/CMakeLists.txt): replaces
 * the board definition of the arduino-audio-driver library, which is not used
 * on the desktop. The I2SCodecStream of arduino-audio-tools outputs to the
 * sound card and ignores the board.
 */

/// Placeholder for the ES8311 codec board of the ESP32-S3 2.8" display
struct DesktopAudioBoard {};
static DesktopAudioBoard ESP32S3HosyondDisplay;
