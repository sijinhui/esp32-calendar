// 在电脑上编译 Adafruit_GFX / U8g2_for_Adafruit_GFX 所需的最小 Arduino 替身（仅供 tools/preview.sh 使用）
#pragma once

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#define PROGMEM
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define radians(deg) ((deg) * DEG_TO_RAD)
typedef bool boolean;
typedef uint8_t byte;

class __FlashStringHelper;

class String {
 public:
  String(const char* s = "") : s_(s ? s : "") {}
  unsigned int length() const { return static_cast<unsigned int>(s_.size()); }
  const char* c_str() const { return s_.c_str(); }

 private:
  std::string s_;
};

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* buf, size_t n) {
    size_t written = 0;
    while (n--) written += write(*buf++);
    return written;
  }
  size_t print(const char* s) { return write(reinterpret_cast<const uint8_t*>(s), strlen(s)); }
};
