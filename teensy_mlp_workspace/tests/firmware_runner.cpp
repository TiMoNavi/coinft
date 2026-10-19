#include <iomanip>
#include <iostream>
#include <string>

#include "calibration.h"
#include "inference.h"
#include "output_protocol.h"

int main() {
  std::string mode;
  coinft::CoinFTCalibration calibration;
  while (std::cin >> mode) {
    if (mode == "I") {
      std::string side;
      float input[12];
      float output[6];
      std::cin >> side;
      for (float& value : input) std::cin >> value;
      const coinft::Model& model = side == "left" ? coinft::leftModel() : coinft::rightModel();
      if (!coinft::infer(model, input, output)) return 2;
      std::cout << std::setprecision(9);
      for (size_t index = 0; index < 6; ++index) {
        if (index) std::cout << ' ';
        std::cout << output[index];
      }
      std::cout << '\n';
    } else if (mode == "P") {
      unsigned int status_value;
      unsigned int valid_value;
      uint32_t sequence;
      uint32_t time_us;
      float left[6];
      float right[6];
      std::cin >> status_value >> valid_value >> sequence >> time_us;
      for (float& value : left) std::cin >> value;
      for (float& value : right) std::cin >> value;
      uint8_t frame[coinft::kOutputFrameSize];
      coinft::encodeOutputFrame(frame, sequence, time_us, static_cast<uint8_t>(status_value), valid_value != 0, left, right);
      for (size_t index = 0; index < sizeof(frame); ++index) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<unsigned int>(frame[index]);
      }
      std::cout << std::dec << '\n';
    } else if (mode == "S") {
      uint32_t timestamp_us;
      uint16_t left[12];
      uint16_t right[12];
      std::cin >> timestamp_us;
      for (uint16_t& value : left) std::cin >> value;
      for (uint16_t& value : right) std::cin >> value;
      coinft::CalibrationOutput output;
      calibration.process(left, right, timestamp_us, &output);
      std::cout << static_cast<unsigned int>(output.status) << ' '
                << (output.has_wrench ? 1 : 0) << ' ' << std::setprecision(9);
      for (size_t axis = 0; axis < 6; ++axis) {
        std::cout << output.left_wrench[axis] << ' ';
      }
      for (size_t axis = 0; axis < 6; ++axis) {
        std::cout << output.right_wrench[axis] << (axis == 5 ? '\n' : ' ');
      }
    } else if (mode == "B") {
      uint32_t sample_count;
      uint32_t interval_us;
      std::cin >> sample_count >> interval_us;
      calibration.reset();
      uint16_t left[12];
      uint16_t right[12];
      for (size_t channel = 0; channel < 12; ++channel) {
        left[channel] = static_cast<uint16_t>(1000 + channel);
        right[channel] = static_cast<uint16_t>(1200 + channel);
      }
      coinft::CalibrationOutput output;
      for (uint32_t sample = 0; sample < sample_count; ++sample) {
        calibration.process(left, right, sample * interval_us, &output);
      }
      std::cout << static_cast<unsigned int>(output.status) << ' '
                << (output.has_wrench ? 1 : 0) << '\n';
    } else {
      return 3;
    }
  }
  return 0;
}
