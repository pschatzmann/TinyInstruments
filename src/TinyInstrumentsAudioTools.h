#pragma once
/**
 * Integration with the arduino-audio-tools library: the SampleSynthesizer as
 * AudioStream which can be copied to any audio output with a StreamCopy.
 */
#include "AudioTools.h"
#include "TinyInstruments.h"

#if defined(IS_DESKTOP) && !defined(USE_I2S)
// Desktop (Arduino-Emulator): the I2S output of the sketches is the sound card
#  include "AudioTools/AudioLibs/MiniAudioStream.h"
namespace audio_tools {
using I2SStream = MiniAudioStream;
}
#endif

namespace tinyinstruments {

/**
 * @brief Provides the output of a SampleSynthesizer as 16 bit audio stream
 * (mono or stereo).
 */
class SampleSynthesizerStream : public audio_tools::AudioStream {
 public:
  explicit SampleSynthesizerStream(SampleSynthesizer& synthesizer) : p_synth(&synthesizer) {
    info.sample_rate = 44100;
    info.channels = 2;
    info.bits_per_sample = 16;
  }

  audio_tools::AudioInfo defaultConfig() {
    audio_tools::AudioInfo result(44100, 2, 16);
    return result;
  }

  bool begin(audio_tools::AudioInfo cfg) {
    setAudioInfo(cfg);
    return begin();
  }

  bool begin() override {
    if (info.bits_per_sample != 16 || info.channels < 1 || info.channels > 2) {
      LOGE("Unsupported format: only 16 bits mono or stereo");
      return false;
    }
    return p_synth->begin(info.sample_rate);
  }

  size_t readBytes(uint8_t* data, size_t len) override {
    int16_t* out = (int16_t*)data;
    if (info.channels == 2) {
      size_t frames = len / 4;
      p_synth->render(out, frames);
      return frames * 4;
    }
    // mono: render stereo blocks and mix them down
    size_t frames = len / 2;
    int16_t stereo[2 * TI_BLOCK_SIZE];
    size_t done = 0;
    while (done < frames) {
      size_t n = frames - done > TI_BLOCK_SIZE ? TI_BLOCK_SIZE : frames - done;
      p_synth->render(stereo, n);
      for (size_t j = 0; j < n; j++) {
        out[done + j] = (int16_t)(((int32_t)stereo[2 * j] + stereo[2 * j + 1]) / 2);
      }
      done += n;
    }
    return frames * 2;
  }

  size_t write(const uint8_t* data, size_t len) override { return 0; }

  int available() override { return DEFAULT_BUFFER_SIZE; }

 protected:
  SampleSynthesizer* p_synth;
};

}  // namespace tinyinstruments
