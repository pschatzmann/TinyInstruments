#pragma once
#include <stdint.h>

#include "Config.h"

TI_OPTIMIZE_BEGIN

namespace tinyinstruments {

/**
 * @brief Streaming IMA ADPCM decoder for mono data: 4 bits per sample, the
 * low nibble of a byte is the first sample. The decoder can be positioned
 * at any sample if the decoder state (predictor and step index) for that
 * position is known (e.g. at the start of a loop).
 */
class ImaAdpcmDecoder {
 public:
  /// Starts decoding at the indicated sample position with the decoder state
  void begin(const uint8_t* data, uint32_t position = 0, int16_t predictor = 0,
             uint8_t index = 0) {
    p_data = data;
    seek(position, predictor, index);
  }

  /// Continue decoding at a different position with the provided state
  void seek(uint32_t position, int16_t predictor, uint8_t index) {
    pos = position;
    pred = predictor;
    step_index = index > 88 ? 88 : index;
  }

  /// Decodes the next sample
  inline int16_t next() {
    uint8_t byte = p_data[pos >> 1];
    uint8_t code = (pos & 1) ? (byte >> 4) : (byte & 0x0F);
    pos++;
    int step = stepTable()[step_index];
    int delta = step >> 3;
    if (code & 4) delta += step;
    if (code & 2) delta += step >> 1;
    if (code & 1) delta += step >> 2;
    int value = (code & 8) ? pred - delta : pred + delta;
    if (value > 32767) value = 32767;
    if (value < -32768) value = -32768;
    pred = (int16_t)value;
    int idx = step_index + indexTable()[code];
    step_index = idx < 0 ? 0 : (idx > 88 ? 88 : idx);
    return pred;
  }

  /// Position of the next sample which will be decoded
  uint32_t position() const { return pos; }
  int16_t predictor() const { return pred; }
  uint8_t stepIndex() const { return step_index; }

  static const int16_t* stepTable() {
    static const int16_t table[89] = {
        7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
        19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
        50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
        130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
        337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
        876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
        2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
        5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
    return table;
  }

  static const int8_t* indexTable() {
    static const int8_t table[16] = {-1, -1, -1, -1, 2, 4, 6, 8,
                                     -1, -1, -1, -1, 2, 4, 6, 8};
    return table;
  }

 protected:
  const uint8_t* p_data = nullptr;
  uint32_t pos = 0;
  int16_t pred = 0;
  uint8_t step_index = 0;
};

}  // namespace tinyinstruments

TI_OPTIMIZE_END
