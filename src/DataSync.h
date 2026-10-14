// 联网获取数据：NTP 对时、法定节假日数据、iCal 在线日历（WiFi 连接见 WifiManager.h）
#pragma once

#include <HolidayCN.h>
#include <IcsParser.h>

#include <string>
#include <vector>

namespace net {

bool syncTime(uint32_t timeoutMs);
bool timeValid();  // 系统时间是否已经对过（没有 RTC，断电后需重新对时）

// 法定节假日：联网下载 {year}.json 并缓存到 LittleFS；离线或下载失败时读缓存
void loadHolidays(HolidayCN& holidays, int year, bool online);

struct IcsResult {
  int sources = 0;    // 配置了几个订阅
  int ok = 0;         // 成功了几个
  std::string error;  // 最近一个错误
};
int icsSourceCount();
// 拉取全部 iCal 订阅，只保留 [from, to) 内的事件。全部失败时不改动 events（保留上次的数据）
IcsResult fetchCalendars(ics::LocalTime from, ics::LocalTime to, std::vector<ics::Event>& events);

}  // namespace net
