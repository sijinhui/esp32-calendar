// ESP32-S3 墨水屏日历
//
// 流程：开机 → 连 WiFi（没配置就进入配网模式）→ NTP 对时 → 拉取节假日 / 在线日历 → 画到帧缓冲
//       →（有屏幕时）刷屏
// 之后跨天时、以及每 ICS_SYNC_MINUTES 分钟同步一次（节假日每天一次）；画面没变就不刷墨水屏。
// 省电模式（DEBUG_MODE=0）：WiFi 只在开机后 / 短按 BOOT 键后的 WIFI_WINDOW_S 秒、以及同步时打开，
// 其余时间浅睡眠，到点或按 BOOT 键醒来。开发模式（DEBUG_MODE=1）：一直联网，不睡眠。
// 没有屏幕时，用浏览器打开 http://epaper-calendar.local/ 查看画面。
// 配网：手机连设备热点（会自动弹出设置页），或者用微信小程序走蓝牙；长按 BOOT 键随时重新配网。
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ChineseCalendar.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_core_dump.h>
#include <esp_sleep.h>
#include <time.h>

#include <algorithm>

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
int32_t holidayDay = -1;      // 哪一天下载过节假日数据（一天只下载一次）
int icsFailures = 0;          // 日历连续同步失败的次数
int wifiFailures = 0;         // 同步时连续连不上 WiFi 的次数
bool syncing = false;         // 到点该同步了，正在等 WiFi 连上
uint32_t syncWaitSince = 0;   // 开始等 WiFi 的时间（millis）
uint32_t lastAttempt = 0;     // 上次尝试同步（含对时）的时间（millis），0 = 还没试过
int32_t previewDay = -1;      // 网页上选中的预览日期（-1 = 今天）
uint32_t previewSince = 0;    // 预览开始的时间（millis），10 分钟后自动回到今天
std::string lastCrash;        // 上次崩溃的位置（开机时从 Flash 里的崩溃转储读出来）
uint32_t epdChecksum = 0;     // 墨水屏上当前画面的校验和
bool epdShowsSetup = false;   // 墨水屏上是否是配网画面

const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "上电";
    case ESP_RST_SW: return "软件重启";
    case ESP_RST_PANIC: return "程序崩溃";
    case ESP_RST_INT_WDT: return "中断看门狗";
    case ESP_RST_TASK_WDT: return "任务看门狗";
    case ESP_RST_WDT: return "看门狗";
    case ESP_RST_BROWNOUT: return "电压不足";
    case ESP_RST_DEEPSLEEP: return "深度睡眠唤醒";
    case ESP_RST_EXT: return "复位键";
    default: return "其他";
  }
}

// 程序崩溃时系统会把现场存到 coredump 分区；开机读出摘要打到日志里（设备不插 UART 口也能知道崩在哪），然后清掉。
// 地址用 xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/esp32s3/firmware.elf <地址> 解析
void reportCrash() {
  if (esp_core_dump_image_check() != ESP_OK) return;
  auto* sum = static_cast<esp_core_dump_summary_t*>(malloc(sizeof(esp_core_dump_summary_t)));
  if (sum && esp_core_dump_get_summary(sum) == ESP_OK) {
    char buf[320];
    int n = snprintf(buf, sizeof buf, "任务 %s，PC 0x%08x，异常 %u，地址 0x%08x，回溯", sum->exc_task,
                     static_cast<unsigned>(sum->exc_pc), static_cast<unsigned>(sum->ex_info.exc_cause),
                     static_cast<unsigned>(sum->ex_info.exc_vaddr));
    for (uint32_t i = 0; i < sum->exc_bt_info.depth && n < static_cast<int>(sizeof buf) - 12; i++) {
      n += snprintf(buf + n, sizeof buf - n, " 0x%08x", static_cast<unsigned>(sum->exc_bt_info.bt[i]));
    }
    lastCrash = buf;
    LOGF("上次崩溃：%s", buf);
  }
  free(sum);
  esp_core_dump_image_erase();
}

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

// 连续失败 SYNC_FAIL_SHOW_AFTER 次才算失败，偶尔失败一次不改画面（三色屏刷一次十几秒）
bool failedTooOften(int failures) { return failures >= SYNC_FAIL_SHOW_AFTER; }
bool icsFailed() { return failedTooOften(icsFailures); }

// 屏幕上的状态只放不常变的内容（IP 在网页 / 小程序里看）。
// 省电模式下 WiFi 平时是关着的，所以看的是同步结果，不是当前的连接状态
std::string statusLine() {
  if (wifi::provisioning() && !wifi::connected()) return "热点 " + wifi::apName();
  if (failedTooOften(wifiFailures)) return "WiFi 未连接";
  if (icsFailed()) return "同步失败";
  return lastSync ? "已同步" : "";
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
    const std::string ssid = settings::wifiSsid().c_str();
    renderMessage(canvas, "等待联网对时…",
                  wifi::connected() ? "正在连接 NTP 服务器"
                  : wifiFailures    ? "连不上 WiFi「" + ssid + "」，长按 BOOT 键 3 秒可重新配网"
                                    : "正在连接 WiFi「" + ssid + "」");
    return;
  }

  RenderInput in;
  in.year = y;
  in.month = m;
  in.day = d;
  in.holidays = &holidays;
  in.events = &events;
  in.icsConfigured = net::icsSourceCount() > 0;
  in.icsFailed = icsFailed();
  in.status = statusLine();
  renderCalendar(canvas, in);
}

void render(bool toEpd) {
  draw();
  if (toEpd) epdShowsSetup = showSetup();
  if (!toEpd || !epd::enabled()) return;
  const uint32_t sum = canvas.checksum();
  if (sum != epdChecksum) {
    epd::show(canvas);
    epdChecksum = sum;
  }
}

// 同步期间临时升频，结束后回到低频
struct CpuBoost {
  CpuBoost() { setCpuFrequencyMhz(CPU_MHZ_SYNC); }
  ~CpuBoost() { setCpuFrequencyMhz(CPU_MHZ_IDLE); }
};

// 同步节假日（当年 + 次年）和在线日历
void syncData() {
  int y, m, d;
  if (!today(y, m, d)) return;
  CpuBoost boost;
  const int32_t day = cal::daysFromCivil(y, m, d);

  // 节假日安排一年只变几次：每天下载一次，其余时候用内存里的 / LittleFS 缓存；
  // 当年的数据既没下载到也没有缓存时，每次同步都重试
  const bool refreshHolidays = wifi::connected() && (holidayDay != day || !holidays.hasYear(y));
  for (int year : {y, y + 1}) net::loadHolidays(holidays, year, refreshHolidays);
  if (refreshHolidays) holidayDay = day;

  ics::LocalTime from, to;
  eventWindow(y, m, d, from, to);
  icsResult = net::fetchCalendars(from, to, events);
  icsFailures = icsResult.error.empty() ? 0 : icsFailures + 1;
  lastSync = time(nullptr);
  syncedDay = day;
  syncedOnline = wifi::connected();
  LOGF("同步完成：日程 %u 条%s，可用内存 %u 字节", events.size(),
       icsFailures ? ("，连续失败 " + std::to_string(icsFailures) + " 次").c_str() : "", ESP.getFreeHeap());
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
  doc["calendar_failures"] = icsFailures;
  doc["wifi_failures"] = wifiFailures;
  doc["low_power"] = !DEBUG_MODE;
  doc["events"] = events.size();
  doc["epd"] = epd::enabled();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["cpu_mhz"] = getCpuFrequencyMhz();
  doc["uptime_s"] = millis() / 1000;
  doc["reset_reason"] = resetReason();
  if (!lastCrash.empty()) doc["last_crash"] = lastCrash;
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

// BOOT 键：长按进入配网模式；短按打开 WiFi 和网页 WIFI_WINDOW_S 秒（省电模式下平时 WiFi 是关的）
void checkButton() {
  static uint32_t pressedAt = 0, lastShort = 0;
  static bool down = false, fired = false;
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (!down) {
      down = true;
      fired = false;
      pressedAt = millis();
    }
    if (!fired && millis() - pressedAt >= BUTTON_HOLD_MS) {
      fired = true;
      LOGF("长按 BOOT 键：进入配网模式");
      wifi::startProvisioning(true);
    }
    return;
  }
  if (down && !fired && millis() - lastShort > 300) {  // 300ms 内的抖动不重复算
    lastShort = millis();
    LOGF("短按 BOOT 键：WiFi 和网页打开 %d 分钟", WIFI_WINDOW_S / 60);
    wifi::openWindow(WIFI_WINDOW_S * 1000UL);
  }
  down = false;
}

// 离下次同步（或重试对时）还有几秒；<= 0 表示该同步了
int32_t secondsUntilSync() {
  if (!net::timeValid()) {
    // 还没对过时：联网时每 30 秒重试，没联网就等下个同步周期
    const uint32_t retry = wifi::connected() ? 30000 : ICS_SYNC_MINUTES * 60000UL;
    const uint32_t elapsed = millis() - lastAttempt;
    return !lastAttempt || elapsed >= retry ? 0 : static_cast<int32_t>((retry - elapsed + 999) / 1000);
  }
  int y, m, d;
  today(y, m, d);
  if (cal::daysFromCivil(y, m, d) != syncedDay) return 0;  // 跨天
  if (!syncedOnline && wifi::connected()) return 0;        // 上次没联网，现在连上了
  const time_t now = time(nullptr);
  tm t;
  localtime_r(&now, &t);
  const int32_t untilMidnight = 24 * 3600 - (t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec);
  const int32_t untilDue = static_cast<int32_t>(lastSync + ICS_SYNC_MINUTES * 60 - now);
  return std::min(untilDue, untilMidnight);
}

// WiFi 连上了（online）或者等超时了：对时、同步、重画
void runSync(bool online) {
  lastAttempt = millis();
  wifiFailures = online ? 0 : wifiFailures + 1;
  if (online) {
    // 每次联网都对时（浅睡眠时靠芯片内部的 RC 振荡器计时，误差较大）
    net::syncTime(net::timeValid() ? 3000 : 10000);
  } else {
    LOGF("%d 秒没连上 WiFi，等下次同步再试（连续 %d 次）", WIFI_SYNC_TIMEOUT_S, wifiFailures);
  }
  if (net::timeValid()) syncData();
  led::setSyncing(false);
  led::showResult(online && net::timeValid() && icsResult.error.empty());
  render(previewDay < 0);
  syncing = false;
  wifi::holdForSync(false);
}

// 每秒检查一次：配网画面、预览超时、该不该同步
void tick() {
  led::setProvisioning(wifi::provisioning());

  // 进入 / 退出配网模式时更新墨水屏
  if (showSetup() != epdShowsSetup) render(true);

  // 预览只给网页看：10 分钟后、或者 WiFi 关了（网页打不开了）就回到今天
  if (previewDay >= 0 && (millis() - previewSince > 10 * 60 * 1000 || !wifi::radioOn())) {
    previewDay = -1;
    render(true);
  }

  if (!syncing && secondsUntilSync() <= 0) {
    syncing = true;
    syncWaitSince = millis();
    led::setSyncing(true);
    wifi::holdForSync(true);  // 省电模式下这时才打开 WiFi
  }
  if (syncing) {
    if (wifi::connected()) {
      runSync(true);
    } else if (millis() - syncWaitSince > WIFI_SYNC_TIMEOUT_S * 1000UL) {
      runSync(false);
    }
  }
}

// 省电模式：WiFi 关着、没事可做时浅睡眠（内存保留，醒来接着跑），到下次同步或者按 BOOT 键时醒来
void sleepIfIdle() {
#if !DEBUG_MODE
  if (syncing || wifi::radioOn() || ble::active() || !led::idle() || digitalRead(BUTTON_PIN) == LOW) return;
  const int32_t seconds = secondsUntilSync();
  if (seconds <= 0) return;
  LOGF("浅睡眠 %ld 秒（短按 BOOT 键可唤醒）", static_cast<long>(seconds));
  Log.flushAll();
  const bool usbHost = Serial;  // 插着电脑（开发调试时）
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);
  gpio_wakeup_enable(static_cast<gpio_num_t>(BUTTON_PIN), GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  const uint32_t t0 = millis();
  esp_light_sleep_start();
  const uint32_t slept = millis() - t0;
  // 睡眠时 USB 串口会断开，醒来后要重新枚举：插着电脑时等一下再往下跑，免得丢日志
  if (usbHost) delay(4000);
  LOGF("醒来（%s），睡了 %lu 秒", esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO ? "按键" : "定时",
       slept / 1000);
#endif
}

}  // namespace

void setup() {
  Log.begin(115200);
  delay(1500);  // 给 USB 串口一点时间，方便看到开机日志
  LOGF("==== ESP32-S3 墨水屏日历 ====");
  LOGF("芯片 %s，Flash %u MB，PSRAM %u MB，可用内存 %u 字节", ESP.getChipModel(), ESP.getFlashChipSize() >> 20,
       ESP.getPsramSize() >> 20, ESP.getFreeHeap());
  LOGF("上次重启原因：%s", resetReason());
  reportCrash();

  if (!canvas.ok()) LOGF("帧缓冲分配失败！");
  if (!LittleFS.begin(true)) LOGF("LittleFS 挂载失败，节假日数据将无法缓存");
  setCpuFrequencyMhz(CPU_MHZ_IDLE);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // 时区开机就设好（对时由 net::syncTime() 在每次同步时做，不用系统常驻的 SNTP）
  setenv("TZ", TZ_POSIX, 1);
  tzset();
  settings::begin();
  epd::begin();
  led::begin();
  LOGF("%s", DEBUG_MODE ? "开发模式：WiFi 一直联网，不睡眠" : "省电模式：WiFi 按需打开，空闲时浅睡眠");
  wifi::begin();  // 不阻塞：连上 WiFi 后在 loop() 里对时、同步
  wifi::openWindow(WIFI_WINDOW_S * 1000UL);  // 开机后先联网一段时间，方便用网页 / 小程序查看和设置
  render(false);  // 先在网页上显示“等待联网”；三色屏刷一次要十几秒，联网结果出来再刷

  web::Hooks hooks;
  hooks.preview = [](int y, int m, int d) {
    previewDay = y ? cal::daysFromCivil(y, m, d) : -1;
    previewSince = millis();
    render(false);  // 预览只画到网页，不动墨水屏
  };
  hooks.sync = [] {
    led::setSyncing(true);
    syncData();
    led::setSyncing(false);
    led::showResult(net::timeValid() && icsResult.error.empty());
    render(previewDay < 0);
  };
  hooks.pushEpd = [] {
    if (!epd::enabled()) return;
    epd::show(canvas);
    epdChecksum = canvas.checksum();
  };
  hooks.statusJson = statusJson;
  web::begin(canvas, hooks);
  wifi::onPower([](bool on) { on ? web::resume() : web::pause(); });

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

  static uint32_t lastCheck = 0;
  if (millis() - lastCheck < 1000) return;
  lastCheck = millis();
  tick();
  sleepIfIdle();
}
