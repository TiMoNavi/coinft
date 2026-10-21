#include <Arduino.h>

#include "calibration.h"
#include "output_protocol.h"

void setup();
void loop();

namespace {

constexpr uint32_t kSensorBaud = 1000000;
constexpr uint32_t kUsbBaud = 115200;
constexpr uint8_t kFrameStart = 0x02;
constexpr uint8_t kFrameEnd = 0x03;
constexpr size_t kSensorPayloadSize = 24;
constexpr uint32_t kMaxSampleSkewUs = 10000;
constexpr uint32_t kMinOutputIntervalUs = 1000;

struct SensorReader {
  HardwareSerial& port;
  uint8_t state = 0;
  size_t payload_index = 0;
  uint8_t payload[kSensorPayloadSize] = {};
  uint16_t channels[12] = {};
  uint32_t received_at_us = 0;
  bool ready = false;

  explicit SensorReader(HardwareSerial& serial_port) : port(serial_port) {}

  void poll() {
    while (port.available() > 0) {
      const int next = port.read();
      if (next < 0) return;
      const uint8_t value = static_cast<uint8_t>(next);
      if (state == 0) {
        if (value == kFrameStart) {
          payload_index = 0;
          state = 1;
        }
      } else if (state == 1) {
        payload[payload_index++] = value;
        if (payload_index == kSensorPayloadSize) state = 2;
      } else {
        if (value == kFrameEnd) {
          for (size_t index = 0; index < 12; ++index) {
            channels[index] = static_cast<uint16_t>(payload[index * 2]) |
                              (static_cast<uint16_t>(payload[index * 2 + 1]) << 8);
          }
          received_at_us = micros();
          ready = true;
        }
        state = value == kFrameStart ? 1 : 0;
        payload_index = 0;
      }
    }
  }
};

SensorReader rightSensor(Serial1);
SensorReader leftSensor(Serial5);
uint32_t sequence = 0;
uint32_t last_output_us = 0;
bool has_output_timestamp = false;
bool streaming = false;
coinft::CoinFTCalibration calibration;

void forwardCommand(char command) {
  Serial1.write(static_cast<uint8_t>(command));
  Serial5.write(static_cast<uint8_t>(command));
}

void pollHostCommands() {
  while (Serial.available() > 0) {
    const char command = static_cast<char>(Serial.read());
    if (command == 'i') {
      streaming = false;
      rightSensor.ready = false;
      leftSensor.ready = false;
      forwardCommand(command);
    } else if (command == 's') {
      rightSensor.ready = false;
      leftSensor.ready = false;
      forwardCommand(command);
      calibration.begin();
      has_output_timestamp = false;
      streaming = true;
    } else if (command == 't') {
      forwardCommand(command);
    }
  }
}

void emitIfBothFresh() {
  if (!streaming || !Serial || !rightSensor.ready || !leftSensor.ready) return;

  const uint32_t now_us = micros();
  if (static_cast<uint32_t>(now_us - rightSensor.received_at_us) > kMaxSampleSkewUs) {
    rightSensor.ready = false;
    return;
  }
  if (static_cast<uint32_t>(now_us - leftSensor.received_at_us) > kMaxSampleSkewUs) {
    leftSensor.ready = false;
    return;
  }
  if (has_output_timestamp &&
      static_cast<uint32_t>(now_us - last_output_us) < kMinOutputIntervalUs) {
    return;
  }

  coinft::CalibrationOutput output;
  const uint32_t timestamp_us = now_us;
  calibration.process(leftSensor.channels, rightSensor.channels, timestamp_us,
                      &output);
  leftSensor.ready = false;
  rightSensor.ready = false;
  last_output_us = timestamp_us;
  has_output_timestamp = true;

  uint8_t frame[coinft::kOutputFrameSize];
  coinft::encodeOutputFrame(frame, sequence++, timestamp_us, output.status,
                            output.has_wrench, output.zero_check_warning,
                            output.left_wrench,
                            output.right_wrench);
  Serial.write(frame, sizeof(frame));
}

}  // namespace

void setup() {
  Serial1.begin(kSensorBaud);  // Right CoinFT: TX1/RX1.
  Serial5.begin(kSensorBaud);  // Left CoinFT: TX5/RX5.
  Serial.begin(kUsbBaud);
}

void loop() {
  pollHostCommands();
  rightSensor.poll();
  leftSensor.poll();
  emitIfBothFresh();
}
