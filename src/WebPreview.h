// 设备上的网页和接口（HTTP 80 端口）
//   /         实时查看屏幕画面（没有墨水屏时的“虚拟屏幕”），预览任意日期，手动同步/刷屏
//   /setup    设置 WiFi 和日历订阅；配网模式下手机连上热点会自动弹出这个页面
//   /api/*    JSON 接口，网页和微信小程序共用
// 局域网访问 http://epaper-calendar.local/ 或 http://<设备 IP>/；配网热点里访问 http://192.168.4.1/
#pragma once

#include <Arduino.h>

#include <functional>

#include "Canvas3C.h"

namespace web {

struct Hooks {
  std::function<void(int y, int m, int d)> preview;  // 预览指定日期；y == 0 表示回到今天
  std::function<void()> sync;                        // 立即同步数据并重绘
  std::function<void()> pushEpd;                     // 把当前画面刷到墨水屏
  std::function<String()> statusJson;                // 运行状态
};

void begin(const Canvas3C& canvas, const Hooks& hooks);
void loop();
// WiFi 关闭前 / 打开后调用：关掉 / 重新打开监听端口和 mDNS（WiFi 驱动释放前，连接必须都关掉）
void pause();
void resume();

}  // namespace web
