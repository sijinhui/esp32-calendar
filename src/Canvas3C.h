// 黑/白/红三色帧缓冲。所有画面先画到这里，再交给墨水屏或网页预览输出。
#pragma once

#include <Adafruit_GFX.h>

// 颜色值与 GxEPD2 的 GxEPD_WHITE / GxEPD_BLACK / GxEPD_RED 一致
constexpr uint16_t C_WHITE = 0xFFFF;
constexpr uint16_t C_BLACK = 0x0000;
constexpr uint16_t C_RED = 0xF800;

class Canvas3C : public Adafruit_GFX {
 public:
  Canvas3C(int16_t w, int16_t h);
  ~Canvas3C();
  Canvas3C(const Canvas3C&) = delete;
  Canvas3C& operator=(const Canvas3C&) = delete;

  bool ok() const { return black_ && red_; }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void fillScreen(uint16_t color) override;

  // 物理尺寸与物理坐标（不受 setRotation 影响）上的颜色
  int16_t rawWidth() const { return WIDTH; }
  int16_t rawHeight() const { return HEIGHT; }
  uint16_t rawPixel(int16_t x, int16_t y) const;

  // 每个平面 1 bit/像素，行优先、高位在前，置 1 表示该颜色（与 Adafruit_GFX::drawBitmap 格式相同）
  const uint8_t* blackPlane() const { return black_; }
  const uint8_t* redPlane() const { return red_; }
  size_t planeSize() const { return stride_ * HEIGHT; }

  uint32_t checksum() const;  // 用来判断画面是否变化，避免墨水屏无谓刷新

 private:
  size_t stride_;
  uint8_t* black_;
  uint8_t* red_;
};
