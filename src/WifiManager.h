// WiFi 管理：连接路由器、断线重连，以及配网模式（设备开热点 + 强制门户，同时蓝牙接受配置）
//
// 什么时候进入配网模式：
//   - 还没配置过 WiFi
//   - 已保存的 WiFi 连不上超过 WIFI_PROVISION_AFTER_S 秒（期间继续定期重试，连上后自动退出）
//   - 长按 BOOT 键（手动，持续 10 分钟或配网成功为止）
#pragma once

#include <Arduino.h>

#include <string>
#include <vector>

namespace wifi {

void begin();  // 不阻塞：有配置就开始连接，没配置直接进入配网模式
void loop();   // 放在主循环里

bool configured();  // 是否保存过 WiFi
bool connected();
std::string ip();

void startProvisioning(bool manual);
bool provisioning();
bool provisioningManual();
const std::string& apName();  // 热点和蓝牙的名称，例如 EPaper-7D24
std::string apIp();

// 尝试连接新的 WiFi：连上后才保存；失败则继续用原来的配置
void tryConnect(const String& ssid, const String& password);
enum class AttemptState { kIdle, kConnecting, kConnected, kFailed };
struct Attempt {
  AttemptState state = AttemptState::kIdle;
  std::string ssid;
  std::string error;
};
const Attempt& attempt();
const char* stateName(AttemptState s);
std::string lastError();  // 已保存的 WiFi 最近一次连接失败的原因（配网页面 / 屏幕上提示用）

// 扫描附近的 WiFi（异步，结果按信号强弱排序、同名合并）
struct Ap {
  std::string ssid;
  int rssi;
  bool secure;
  int channel;  // 信号最强的那个 AP 所在的信道
};
void startScan();
bool scanning();
const std::vector<Ap>& scanResults();

}  // namespace wifi
