// 把 Canvas3C 编码成 4 位调色板 BMP（浏览器、macOS 预览都能直接打开）
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Canvas3C.h"

namespace bmp {

constexpr size_t kHeaderSize = 14 + 40 + 16 * 4;  // 文件头 + 信息头 + 16 色调色板

inline size_t rowBytes(const Canvas3C& c) { return ((c.rawWidth() * 4 + 31) / 32) * 4; }
inline size_t fileSize(const Canvas3C& c) { return kHeaderSize + rowBytes(c) * c.rawHeight(); }

// sink(const uint8_t* data, size_t len)；按物理方向输出（不受 setRotation 影响）
template <typename Sink>
void write(const Canvas3C& c, Sink sink) {
  const int w = c.rawWidth(), h = c.rawHeight();
  const size_t row = rowBytes(c);
  const uint32_t size = static_cast<uint32_t>(fileSize(c));

  uint8_t hdr[kHeaderSize] = {};
  auto put16 = [&](size_t at, uint16_t v) {
    hdr[at] = v & 0xff;
    hdr[at + 1] = v >> 8;
  };
  auto put32 = [&](size_t at, uint32_t v) {
    for (int i = 0; i < 4; i++) hdr[at + i] = (v >> (8 * i)) & 0xff;
  };
  hdr[0] = 'B';
  hdr[1] = 'M';
  put32(2, size);
  put32(10, kHeaderSize);
  put32(14, 40);
  put32(18, w);
  put32(22, h);  // 正数 = 自下而上存储
  put16(26, 1);
  put16(28, 4);  // 4 bpp
  put32(34, row * h);
  put32(38, 2835);
  put32(42, 2835);
  // 调色板（B, G, R, 0）：0 白、1 黑、2 红（颜色模拟墨水屏的观感）
  const uint8_t palette[3][4] = {{0xF2, 0xF4, 0xF4, 0}, {0x22, 0x22, 0x22, 0}, {0x2A, 0x2A, 0xC8, 0}};
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 4; j++) hdr[54 + i * 4 + j] = palette[i][j];
  }
  sink(hdr, sizeof hdr);

  uint8_t line[512];  // 足够 1024 像素宽
  for (int y = h - 1; y >= 0; y--) {
    for (size_t i = 0; i < row; i++) line[i] = 0;
    for (int x = 0; x < w; x++) {
      const uint16_t px = c.rawPixel(x, y);
      const uint8_t idx = px == C_RED ? 2 : px == C_BLACK ? 1 : 0;
      line[x / 2] |= (x & 1) ? idx : idx << 4;
    }
    sink(line, row);
  }
}

}  // namespace bmp
