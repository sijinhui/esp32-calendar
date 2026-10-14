#include "ChineseCalendar.h"

#include "lunar_data.h"

namespace cal {

// ---------- 公历 ----------
// 算法来自 Howard Hinnant, "chrono-Compatible Low-Level Date Algorithms"

int32_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

void civilFromDays(int32_t z, int& y, int& m, int& d) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  y = static_cast<int>(yoe) + era * 400 + (m <= 2);
}

int weekday(int y, int m, int d) {
  const int32_t z = daysFromCivil(y, m, d);
  return static_cast<int>(z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6);  // 1970-01-01 是周四
}

bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int daysInMonth(int y, int m) {
  static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return (m == 2 && isLeapYear(y)) ? 29 : kDays[m - 1];
}

// ---------- 农历 ----------

static uint32_t yearWord(int ly) { return LUNAR_YEARS[ly - LUNAR_FIRST_YEAR]; }
static int leapMonthOf(uint32_t w) { return static_cast<int>((w >> 13) & 0xf); }
static int32_t newYearDays(int ly) {
  return daysFromCivil(ly, 1, 1) + static_cast<int32_t>((yearWord(ly) >> 17) & 0x7f);
}

bool toLunar(int y, int m, int d, LunarDate& out) {
  if (y < LUNAR_FIRST_YEAR || y > LUNAR_LAST_YEAR) return false;
  const int32_t days = daysFromCivil(y, m, d);
  int ly = y;
  if (days < newYearDays(ly)) {
    if (--ly < LUNAR_FIRST_YEAR) return false;
  }

  const uint32_t w = yearWord(ly);
  const int leap = leapMonthOf(w);
  const int count = leap ? 13 : 12;
  int32_t offset = days - newYearDays(ly);
  for (int k = 0; k < count; k++) {
    const int len = ((w >> k) & 1) ? 30 : 29;
    if (offset < len) {
      out.year = ly;
      out.leap = leap && k == leap;
      out.month = (leap && k >= leap) ? k : k + 1;
      out.day = static_cast<int>(offset) + 1;
      out.monthDays = len;
      return true;
    }
    offset -= len;
  }
  return false;
}

static const char* const kDigits[] = {"", "一", "二", "三", "四", "五", "六", "七", "八", "九", "十"};

std::string lunarMonthName(const LunarDate& ld) {
  static const char* const kMonths[] = {"", "正", "二", "三", "四", "五", "六",
                                        "七", "八", "九", "十", "冬", "腊"};
  if (ld.month < 1 || ld.month > 12) return "";
  std::string s = ld.leap ? "闰" : "";
  return s + kMonths[ld.month] + "月";
}

std::string lunarDayName(int day) {
  if (day < 1 || day > 30) return "";
  if (day == 10) return "初十";
  if (day == 20) return "二十";
  if (day == 30) return "三十";
  static const char* const kTens[] = {"初", "十", "廿"};
  return std::string(kTens[day / 10]) + kDigits[day % 10];
}

std::string ganzhiYear(int lunarYear) {
  static const char* const kStems[] = {"甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"};
  static const char* const kBranches[] = {"子", "丑", "寅", "卯", "辰", "巳",
                                          "午", "未", "申", "酉", "戌", "亥"};
  const int n = lunarYear - 4;  // 公元 4 年为甲子年
  return std::string(kStems[((n % 10) + 10) % 10]) + kBranches[((n % 12) + 12) % 12];
}

const char* zodiac(int lunarYear) {
  static const char* const kAnimals[] = {"鼠", "牛", "虎", "兔", "龙", "蛇",
                                         "马", "羊", "猴", "鸡", "狗", "猪"};
  return kAnimals[(((lunarYear - 4) % 12) + 12) % 12];
}

// ---------- 节气 ----------

const char* solarTermName(int index) {
  static const char* const kNames[24] = {
      "小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨", "立夏", "小满", "芒种", "夏至",
      "小暑", "大暑", "立秋", "处暑", "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至"};
  return (index >= 0 && index < 24) ? kNames[index] : "";
}

int solarTermDay(int y, int index) {
  if (y < TERM_FIRST_YEAR || y > TERM_LAST_YEAR || index < 0 || index > 23) return 0;
  const uint8_t b = SOLAR_TERMS[y - TERM_FIRST_YEAR][index / 2];
  return (index % 2 == 0) ? (b & 0xf) : (b >> 4) + 15;
}

int solarTermOn(int y, int m, int d) {
  const int first = 2 * (m - 1);
  if (solarTermDay(y, first) == d) return first;
  if (solarTermDay(y, first + 1) == d) return first + 1;
  return -1;
}

// ---------- 节日 ----------

const char* lunarFestival(const LunarDate& ld) {
  if (ld.leap || ld.month == 0) return nullptr;  // 闰月不过节
  const int md = ld.month * 100 + ld.day;
  switch (md) {
    case 101: return "春节";
    case 115: return "元宵节";
    case 202: return "龙抬头";
    case 505: return "端午节";
    case 707: return "七夕节";
    case 715: return "中元节";
    case 815: return "中秋节";
    case 909: return "重阳节";
    case 1208: return "腊八节";
    case 1223: return "小年";
  }
  if (ld.month == 12 && ld.day == ld.monthDays) return "除夕";
  return nullptr;
}

// 当月第 n 个星期 wd 是几号
static int nthWeekday(int y, int m, int wd, int n) {
  const int first = weekday(y, m, 1);
  return 1 + (wd - first + 7) % 7 + (n - 1) * 7;
}

const char* solarFestival(int y, int m, int d) {
  switch (m * 100 + d) {
    case 101: return "元旦";
    case 214: return "情人节";
    case 308: return "妇女节";
    case 312: return "植树节";
    case 401: return "愚人节";
    case 501: return "劳动节";
    case 504: return "青年节";
    case 601: return "儿童节";
    case 701: return "建党节";
    case 801: return "建军节";
    case 910: return "教师节";
    case 1001: return "国庆节";
    case 1224: return "平安夜";
    case 1225: return "圣诞节";
  }
  if (m == 5 && d == nthWeekday(y, 5, 0, 2)) return "母亲节";   // 五月第二个周日
  if (m == 6 && d == nthWeekday(y, 6, 0, 3)) return "父亲节";   // 六月第三个周日
  if (m == 11 && d == nthWeekday(y, 11, 4, 4)) return "感恩节"; // 十一月第四个周四
  return nullptr;
}

DayLabel dayLabel(int y, int m, int d) {
  LunarDate ld;
  const bool hasLunar = toLunar(y, m, d, ld);
  if (hasLunar) {
    if (const char* f = lunarFestival(ld)) return {f, LabelKind::Festival};
  }
  if (const char* f = solarFestival(y, m, d)) return {f, LabelKind::Festival};
  const int term = solarTermOn(y, m, d);
  if (term >= 0) return {solarTermName(term), LabelKind::SolarTerm};
  if (!hasLunar) return {};
  if (ld.day == 1) return {lunarMonthName(ld), LabelKind::LunarMonth};
  return {lunarDayName(ld.day), LabelKind::LunarDay};
}

}  // namespace cal
