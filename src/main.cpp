// ESP32-S3 墨水屏日历
//
// 流程：开机 → 连 WiFi（没配置或连不上就进入配网模式）→ NTP 对时 → 拉取节假日 / 在线日历 → 画到帧缓冲
//       →（有屏幕时）刷屏
// 之后常驻运行：跨天时重画，每 ICS_SYNC_MINUTES 分钟同步一次日历；画面没变就不刷墨水屏。
// 没有屏幕时，用浏览器打开 http://epaper-calendar.local/ 查看画面。
// 配网：手机连设备热点（会自动弹出设置页），或者用微信小程序走蓝牙；长按 BOOT 键随时重新配网。
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ChineseCalendar.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <time.h>

#include "BleControl.h"
#include "CalendarRenderer.h"
#include "Canvas3C.h"
#include "DataSync.h"
#include "EpdOutput.h"
#include "Log.h"
#include "Settings.h"
#include "StatusLed.h"
#include "WebPreview.h"
#include "WifiManager.h"
#include "config.h"

DualLog Log;

namespace {

Canvas3C canvas(SCREEN_W, SCREEN_H);
HolidayCN holidays;
std::vector<ics::Event> events;
net::IcsResult icsResult;
time_t lastSync = 0;          // 上次同步数据的时间
int32_t syncedDay = -1;       // 上次同步数据是哪一天（跨天要重新同步）
bool syncedOnline = false;    // 上次同步时是否联网（软复位后时间还在，可能在连上 WiFi 之前就同步了）
int32_t previewDay = -1;      // 网页上选中的预览日期（-1 = 今天）
uint32_t previewSince = 0;    // 预览开始的时间（millis），10 分钟后自动回到今天
uint32_t epdChecksum = 0;     // 墨水屏上当前画面的校验和
bool epdShowsSetup = false;   // 墨水屏上是否是配网画面

bool today(int& y, int& m, int& d) {
  if (!net::timeValid()) return false;
  const time_t now = time(nullptr);
  tm t;
  localtime_r(&now, &t);
  y = t.tm_year + 1900;
  m = t.tm_mon + 1;
  d = t.tm_mday;
  return true;
}

// 显示配网画面：手动进入配网、还没配置 WiFi，或者连不上 WiFi 又没有日期可显示
bool showSetup() {
  return previewDay < 0 && wifi::provisioning() &&
         (wifi::provisioningManual() || !wifi::configured() || !net::timeValid());
}

std::string statusLine() {
  if (!wifi::connected()) return wifi::provisioning() ? "热点 " + wifi::apName() : "WiFi 未连接";
  std::string s;
  if (!icsResult.error.empty()) {
    s = "同步失败";
  } else if (lastSync) {
    s = "已同步";
  }
  return s + " " + wifi::ip();
}

void draw() {
  if (showSetup()) {
    SetupInput in;
    in.apName = wifi::apName();
    in.apPassword = AP_PASSWORD;
    in.apIp = wifi::apIp();
    if (wifi::configured() && !wifi::connected()) {
      in.error = "连不上「" + std::string(settings::wifiSsid().c_str()) + "」：" + wifi::lastError();
    }
    renderSetup(canvas, in);
    return;
  }

  int y, m, d;
  if (previewDay >= 0) {
    cal::civilFromDays(previewDay, y, m, d);
  } else if (!today(y, m, d)) {
    renderMessage(canvas, "等待联网对时…",
                  wifi::connected() ? "正在连接 NTP 服务器"
                                    : "正在连接 WiFi「" + std::string(settings::wifiSsid().c_str()) + "」");
    return;
  }

  RenderInput in;
  in.year = y;
  in.month = m;
  in.day = d;
  in.holidays = &holidays;
  in.events = &events;
  in.icsConfigured = net::icsSourceCount() > 0;
  in.icsFailed = !icsResult.error.empty();
  in.status = statusLine();
  renderCalendar(canvas, in);
}

void render(bool toEpd) {
  draw();
  if (toEpd) epdShowsSetup = showSetup();
  if (toEpd && epd::enabled() && canvas.checksum() != epdChecksum) {
    epd::show(canvas);
    epdChecksum = canvas.checksum();
  }
}

// 同步节假日（当年 + 次年）和在线日历
void syncData() {
  int y, m, d;
  if (!today(y, m, d)) return;
  led::setSyncing(true);
  for (int year : {y, y + 1}) net::loadHolidays(holidays, year, true);

  ics::LocalTime from, to;
  eventWindow(y, m, d, from, to);
  icsResult = net::fetchCalendars(from, to, events);
  lastSync = time(nullptr);
  syncedDay = cal::daysFromCivil(y, m, d);
  syncedOnline = wifi::connected();
  led::setSyncing(false);
  LOGF("同步完成：日程 %u 条，可用内存 %u 字节", events.size(), ESP.getFreeHeap());
}

void fillStatus(JsonDocument& doc) {
  int y, m, d;
  if (today(y, m, d)) {
    char buf[32];
    const time_t now = time(nullptr);
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", localtime(&now));
    doc["time"] = buf;
    doc["holidays_loaded"] = holidays.hasYear(y);
  } else {
    doc["time"] = nullptr;
  }
  if (previewDay >= 0) {
    cal::civilFromDays(previewDay, y, m, d);
    char buf[16];
    snprintf(buf, sizeof buf, "%04d-%02d-%02d", y, m, d);
    doc["preview"] = buf;
  }
  doc["wifi"] = wifi::connected();
  doc["ssid"] = settings::wifiSsid();
  doc["ip"] = wifi::ip();
  doc["rssi"] = WiFi.RSSI();
  doc["provisioning"] = wifi::provisioning();
  doc["ap"] = wifi::apName();
  doc["calendars"] = net::icsSourceCount();
  doc["calendars_ok"] = icsResult.ok;
  doc["calendar_error"] = icsResult.error;
  doc["events"] = events.size();
  doc["epd"] = epd::enabled();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["uptime_s"] = millis() / 1000;
  doc["width"] = SCREEN_W;
  doc["height"] = SCREEN_H;
}

String statusJson() {
  JsonDocument doc;
  fillStatus(doc);
  String out;
  serializeJson(doc, out);
  return out;
}

// 长按 BOOT 键进入配网模式
void checkButton() {
  static uint32_t pressedAt = 0;
  static bool fired = false;
  if (digitalRead(BUTTON_PIN) != LOW) {
    pressedAt = 0;
    fired = false;
    return;
  }
  if (!pressedAt) pressedAt = millis();
  if (!fired && millis() - pressedAt >= BUTTON_HOLD_MS) {
    fired = true;
    LOGF("长按 BOOT 键：进入配网模式");
    wifi::startProvisioning(true);
  }
}

}  // namespace

void setup() {
  Log.begin(115200);
  delay(1500);  // 给 USB 串口一点时间，方便看到开机日志
  LOGF("==== ESP32-S3 墨水屏日历 ====");
  LOGF("芯片 %s，Flash %u MB，PSRAM %u MB，可用内存 %u 字节", ESP.getChipModel(), ESP.getFlashChipSize() >> 20,
       ESP.getPsramSize() >> 20, ESP.getFreeHeap());

  if (!canvas.ok()) LOGF("帧缓冲分配失败！");
  if (!LittleFS.begin(true)) LOGF("LittleFS 挂载失败，节假日数据将无法缓存");
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // 开机就设好时区：路由器通过 DHCP 下发 NTP 服务器时，系统会自己对时，不一定走到 net::syncTime()
  setenv("TZ", TZ_POSIX, 1);
  tzset();
  settings::begin();
  epd::begin();
  led::begin();
  wifi::begin();  // 不阻塞：连上 WiFi 后在 loop() 里对时、同步
  render(false);  // 先在网页上显示“等待联网”；三色屏刷一次要十几秒，联网结果出来再刷

  web::Hooks hooks;
  hooks.preview = [](int y, int m, int d) {
    previewDay = y ? cal::daysFromCivil(y, m, d) : -1;
    previewSince = millis();
    render(false);  // 预览只画到网页，不动墨水屏
  };
  hooks.sync = [] {
    syncData();
    render(previewDay < 0);
  };
  hooks.pushEpd = [] {
    if (!epd::enabled()) return;
    epd::show(canvas);
    epdChecksum = canvas.checksum();
  };
  hooks.statusJson = statusJson;
  web::begin(canvas, hooks);

  ble::Hooks bleHooks;
  bleHooks.status = fillStatus;
  bleHooks.icsChanged = hooks.sync;
  ble::begin(wifi::apName(), bleHooks);
}

void loop() {
  wifi::loop();
  web::loop();
  ble::loop();
  checkButton();
  led::loop();

  static uint32_t lastCheck = 0;
  if (millis() - lastCheck < 1000) return;
  lastCheck = millis();

  led::setProvisioning(wifi::provisioning());
  static bool wasConnected = false;
  if (wifi::connected() && !wasConnected) led::flashConnected();
  wasConnected = wifi::connected();

  // 连上 WiFi 后对时（开机、配网完成，或者开机时没网）
  if (!net::timeValid() && wifi::connected()) {
    static uint32_t lastTry = 0;
    if (!lastTry || millis() - lastTry > 30000) {
      lastTry = millis();
      led::setSyncing(true);
      const bool timeOk = net::syncTime(10000);
      led::setSyncing(false);
      if (timeOk) {
        syncData();
        render(true);
      }
    }
  }

  // 进入 / 退出配网模式时更新墨水屏
  if (showSetup() != epdShowsSetup) render(true);

  if (!net::timeValid()) return;

  if (previewDay >= 0 && millis() - previewSince > 10 * 60 * 1000) {
    previewDay = -1;
    render(true);
  }

  int y, m, d;
  today(y, m, d);
  const bool newDay = cal::daysFromCivil(y, m, d) != syncedDay;
  const bool due = time(nullptr) - lastSync >= ICS_SYNC_MINUTES * 60;
  const bool cameOnline = !syncedOnline && wifi::connected();
  if (newDay || due || cameOnline) {
    syncData();
    render(previewDay < 0);
  }
}
