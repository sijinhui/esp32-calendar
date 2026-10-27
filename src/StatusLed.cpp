#include "StatusLed.h"

#include <Arduino.h>

#include "config.h"

namespace led {
namespace {

// 只有 task() 会写灯，其他地方只改这几个标志
volatile bool syncing = false;
volatile bool provisioning = false;
volatile bool resultPending = false;  // 同步结果还没轮到显示（比如又开始同步了）
volatile bool resultOk = false;

volatile uint32_t doneUntil = 0;  // 结果灯亮到什么时候（millis），0 = 没在亮
bool doneOk = false;
volatile uint32_t shown = 0xFFFFFFFF;

uint8_t scale(uint8_t v) { return (uint16_t(v) * RGB_LED_BRIGHTNESS + 127) / 255; }

void write(uint8_t r, uint8_t g, uint8_t b) {
  const uint32_t rgb = (uint32_t(r) << 16) | (uint32_t(g) << 8) | b;
  if (rgb == shown) return;
  shown = rgb;
#if STATUS_LED_MONO
  pinMode(RGB_LED_PIN, OUTPUT);
  digitalWrite(RGB_LED_PIN, rgb ? LOW : HIGH);  // 低电平亮
#elif ESP_ARDUINO_VERSION_MAJOR >= 3
  rgbLedWrite(RGB_LED_PIN, scale(r), scale(g), scale(b));
#else
  neopixelWrite(RGB_LED_PIN, scale(r), scale(g), scale(b));
#endif
}

void refresh() {
  const bool blinkOn = (millis() / RGB_LED_BLINK_MS) % 2 == 0;
  if (syncing) {
    if (blinkOn) write(0, 0, 255);
    else write(0, 0, 0);
    return;
  }
  if (resultPending) {
    resultPending = false;
    doneOk = resultOk;
    doneUntil = millis() + RGB_LED_DONE_MS;
    if (!doneUntil) doneUntil = 1;
  }
  if (doneUntil && int32_t(millis() - doneUntil) < 0) {
    if (doneOk) write(0, 255, 0);
    else write(255, 0, 0);
  } else if (provisioning) {
    doneUntil = 0;
    if (blinkOn) write(255, 100, 0);
    else write(0, 0, 0);
  } else {
    doneUntil = 0;
    write(0, 0, 0);
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

void showResult(bool ok) {
  resultOk = ok;
  resultPending = true;
}

bool idle() { return !syncing && !provisioning && !resultPending && !doneUntil && shown == 0; }

}  // namespace led
