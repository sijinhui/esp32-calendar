// 日志：同时输出到原生 USB 口（USB CDC）和 UART0，开发板上两个 USB 口插哪个都能看到
#pragma once

#include <Arduino.h>

class DualLog : public Print {
 public:
  void begin(unsigned long baud) {
    Serial.begin(baud);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.begin(baud);
#endif
  }

  // 睡眠前把缓冲区里的日志发完，否则醒来后串口会乱码
  void flushAll() {
    Serial.flush();
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.flush();
#endif
  }

  size_t write(uint8_t c) override { return write(&c, 1); }

  size_t write(const uint8_t* buf, size_t n) override {
    Serial.write(buf, n);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial0.write(buf, n);
#endif
    return n;
  }
};

extern DualLog Log;

#define LOGF(fmt, ...) Log.printf("[%7.1f] " fmt "\n", millis() / 1000.0, ##__VA_ARGS__)
