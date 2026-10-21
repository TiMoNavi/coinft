#pragma once

#include <stddef.h>
#include <stdint.h>

namespace coinft {

constexpr size_t kOutputFrameSize = 65;
constexpr uint8_t kOutputProtocolVersion = 2;

uint16_t crc16CcittFalse(const uint8_t* data, size_t length);
void encodeOutputFrame(uint8_t frame[kOutputFrameSize], uint32_t sequence,
                       uint32_t teensy_time_us, uint8_t calibration_status,
                       bool wrench_valid, bool zero_check_warning,
                       const float left[6],
                       const float right[6]);

}  // namespace coinft
