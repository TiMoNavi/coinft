#include "output_protocol.h"

#include <limits>
#include <string.h>

namespace coinft {
namespace {

void writeU32Le(uint8_t* out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
  out[2] = static_cast<uint8_t>(value >> 16);
  out[3] = static_cast<uint8_t>(value >> 24);
}

void writeFloatLe(uint8_t* out, float value) {
  uint32_t bits = 0;
  static_assert(sizeof(bits) == sizeof(value), "float32 is required");
  static_assert(std::numeric_limits<float>::is_iec559, "IEEE-754 float32 is required");
  memcpy(&bits, &value, sizeof(bits));
  writeU32Le(out, bits);
}

}  // namespace

uint16_t crc16CcittFalse(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < length; ++index) {
    crc ^= static_cast<uint16_t>(data[index]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

void encodeOutputFrame(uint8_t frame[kOutputFrameSize], uint32_t sequence,
                       uint32_t teensy_time_us, uint8_t calibration_status,
                       bool wrench_valid, const float left[6],
                       const float right[6]) {
  frame[0] = 'C';
  frame[1] = 'F';
  frame[2] = 'T';
  frame[3] = 'F';
  frame[4] = kOutputProtocolVersion;
  frame[5] = calibration_status;
  frame[6] = wrench_valid ? 0x01 : 0x00;
  writeU32Le(frame + 7, sequence);
  writeU32Le(frame + 11, teensy_time_us);
  for (size_t index = 0; index < 6; ++index) {
    writeFloatLe(frame + 15 + index * 4, left[index]);
    writeFloatLe(frame + 39 + index * 4, right[index]);
  }
  const uint16_t crc = crc16CcittFalse(frame + 4, 59);
  frame[63] = static_cast<uint8_t>(crc);
  frame[64] = static_cast<uint8_t>(crc >> 8);
}

}  // namespace coinft
