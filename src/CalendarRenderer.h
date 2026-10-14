// 日历画面排版（400x300 横屏）：左栏今天的日期 + 农历 + 小月历，右栏整列显示近期日程
// 只依赖 Adafruit_GFX，既能画到 Canvas3C（网页预览 / 电脑预览），也能直接画到 GxEPD2
#pragma once

#include <Adafruit_GFX.h>
#include <HolidayCN.h>
#include <IcsParser.h>

#include <string>
#include <vector>

struct RenderInput {
  int year = 0, month = 0, day = 0;  // 要显示的“今天”
  const HolidayCN* holidays = nullptr;
  const std::vector<ics::Event>* events = nullptr;
  bool icsConfigured = false;  // 是否填写了 iCal 订阅地址
  bool icsFailed = false;      // 最近一次同步是否失败
  std::string status;          // 左下角状态栏，例如 "10:30 已同步"
};

void renderCalendar(Adafruit_GFX& gfx, const RenderInput& in);

// 还不知道日期时（开机后尚未联网对时）显示的提示画面
void renderMessage(Adafruit_GFX& gfx, const std::string& title, const std::string& detail);

// 配网模式的画面：热点二维码 + 操作步骤
struct SetupInput {
  std::string apName;      // 热点名称（也是蓝牙名称）
  std::string apPassword;  // 空 = 开放热点
  std::string apIp;        // 设置页面地址
  std::string error;       // 已保存的 WiFi 连不上时的提示，空 = 不显示
};
void renderSetup(Adafruit_GFX& gfx, const SetupInput& in);

// 画面用得到的日程时间范围：本月 1 日 ~ max(月底, 今天 + AGENDA_DAYS)
void eventWindow(int y, int m, int d, ics::LocalTime& start, ics::LocalTime& end);
