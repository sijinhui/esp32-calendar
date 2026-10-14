// 把 Canvas3C 的内容刷到墨水屏。config.h 里 EPD_ENABLED=0 时什么都不做（屏幕没到也能跑）
#pragma once

#include "Canvas3C.h"

namespace epd {

bool begin();
bool enabled();
// 全屏刷新（三色屏一次约 15~20 秒，期间会闪烁，属正常现象），完成后让屏幕进入休眠
void show(const Canvas3C& canvas);

}  // namespace epd
