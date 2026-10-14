#include "StatusLed.h"

#include <Arduino.h>

#include "config.h"

namespace led {
namespace {

// 只有 task() 会写灯，其他地方只改这几个标志
volatile bool syncing = false;
volatile bool provisioning = false;
volatile bool flashPending = false;  // 联网成功后要亮绿灯，但还没轮到显示（比如正在同步）

uint32_t doneUntil = 0;  // 绿灯亮到什么时候（millis），0 = 没在亮
uint32_t shown = 0xFFFFFFFF;

uint8_t scale(uint8_t v) { return (uint16_t(v) * RGB_LED_BRIGHTNESS + 127) / 255; }

void write(uint8_t r, uint8_t g, uint8_t b) {
  const uint32_t rgb = (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
  if (rgb == shown) return;
  shown = rgb;
  neopixelWrite(RGB_LED_PIN, scale(r), scale(g), scale(b));
}

void refresh() {
  const bool blinkOn = (millis() / RGB_LED_BLINK_MS) % 2 == 0;
  if (syncing) {
    if (blinkOn) write(0, 0, 255);
    else write(0, 0, 0);
  } else if (provisioning) {
    if (blinkOn) write(255, 100, 0);
    else write(0, 0, 0);
  } else {
    if (flashPending) {
      flashPending = false;
      doneUntil = millis() + RGB_LED_DONE_MS;
      if (!doneUntil) doneUntil = 1;
    }
    if (doneUntil && int32_t(millis() - doneUntil) < 0) {
      write(0, 255, 0);
    } else {
      doneUntil = 0;
      write(0, 0, 0);
    }
  }
}

void task(void*) {
  for (;;) {
    refresh();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

}  // namespace

void begin() {
  refresh();
  xTaskCreate(task, "status_led", 3072, nullptr, 1, nullptr);
}

void setSyncing(bool on) { syncing = on; }

void setProvisioning(bool on) { provisioning = on; }

void flashConnected() { flashPending = true; }

}  // namespace led
