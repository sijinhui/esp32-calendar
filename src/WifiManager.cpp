#include "WifiManager.h"

#include <DNSServer.h>
#include <WiFi.h>

#include <algorithm>
#include <atomic>
#include <cstring>

#include "Log.h"
#include "Settings.h"
#include "config.h"

namespace wifi {

namespace {

constexpr uint32_t kTryTimeoutMs = 15000;         // 新 WiFi 多久连不上算失败
constexpr uint32_t kRetryMs = 15000;              // 断线后的重连间隔
constexpr uint32_t kRetryProvisioningMs = 60000;  // 配网模式下的重连间隔（重连要扫描信道，热点会短暂掉线）
constexpr uint32_t kManualMs = 10 * 60 * 1000;    // 手动配网模式持续多久
constexpr uint32_t kFastTimeoutMs = 4000;         // 快速重连多久没连上就改为完整扫描

DNSServer dns;
std::string name;
bool apOn = false;
bool manual = false;
uint32_t apHoldUntil = 0;  // 这之前即使连上了路由器也不关热点（手动配网时用）
uint32_t upSince = 0, downSince = 0, lastBegin = 0;
bool wasUp = false;

// 省电模式：WiFi 只在有人需要时打开
bool powered = false;       // WiFi 射频是否打开
std::function<void(bool)> powerHook;
uint32_t windowUntil = 0;   // 这之前保持联网（开机后、短按 BOOT 键、配网成功后），网页 / 小程序可以访问
bool syncHold = false;      // 主程序要同步数据

// 快速重连：记住上次连上的路由器（BSSID）和信道，下次直接连，省掉扫描全部信道的一两秒
String fastSsid;
uint8_t fastBssid[6];
int32_t fastChannel = 0;
bool fastBegin = false;     // 这次连接是不是快速重连

bool trying = false;
String trySsid, tryPassword;
uint32_t tryStart = 0;
uint32_t gotIpAtTry = 0;
Attempt last;

// WiFi 事件在系统任务里回调
std::atomic<uint8_t> disconnectReason{0};
std::atomic<uint32_t> gotIpCount{0};

bool scanWanted = false, scanRunning = false;
uint32_t lastScanTry = 0;
std::vector<Ap> aps;

std::string reasonText(uint8_t reason) {
  switch (reason) {
    case 0:
      return "连接超时";
    case WIFI_REASON_NO_AP_FOUND:
#if CONFIG_SOC_WIFI_SUPPORT_5G
      return "找不到这个 WiFi（检查名字和外接天线）";
#else
      return "找不到这个 WiFi（设备只支持 2.4GHz）";
#endif
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "密码错误";
    case WIFI_REASON_AUTH_EXPIRE:
      return "认证超时（密码错误或信号太弱）";
    default:
      return "连接失败（原因 " + std::to_string(reason) + "）";
  }
}

void beginStation(const String& ssid, const String& password) {
  WiFi.disconnect();
  disconnectReason = 0;
  fastBegin = fastChannel > 0 && ssid == fastSsid;
  if (fastBegin) {
    WiFi.begin(ssid.c_str(), password.c_str(), fastChannel, fastBssid);
  } else {
    WiFi.begin(ssid.c_str(), password.c_str());
  }
  lastBegin = millis();
}

// 重新开始连接：正在试新 WiFi 就连新的，否则连保存的
void restartStation() {
  if (trying) {
    beginStation(trySsid, tryPassword);
  } else if (configured()) {
    beginStation(settings::wifiSsid(), settings::wifiPassword());
  }
}

bool wanted(uint32_t now) {
  return DEBUG_MODE || apOn || trying || syncHold || static_cast<int32_t>(windowUntil - now) > 0;
}

void powerOn() {
  powered = true;
  WiFi.mode(apOn ? WIFI_AP_STA : WIFI_STA);
  if (powerHook) powerHook(true);
  downSince = millis();
  restartStation();
}

// 关掉射频并释放 WiFi 驱动（下次 powerOn 时重新初始化，大约 0.1 秒）。
// 驱动释放后，协议栈里还没处理完的数据包（比如刚建立的网页连接）再被释放就会崩溃，
// 所以先让主程序关掉网页服务等所有连接，再等协议栈处理完已经收到的包。
// （只用 esp_wifi_stop() 停射频、保留驱动也不行：实测之后浅睡眠醒不过来）
void powerOff() {
  if (powerHook) powerHook(false);
  WiFi.disconnect();
  delay(100);
  WiFi.mode(WIFI_OFF);
  powered = false;
  wasUp = false;
  fastBegin = false;
  LOGF("WiFi 已关闭（省电）");
}

void stopProvisioning() {
  dns.stop();
  WiFi.softAPdisconnect(true);  // 同时切回纯 STA 模式，不影响已有的路由器连接
  apOn = false;
  manual = false;
  LOGF("退出配网模式");
}

void pollScan(uint32_t now) {
  if (scanWanted && !scanRunning && now - lastScanTry > 1000) {
    lastScanTry = now;
    // 正在连接路由器时扫描会失败，下一秒再试
    if (WiFi.scanNetworks(true, true) == WIFI_SCAN_RUNNING) {  // 隐藏网络也扫出来，只为在日志里报个数
      scanWanted = false;
      scanRunning = true;
    }
  }
  if (!scanRunning) return;
  const int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;
  scanRunning = false;
  aps.clear();
  int hidden = 0;
  for (int i = 0; i < n; i++) {
    const std::string ssid = WiFi.SSID(i).c_str();
    if (ssid.empty()) {
      hidden++;
      continue;
    }
    auto it = std::find_if(aps.begin(), aps.end(), [&](const Ap& ap) { return ap.ssid == ssid; });
    if (it == aps.end()) {
      aps.push_back({ssid, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN, WiFi.channel(i)});
    } else if (WiFi.RSSI(i) > it->rssi) {  // Mesh / 多个 AP 同名，取最强的
      it->rssi = WiFi.RSSI(i);
      it->channel = WiFi.channel(i);
    }
  }
  WiFi.scanDelete();
  std::sort(aps.begin(), aps.end(), [](const Ap& a, const Ap& b) { return a.rssi > b.rssi; });
  if (aps.size() > 20) aps.resize(20);
  LOGF("扫描到 %u 个 WiFi（另有 %d 个隐藏网络）", aps.size(), hidden);
  for (const Ap& ap : aps) {
    LOGF("  %4d dBm  信道 %2d  %s  「%s」", ap.rssi, ap.channel, ap.secure ? "加密" : "开放", ap.ssid.c_str());
  }
}

}  // namespace

void begin() {
  WiFi.persistent(false);        // WiFi 配置由 Settings 保存，不让驱动再往 NVS 里存一份
  WiFi.setAutoReconnect(false);  // 重连由 loop() 管理
  WiFi.onEvent(
      [](arduino_event_id_t, arduino_event_info_t info) {
        // 自己调用 disconnect() 产生的“主动离开”不算失败原因
        if (info.wifi_sta_disconnected.reason != WIFI_REASON_ASSOC_LEAVE) {
          disconnectReason = info.wifi_sta_disconnected.reason;
        }
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t) { gotIpCount++; }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  powered = true;

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[24];
  snprintf(buf, sizeof buf, "EPaper-%02X%02X", mac[4], mac[5]);
  name = buf;

  downSince = millis();
  if (configured()) {
    LOGF("连接 WiFi「%s」…", settings::wifiSsid().c_str());
    beginStation(settings::wifiSsid(), settings::wifiPassword());
  } else {
    LOGF("还没有配置 WiFi");
    startProvisioning(false);
  }
}

void loop() {
  const uint32_t now = millis();
  if (wanted(now) != powered) {
    // 打开后直接返回：powerOn() 刚把 lastBegin 设成比 now 晚的时间，这一轮再算 now - lastBegin 会下溢
    return powered ? powerOff() : powerOn();
  }
  if (!powered) return;

  if (apOn) dns.processNextRequest();
  pollScan(now);

  const bool up = connected();
  if (up != wasUp) {
    wasUp = up;
    if (up) {
      upSince = now;
      fastSsid = WiFi.SSID();
      fastChannel = WiFi.channel();
      memcpy(fastBssid, WiFi.BSSID(), sizeof fastBssid);
      LOGF("WiFi 已连接「%s」：IP %s，信号 %d dBm，信道 %d，用时 %lu ms%s", WiFi.SSID().c_str(),
           WiFi.localIP().toString().c_str(), WiFi.RSSI(), fastChannel, now - lastBegin,
           fastBegin ? "（快速重连）" : "");
      fastBegin = false;
    } else {
      downSince = now;
      LOGF("WiFi 已断开");
    }
  }

  // 快速重连没成功（路由器换了信道、Mesh 换了节点等）：马上改为扫描全部信道
  if (!up && fastBegin && (disconnectReason != 0 || now - lastBegin > kFastTimeoutMs)) {
    LOGF("快速重连失败（%s），改为扫描信道", reasonText(disconnectReason).c_str());
    fastChannel = 0;
    restartStation();
    return;  // 同上：lastBegin 刚更新，这一轮不再往下算
  }

  if (trying) {
    if (gotIpCount != gotIpAtTry) {
      trying = false;
      settings::setWifi(trySsid, tryPassword);
      last.state = AttemptState::kConnected;
      apHoldUntil = now;  // 配网成功，手动配网模式也可以结束了
      openWindow(WIFI_WINDOW_S * 1000UL);  // 留时间在网页 / 小程序里设置日历
    } else if (now - tryStart > kTryTimeoutMs) {
      trying = false;
      last.state = AttemptState::kFailed;
      last.error = reasonText(disconnectReason);
      LOGF("连接「%s」失败：%s", trySsid.c_str(), last.error.c_str());
      if (configured()) {
        beginStation(settings::wifiSsid(), settings::wifiPassword());  // 继续用原来的配置
      } else {
        WiFi.disconnect();
      }
    }
    return;
  }

  if (up) {
    // 连上了路由器就关闭配网热点；有手机连在热点上时多留一会儿，让配网页面能显示结果
    if (apOn) {
      const uint32_t since = static_cast<int32_t>(apHoldUntil - upSince) > 0 ? apHoldUntil : upSince;
      const uint32_t grace = WiFi.softAPgetStationNum() ? 60000 : 5000;
      if (static_cast<int32_t>(now - since) > static_cast<int32_t>(grace)) stopProvisioning();
    }
    return;
  }

  if (!configured()) return;  // 没有配置：一直保持配网模式

#if DEBUG_MODE
  // 常驻联网：连不上就自动打开配网热点
  if (!apOn && now - downSince > WIFI_PROVISION_AFTER_S * 1000UL) {
    LOGF("「%s」%d 秒没连上（%s）", settings::wifiSsid().c_str(), WIFI_PROVISION_AFTER_S, lastError().c_str());
    startProvisioning(false);
  }
#else
  // 省电模式：连不上不自动开热点（路由器断电时每次同步都开热点太费电），等下次同步再试；
  // 手动配网 10 分钟内没完成就关掉热点
  if (apOn && manual && static_cast<int32_t>(now - apHoldUntil) > 0 && WiFi.softAPgetStationNum() == 0) {
    LOGF("手动配网超时，关闭热点");
    stopProvisioning();
    return;
  }
#endif
  if (apOn && WiFi.softAPgetStationNum() > 0) return;  // 有手机正在配网时不重连，免得热点掉线
  if (now - lastBegin > (apOn ? kRetryProvisioningMs : kRetryMs)) {
    beginStation(settings::wifiSsid(), settings::wifiPassword());
  }
}

void openWindow(uint32_t ms) {
  const uint32_t until = millis() + ms;
  if (static_cast<int32_t>(until - windowUntil) > 0) windowUntil = until;
}

void holdForSync(bool hold) { syncHold = hold; }

void onPower(const std::function<void(bool)>& hook) { powerHook = hook; }

bool radioOn() { return powered; }

bool configured() { return !settings::wifiSsid().isEmpty(); }

bool connected() { return WiFi.status() == WL_CONNECTED; }

std::string ip() { return connected() ? WiFi.localIP().toString().c_str() : ""; }

void startProvisioning(bool isManual) {
  if (isManual) {
    manual = true;
    apHoldUntil = millis() + kManualMs;
  }
  if (apOn) return;

  const bool wasOff = !powered;
  WiFi.mode(WIFI_AP_STA);
  powered = true;
  if (wasOff && powerHook) powerHook(true);
  WiFi.softAP(name.c_str(), strlen(AP_PASSWORD) ? AP_PASSWORD : nullptr);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());  // 所有域名都解析到设备，手机连上热点后会自动弹出设置页
  apOn = true;
  LOGF("进入配网模式：手机连接热点「%s」，或用微信小程序蓝牙配网（设置页 http://%s/）", name.c_str(),
       apIp().c_str());
  startScan();
}

bool provisioning() { return apOn; }

bool provisioningManual() { return apOn && manual; }

const std::string& apName() { return name; }

std::string apIp() { return WiFi.softAPIP().toString().c_str(); }

void tryConnect(const String& ssid, const String& password) {
  last.ssid = ssid.c_str();
  last.error.clear();
  if (ssid.isEmpty() || ssid.length() > 32) {
    last.state = AttemptState::kFailed;
    last.error = "WiFi 名称无效";
    return;
  }
  if (!password.isEmpty() && (password.length() < 8 || password.length() > 63)) {
    last.state = AttemptState::kFailed;
    last.error = "密码应为 8~63 位";
    return;
  }
  LOGF("尝试连接新 WiFi「%s」…", ssid.c_str());
  trySsid = ssid;
  tryPassword = password;
  trying = true;
  tryStart = millis();
  gotIpAtTry = gotIpCount;
  last.state = AttemptState::kConnecting;
  beginStation(ssid, password);
}

const Attempt& attempt() { return last; }

const char* stateName(AttemptState s) {
  switch (s) {
    case AttemptState::kConnecting:
      return "connecting";
    case AttemptState::kConnected:
      return "connected";
    case AttemptState::kFailed:
      return "failed";
    default:
      return "idle";
  }
}

std::string lastError() { return connected() ? "" : reasonText(disconnectReason); }

void startScan() { scanWanted = true; }

bool scanning() { return scanWanted || scanRunning; }

const std::vector<Ap>& scanResults() { return aps; }

}  // namespace wifi
