#include "StatusLed.h"

#include <Arduino.h>

#include "config.h"

namespace led {
namespace {

bool syncing = false;
bool provisioning = false;
bool flashPending = false;  // 联网成功后要闪绿灯，但还没轮到显示（比如正在同步）
uint32_t flashUntil = 0;    // 绿灯亮到什么时候（millis），0 = 没在亮
uint32_t shown = 0xFFFFFFFF;

uint8_t scale(uint8_t v) { return (uint16_t(v) * RGB_LED_BRIGHTNESS + 127) / 255; }

void write(uint8_t r, uint8_t g, uint8_t b) {
  const uint32_t rgb = (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
  if (rgb == shown) return;
  shown = rgb;
  neopixelWrite(RGB_LED_PIN, scale(r), scale(g), scale(b));
}

void refresh() {
  if (syncing) {
    write(0, 0, 255);
  } else if (provisioning) {
    write(255, 100, 0);
  } else {
    if (flashPending) {
      flashPending = false;
      flashUntil = millis() + RGB_LED_DONE_MS;
      if (!flashUntil) flashUntil = 1;
    }
    if (flashUntil && int32_t(millis() - flashUntil) < 0) {
      write(0, 255, 0);
    } else {
      flashUntil = 0;
      write(0, 0, 0);
    }
  }
}

}  // namespace

void begin() { refresh(); }

void loop() { refresh(); }

void setSyncing(bool on) {
  syncing = on;
  refresh();
}

void setProvisioning(bool on) {
  provisioning = on;
  refresh();
}

void flashConnected() {
  flashPending = true;
  refresh();
}

}  // namespace led
