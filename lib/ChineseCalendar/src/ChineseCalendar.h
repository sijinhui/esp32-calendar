// 公历 / 农历 / 二十四节气 / 传统节日。纯 C++，不依赖 Arduino，可在电脑上做单元测试。
// 农历与节气数据覆盖 1900–2100 年（见 lunar_data.h，由 tools/gen_lunar_data.py 生成）。
#pragma once

#include <stdint.h>

#include <string>

namespace cal {

// ---------- 公历工具 ----------
int32_t daysFromCivil(int y, int m, int d);  // 距 1970-01-01 的天数
void civilFromDays(int32_t days, int& y, int& m, int& d);
int weekday(int y, int m, int d);  // 0 = 周日 … 6 = 周六
bool isLeapYear(int y);
int daysInMonth(int y, int m);

// ---------- 农历 ----------
struct LunarDate {
  int year = 0;       // 农历年（以正月初一为界）
  int month = 0;      // 1..12
  int day = 0;        // 1..30
  bool leap = false;  // 是否闰月
  int monthDays = 0;  // 本月天数（29 或 30）
};

// 公历转农历，超出数据范围返回 false
bool toLunar(int y, int m, int d, LunarDate& out);

std::string lunarMonthName(const LunarDate& ld);  // "正月" "闰六月" "冬月" "腊月"
std::string lunarDayName(int day);                // "初一" "十五" "廿三" "三十"
std::string ganzhiYear(int lunarYear);            // "丙午"
const char* zodiac(int lunarYear);                // "马"

// ---------- 二十四节气 ----------
// 节气序号 0..23，从小寒开始：0 小寒、1 大寒、2 立春 … 23 冬至
const char* solarTermName(int index);
int solarTermDay(int y, int index);  // 该年该节气在第几日（月份 = index/2+1），超出范围返回 0
int solarTermOn(int y, int m, int d);  // 当天是哪个节气，不是节气返回 -1

// ---------- 节日 ----------
const char* lunarFestival(const LunarDate& ld);  // 春节、元宵节、端午节、中秋节、除夕……
const char* solarFestival(int y, int m, int d);  // 元旦、劳动节、国庆节、母亲节……

// ---------- 日历格子里数字下面那行字 ----------
// 优先级：农历节日 > 公历节日 > 节气 > 农历月初（显示月名）> 农历日
enum class LabelKind { None, LunarDay, LunarMonth, SolarTerm, Festival };
struct DayLabel {
  std::string text;
  LabelKind kind = LabelKind::None;
};
DayLabel dayLabel(int y, int m, int d);

}  // namespace cal
