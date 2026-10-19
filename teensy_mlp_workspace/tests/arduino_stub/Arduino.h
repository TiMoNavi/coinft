#pragma once

#include <stddef.h>
#include <stdint.h>

class HardwareSerial {
 public:
  void begin(unsigned long baud);
  int available();
  int read();
  size_t write(uint8_t value);
  size_t write(const uint8_t* data, size_t length);
};

class UsbSerial : public HardwareSerial {
 public:
  operator bool() const;
};

extern UsbSerial Serial;
extern HardwareSerial Serial1;
extern HardwareSerial Serial5;

uint32_t micros();
