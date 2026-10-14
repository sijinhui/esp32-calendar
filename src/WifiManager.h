// WiFi 管理：连接路由器、断线重连，以及配网模式（设备开热点 + 强制门户，同时蓝牙接受配置）
//
// 省电模式（DEBUG_MODE=0）下 WiFi 平时关闭，只在这些时候打开：开机后 / 短按 BOOT 键后 / 配网成功后的
// WIFI_WINDOW_S 秒（openWindow），同步数据时（holdForSync），配网模式下。开发模式下一直联网。
//
// 什么时候进入配网模式：
//   - 还没配置过 WiFi
//   - 长按 BOOT 键（手动，持续 10 分钟或配网成功为止）
//   - 仅开发模式：已保存的 WiFi 连不上超过 WIFI_PROVISION_AFTER_S 秒（期间继续定期重试，连上后自动退出）。
//     省电模式下连不上就等下次同步再试，不自动开热点
#pragma once

#include <Arduino.h>

#include <functional>
#include <string>
#include <vector>

namespace wifi {

void begin();  // 不阻塞：有配置就开始连接，没配置直接进入配网模式
void loop();   // 放在主循环里

bool configured();  // 是否保存过 WiFi
bool connected();
std::string ip();

void openWindow(uint32_t ms);  // 至少保持联网 ms 毫秒（可以叠加，取最晚的）
void holdForSync(bool hold);   // 同步数据期间保持联网
bool radioOn();                // WiFi 射频是否打开（关着的时候主程序可以睡眠）
// WiFi 打开后 / 关闭前回调（true = 已打开，false = 马上关闭）：关闭前必须关掉所有网络连接
void onPower(const std::function<void(bool on)>& hook);

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
