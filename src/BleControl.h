// 蓝牙通道：给微信小程序配网用（小程序里切换到设备热点再访问 192.168.4.1 很不可靠，蓝牙最稳）
// 为了省电，蓝牙只在配网模式下打开；配网结束、手机断开后关闭（手机一直连着的话最多再留 1 分钟）。
//
// 一个自定义 GATT 服务，两个特征值：
//   RX（手机写入）：UTF-8 JSON 命令，以 '\n' 结尾，可以拆成多次写入（每次 20 字节也行）
//   TX（通知）    ：JSON 回复，以 '\n' 结尾，按 MTU 分包，手机端拼起来再解析
// 命令：
//   {"cmd":"status"}                          → 设备状态（字段同 /api/status）
//   {"cmd":"scan"}                            → {"cmd":"scan","aps":[{"ssid","rssi","secure"}…]}
//   {"cmd":"wifi","ssid":"…","password":"…"}  → 先回 state=connecting，连上回 state=connected（带 ip），
//                                                失败回 state=failed（带 error）
//   {"cmd":"ics","urls":["https://…"]}        → {"cmd":"ics","ok":true}，之后设备开始同步日历
// 出错时回复 {"cmd":"…","error":"…"}。
// wifi / ics 只在配网模式下接受；蓝牙连上时设备处于配网模式的话，这次连接期间一直可以改。
//
// 广播：主广播包带服务 UUID（小程序按它过滤），扫描响应带设备名和厂商数据：
//   FF FF 'E' 'P' 版本(1) 标志(bit0 配网模式, bit1 已连 WiFi) IPv4(4 字节)
// 小程序扫描一下就能拿到设备 IP，之后走局域网 HTTP（iOS 微信用不了 mDNS，没法用 .local 域名）。
// 平时蓝牙关着，设备 IP 变了的话，长按 BOOT 键进入配网模式，小程序就能重新搜到新 IP。
#pragma once

#include <ArduinoJson.h>

#include <functional>
#include <string>

namespace ble {

struct Hooks {
  std::function<void(JsonDocument&)> status;  // 填写设备状态
  std::function<void()> icsChanged;           // 日历订阅改了：同步并重绘
};

void begin(const std::string& name, const Hooks& hooks);  // 只记下名称和回调，蓝牙由 loop() 按需打开
bool active();  // 蓝牙是否开着（开着时不能睡眠）
void loop();  // 在主循环里：按配网状态打开 / 关闭蓝牙，处理收到的命令、更新广播内容

}  // namespace ble
