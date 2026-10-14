#include "Canvas3C.h"

#include <stdlib.h>
#include <string.h>

Canvas3C::Canvas3C(int16_t w, int16_t h)
    : Adafruit_GFX(w, h),
      stride_((w + 7) / 8),
      black_(static_cast<uint8_t*>(calloc(stride_ * h, 1))),
      red_(static_cast<uint8_t*>(calloc(stride_ * h, 1))) {}

Canvas3C::~Canvas3C() {
  free(black_);
  free(red_);
}

void Canvas3C::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (!ok() || x < 0 || y < 0 || x >= _width || y >= _height) return;
  int16_t t;
  switch (rotation) {
    case 1:
      t = x;
      x = WIDTH - 1 - y;
      y = t;
      break;
    case 2:
      x = WIDTH - 1 - x;
      y = HEIGHT - 1 - y;
      break;
    case 3:
      t = x;
      x = y;
      y = HEIGHT - 1 - t;
      break;
  }
  const size_t i = y * stride_ + (x >> 3);
  const uint8_t bit = 0x80 >> (x & 7);
  if (color == C_WHITE) {
    black_[i] &= ~bit;
    red_[i] &= ~bit;
  } else if (color == C_RED) {
    black_[i] &= ~bit;
    red_[i] |= bit;
  } else {
    black_[i] |= bit;
    red_[i] &= ~bit;
  }
}

void Canvas3C::fillScreen(uint16_t color) {
  if (!ok()) return;
  memset(black_, color == C_BLACK ? 0xFF : 0x00, planeSize());
  memset(red_, color == C_RED ? 0xFF : 0x00, planeSize());
}

uint16_t Canvas3C::rawPixel(int16_t x, int16_t y) const {
  if (!ok() || x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) return C_WHITE;
  const size_t i = y * stride_ + (x >> 3);
  const uint8_t bit = 0x80 >> (x & 7);
  if (red_[i] & bit) return C_RED;
  if (black_[i] & bit) return C_BLACK;
  return C_WHITE;
}

uint32_t Canvas3C::checksum() const {
  uint32_t h = 2166136261u;  // FNV-1a
  for (const uint8_t* plane : {black_, red_}) {
    for (size_t i = 0; plane && i < planeSize(); i++) h = (h ^ plane[i]) * 16777619u;
  }
  return h;
}
