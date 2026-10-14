// 流式 iCalendar (.ics) 解析器：边下载边解析，只保留时间窗口内的事件。
// 纯 C++，不依赖 Arduino，可在电脑上做单元测试。
//
// 支持：
//   - 单次事件、全天事件、跨天事件；行折叠；文本转义
//   - DTSTART/DTEND/DURATION，UTC 时间（…Z）按 utcOffsetSec 转本地时间
//   - RRULE：FREQ=DAILY/WEEKLY/MONTHLY/YEARLY，INTERVAL、COUNT、UNTIL，
//            WEEKLY 的 BYDAY，MONTHLY 的 BYMONTHDAY 或单个 BYDAY（如 2MO、-1FR），
//            YEARLY 的 BYMONTH + 单个 BYDAY（如母亲节 BYMONTH=5;BYDAY=2SU）
//   - EXDATE、RECURRENCE-ID（单次修改/取消重复事件中的某一次）、STATUS:CANCELLED
// 限制：
//   - 带 TZID 的时间按“本地时间”处理（适合日历和设备在同一时区的情况）
//   - 不支持的 RRULE 组合（BYSETPOS、MONTHLY 多个 BYDAY 等）只显示第一次
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <string>
#include <vector>

namespace ics {

// 本地“挂钟时间”的秒数：把本地年月日时分秒当成 UTC 算出的 epoch 秒，方便按天切分
using LocalTime = int64_t;
constexpr LocalTime kDay = 86400;

LocalTime makeLocal(int y, int m, int d, int hh = 0, int mm = 0, int ss = 0);
inline int32_t dayOf(LocalTime t) { return static_cast<int32_t>(t >= 0 ? t / kDay : (t - kDay + 1) / kDay); }

struct Event {
  std::string summary;
  LocalTime start = 0;
  LocalTime end = 0;  // 不含
  bool allDay = false;

  // 该事件是否覆盖 day（cal::daysFromCivil 意义下的天数）
  bool coversDay(int32_t day) const;
};

// 排序：按天，同一天里全天事件在前，其余按开始时间
void sortEvents(std::vector<Event>& events);

class Parser {
 public:
  // 只保留与 [windowStart, windowEnd) 有重叠的事件
  Parser(LocalTime windowStart, LocalTime windowEnd, int32_t utcOffsetSec);

  void feed(const char* data, size_t len);  // 原始字节，可任意分块
  void finish();                            // 数据喂完后调用：展开重复事件并排序

  const std::vector<Event>& events() const { return events_; }
  std::vector<Event>& events() { return events_; }
  size_t veventCount() const { return veventCount_; }
  size_t unsupportedRules() const { return unsupportedRules_; }

 private:
  struct Time {
    LocalTime t = 0;
    bool dateOnly = false;
    bool valid = false;
  };

  struct Rule {
    enum Freq { None, Daily, Weekly, Monthly, Yearly } freq = None;
    int interval = 1;
    int count = 0;  // 0 = 不限
    Time until;
    uint8_t weekdays = 0;  // WEEKLY BYDAY：bit0=周日 … bit6=周六
    int nthWeekday = 0;    // MONTHLY/YEARLY BYDAY 的序数（2MO → 2，-1FR → -1）
    int nthWeekdayWd = -1;
    int monthDay = 0;      // BYMONTHDAY（负数表示倒数）
    int month = 0;         // BYMONTH
    bool unsupported = false;
  };

  struct Pending {
    std::string uid, summary, rrule, status;
    Time start, end, recurrenceId;
    int64_t duration = -1;
    std::vector<Time> exdates;
  };

  struct Master {
    std::string uid, summary;
    Time start;
    int64_t duration;
    Rule rule;
    std::vector<Time> exdates;
  };

  void onPhysicalLine(std::string& line);
  void onLogicalLine(const std::string& line);
  void onProperty(const std::string& name, const std::string& params, const std::string& value);
  void endEvent();
  void addIfInWindow(const std::string& summary, LocalTime start, LocalTime end, bool allDay);
  void expand(const Master& m);
  bool excluded(const Master& m, LocalTime start) const;

  Time parseTime(const std::string& value) const;
  Rule parseRule(const std::string& value) const;
  static int64_t parseDuration(const std::string& value);
  static std::string unescape(const std::string& value);

  LocalTime windowStart_, windowEnd_;
  int32_t utcOffset_;

  std::string raw_;      // 正在接收的物理行
  std::string logical_;  // 已展开折叠的逻辑行
  bool inEvent_ = false;
  int nested_ = 0;  // VEVENT 内部的 VALARM 等嵌套组件
  Pending cur_;

  std::vector<Event> events_;
  std::vector<Master> masters_;
  std::map<std::string, std::vector<Time>> overrides_;  // uid -> 被单独修改/取消的那几次的 RECURRENCE-ID
  size_t veventCount_ = 0;
  size_t unsupportedRules_ = 0;
};

}  // namespace ics
