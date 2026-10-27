#include "BleControl.h"

#include <NimBLEDevice.h>
#include <WiFi.h>

#include <deque>
#include <mutex>

#include "Log.h"
#include "Settings.h"
#include "WifiManager.h"

namespace ble {

namespace {

// 小程序端（miniprogram/utils/ble.js）用同样的 UUID
constexpr const char* kServiceUuid = "9b6d0001-3c1a-4f7e-a1d2-5e8c7b4a2f10";
constexpr const char* kRxUuid = "9b6d0002-3c1a-4f7e-a1d2-5e8c7b4a2f10";
constexpr const char* kTxUuid = "9b6d0003-3c1a-4f7e-a1d2-5e8c7b4a2f10";
constexpr size_t kMaxLine = 4096;
constexpr const char* kNeedProvisioning = "设备不在配网模式：请长按设备上的 BOOT 键 3 秒";
constexpr uint32_t kLingerMs = 60000;  // 配网结束后手机还连着蓝牙的话，最多再保留多久

Hooks hooks;
std::string deviceName;
NimBLEServer* server = nullptr;
NimBLECharacteristic* tx = nullptr;

// 下面几个变量在 NimBLE 任务里写、主循环里读
std::mutex mu;
std::string rxBuf;
std::deque<std::string> commands;
uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;
bool authorized = false;

// 只在主循环里用
bool waitScan = false, waitWifi = false;
std::string advMfg;
uint32_t neededAt = 0;    // 最近一次需要蓝牙（配网模式）的时间
uint32_t lastAdvAt = 0;   // 上次检查广播内容的时间

// Arduino 3.x（C5）配的是 NimBLE-Arduino 2.x，回调参数从 ble_gap_conn_desc* 换成了 NimBLEConnInfo&
#if ESP_ARDUINO_VERSION_MAJOR >= 3
using ConnArg = NimBLEConnInfo&;
uint16_t handleOf(ConnArg info) { return info.getConnHandle(); }
#else
using ConnArg = ble_gap_conn_desc*;
uint16_t handleOf(ConnArg desc) { return desc->conn_handle; }
#endif

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, ConnArg conn) override {
    std::lock_guard<std::mutex> lock(mu);
    connHandle = handleOf(conn);
    authorized = wifi::provisioning();
    rxBuf.clear();
    commands.clear();
  }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  void onDisconnect(NimBLEServer*, ConnArg, int) override {
#else
  void onDisconnect(NimBLEServer*, ConnArg) override {
#endif
    std::lock_guard<std::mutex> lock(mu);
    connHandle = BLE_HS_CONN_HANDLE_NONE;
    authorized = false;
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, ConnArg) override {
    const NimBLEAttValue v = c->getValue();
    std::lock_guard<std::mutex> lock(mu);
    for (size_t i = 0; i < v.length(); i++) {
      const char ch = static_cast<char>(v.data()[i]);
      if (ch == '\n') {
        if (!rxBuf.empty()) commands.push_back(rxBuf);
        rxBuf.clear();
      } else if (rxBuf.size() < kMaxLine) {
        rxBuf += ch;
      }
    }
  }
};

// 蓝牙会反复打开 / 关闭，回调对象用静态的，不交给 NimBLE 释放
ServerCallbacks serverCallbacks;
RxCallbacks rxCallbacks;

bool peerConnected() {
  std::lock_guard<std::mutex> lock(mu);
  return connHandle != BLE_HS_CONN_HANDLE_NONE;
}

void send(const JsonDocument& doc) {
  uint16_t handle;
  {
    std::lock_guard<std::mutex> lock(mu);
    handle = connHandle;
  }
  if (handle == BLE_HS_CONN_HANDLE_NONE) return;

  std::string out;
  serializeJson(doc, out);
  out += '\n';
  const uint16_t mtu = server->getPeerMTU(handle);
  const size_t chunk = mtu > 23 ? mtu - 3 : 20;
  for (size_t off = 0; off < out.size(); off += chunk) {
    tx->notify(reinterpret_cast<const uint8_t*>(out.data()) + off, std::min(chunk, out.size() - off));
    delay(10);  // 连续通知太快，协议栈缓冲区会满而丢包
  }
}

void sendError(const std::string& cmd, const std::string& error) {
  JsonDocument doc;
  doc["cmd"] = cmd;
  doc["error"] = error;
  send(doc);
}

void handle(const std::string& line) {
  JsonDocument req;
  if (deserializeJson(req, line) != DeserializationError::Ok) return sendError("", "命令格式错误");
  const std::string cmd = req["cmd"] | "";
  bool allowed;
  {
    std::lock_guard<std::mutex> lock(mu);
    allowed = authorized || wifi::provisioning();
  }
  LOGF("蓝牙命令：%s", cmd.c_str());

  JsonDocument resp;
  resp["cmd"] = cmd;
  if (cmd == "status") {
    if (hooks.status) hooks.status(resp);
    send(resp);
  } else if (cmd == "scan") {
    wifi::startScan();
    waitScan = true;
  } else if (cmd == "wifi") {
    if (!allowed) return sendError(cmd, kNeedProvisioning);
    wifi::tryConnect(req["ssid"] | "", req["password"] | "");
    resp["state"] = wifi::stateName(wifi::attempt().state);
    if (wifi::attempt().state == wifi::AttemptState::kFailed) {
      resp["error"] = wifi::attempt().error;
    } else {
      waitWifi = true;
    }
    send(resp);
  } else if (cmd == "ics") {
    if (!allowed) return sendError(cmd, kNeedProvisioning);
    if (!req["urls"].is<JsonArray>()) return sendError(cmd, "缺少 urls");
    std::vector<std::string> urls;
    for (JsonVariant v : req["urls"].as<JsonArray>()) {
      const char* url = v.as<const char*>();
      if (url && *url) urls.emplace_back(url);
    }
    const std::string error = settings::checkIcsUrls(urls);
    if (!error.empty()) return sendError(cmd, error);
    settings::setIcsUrls(urls);
    resp["ok"] = true;
    send(resp);
    if (hooks.icsChanged) hooks.icsChanged();  // 同步要十几秒，先回复
  } else {
    sendError(cmd, "未知命令");
  }
}

// 广播里的 IP / 状态变了就更新
void updateAdvertising() {
  const bool up = wifi::connected();
  const IPAddress ip = up ? WiFi.localIP() : IPAddress(0, 0, 0, 0);
  const uint8_t flags = (wifi::provisioning() ? 1 : 0) | (up ? 2 : 0);
  std::string mfg("\xFF\xFF" "EP", 4);
  mfg += static_cast<char>(1);
  mfg += static_cast<char>(flags);
  for (int i = 0; i < 4; i++) mfg += static_cast<char>(ip[i]);
  if (mfg == advMfg) return;
  advMfg = mfg;

  NimBLEAdvertisementData adv, rsp;
  adv.setFlags(BLE_HS_ADV_F_DISC_GEN);
  adv.setCompleteServices(NimBLEUUID(kServiceUuid));
  rsp.setName(deviceName);
  rsp.setManufacturerData(mfg);

  NimBLEAdvertising* a = NimBLEDevice::getAdvertising();
  const bool advertising = a->isAdvertising();
  if (advertising) a->stop();
  a->setAdvertisementData(adv);
  a->setScanResponseData(rsp);
  // 有手机连着时不广播（断开后 NimBLE 会自动恢复广播，用的就是这里设置的内容）
  if (advertising || !peerConnected()) a->start();
}

void start() {
  NimBLEDevice::init(deviceName);
  server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks, false);
  NimBLEService* service = server->createService(kServiceUuid);
  NimBLECharacteristic* rx =
      service->createCharacteristic(kRxUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(&rxCallbacks);
  tx = service->createCharacteristic(kTxUuid, NIMBLE_PROPERTY::NOTIFY);
#if ESP_ARDUINO_VERSION_MAJOR < 3
  service->start();  // NimBLE 2.x 会在服务器启动时自动启动服务
#endif
  advMfg.clear();
  updateAdvertising();
  lastAdvAt = millis();
  LOGF("蓝牙已打开：%s，可用内存 %u 字节", deviceName.c_str(), ESP.getFreeHeap());
}

// 关闭协议栈和射频控制器，释放内存（NimBLE 的服务器、广播对象一并删除）
void stop() {
  NimBLEDevice::deinit(true);
  server = nullptr;
  tx = nullptr;
  {
    std::lock_guard<std::mutex> lock(mu);
    connHandle = BLE_HS_CONN_HANDLE_NONE;
    authorized = false;
    rxBuf.clear();
    commands.clear();
  }
  waitScan = waitWifi = false;
  advMfg.clear();
  LOGF("蓝牙已关闭，可用内存 %u 字节", ESP.getFreeHeap());
}

}  // namespace

void begin(const std::string& name, const Hooks& h) {
  hooks = h;
  deviceName = name;
}

bool active() { return server != nullptr; }

void loop() {
  // 蓝牙只在配网模式下打开；配网结束后等手机断开（最多 kLingerMs）再关
  const uint32_t now = millis();
  if (wifi::provisioning()) {
    neededAt = now;
    if (!server) start();
  } else if (server && (!peerConnected() || now - neededAt > kLingerMs)) {
    stop();
  }
  if (!server) return;

  std::deque<std::string> pending;
  {
    std::lock_guard<std::mutex> lock(mu);
    pending.swap(commands);
  }
  for (const auto& line : pending) handle(line);

  if (waitScan && !wifi::scanning()) {
    waitScan = false;
    JsonDocument doc;
    doc["cmd"] = "scan";
    JsonArray aps = doc["aps"].to<JsonArray>();
    for (const auto& ap : wifi::scanResults()) {
      JsonObject o = aps.add<JsonObject>();
      o["ssid"] = ap.ssid;
      o["rssi"] = ap.rssi;
      o["secure"] = ap.secure;
    }
    send(doc);
  }

  const wifi::Attempt& a = wifi::attempt();
  if (waitWifi && a.state != wifi::AttemptState::kConnecting) {
    waitWifi = false;
    JsonDocument doc;
    doc["cmd"] = "wifi";
    doc["state"] = wifi::stateName(a.state);
    if (a.state == wifi::AttemptState::kConnected) {
      doc["ip"] = wifi::ip();
    } else {
      doc["error"] = a.error;
    }
    send(doc);
  }

  if (now - lastAdvAt >= 1000) {
    lastAdvAt = now;
    updateAdvertising();
  }
}

}  // namespace ble
