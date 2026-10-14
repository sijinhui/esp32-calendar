// pio test -e native -f test_lunar
#include <ChineseCalendar.h>
#include <unity.h>

using namespace cal;

void setUp() {}
void tearDown() {}

static void assertLunar(int y, int m, int d, int ly, int lm, int ld, bool leap) {
  LunarDate out;
  char msg[48];
  snprintf(msg, sizeof msg, "%04d-%02d-%02d", y, m, d);
  TEST_ASSERT_TRUE_MESSAGE(toLunar(y, m, d, out), msg);
  TEST_ASSERT_EQUAL_INT_MESSAGE(ly, out.year, msg);
  TEST_ASSERT_EQUAL_INT_MESSAGE(lm, out.month, msg);
  TEST_ASSERT_EQUAL_INT_MESSAGE(ld, out.day, msg);
  TEST_ASSERT_EQUAL_MESSAGE(leap, out.leap, msg);
}

void test_civil_roundtrip() {
  TEST_ASSERT_EQUAL_INT32(0, daysFromCivil(1970, 1, 1));
  for (int32_t z = daysFromCivil(1899, 1, 1); z <= daysFromCivil(2101, 12, 31); z++) {
    int y, m, d;
    civilFromDays(z, y, m, d);
    TEST_ASSERT_EQUAL_INT32(z, daysFromCivil(y, m, d));
  }
  TEST_ASSERT_EQUAL_INT(4, weekday(1970, 1, 1));   // 周四
  TEST_ASSERT_EQUAL_INT(3, weekday(2026, 10, 14)); // 周三
  TEST_ASSERT_EQUAL_INT(2, weekday(2000, 2, 29));  // 周二
  TEST_ASSERT_EQUAL_INT(29, daysInMonth(2000, 2));
  TEST_ASSERT_EQUAL_INT(28, daysInMonth(2100, 2));
}

// 期望值来自 lunar_python
void test_known_dates() {
  assertLunar(1900, 1, 31, 1900, 1, 1, false);
  assertLunar(1901, 1, 1, 1900, 11, 11, false);
  assertLunar(2025, 7, 25, 2025, 6, 1, true);   // 闰六月初一
  assertLunar(2025, 8, 22, 2025, 6, 29, true);
  assertLunar(2026, 2, 16, 2025, 12, 29, false);  // 除夕（腊月小）
  assertLunar(2026, 2, 17, 2026, 1, 1, false);
  assertLunar(2026, 10, 14, 2026, 9, 5, false);
  assertLunar(2033, 12, 22, 2033, 11, 1, true);  // “2033 年问题”：闰十一月
  assertLunar(2034, 1, 20, 2033, 12, 1, false);
  assertLunar(2057, 9, 28, 2057, 8, 30, false);
  assertLunar(2100, 12, 31, 2100, 12, 1, false);

  LunarDate out;
  TEST_ASSERT_FALSE(toLunar(1900, 1, 30, out));
  TEST_ASSERT_FALSE(toLunar(2101, 1, 1, out));
}

// 逐日检查：农历日期必须连续递增，月份按顺序推进
void test_continuity() {
  LunarDate prev;
  TEST_ASSERT_TRUE(toLunar(1900, 1, 31, prev));
  for (int32_t z = daysFromCivil(1900, 2, 1); z <= daysFromCivil(2100, 12, 31); z++) {
    int y, m, d;
    civilFromDays(z, y, m, d);
    LunarDate cur;
    TEST_ASSERT_TRUE(toLunar(y, m, d, cur));
    if (cur.day != 1) {
      TEST_ASSERT_EQUAL_INT(prev.day + 1, cur.day);
      TEST_ASSERT_EQUAL_INT(prev.month, cur.month);
      TEST_ASSERT_EQUAL(prev.leap, cur.leap);
    } else {
      TEST_ASSERT_EQUAL_INT(prev.monthDays, prev.day);
      if (cur.leap) {
        TEST_ASSERT_EQUAL_INT(prev.month, cur.month);
      } else if (cur.month == 1) {
        TEST_ASSERT_EQUAL_INT(12, prev.month);
        TEST_ASSERT_EQUAL_INT(prev.year + 1, cur.year);
      } else {
        TEST_ASSERT_EQUAL_INT(prev.month + 1, cur.month);
      }
    }
    prev = cur;
  }
}

void test_solar_terms() {
  TEST_ASSERT_EQUAL_INT(18, solarTermDay(2026, 3));  // 2026 雨水在 2 月 18 日（公式法的著名例外）
  TEST_ASSERT_EQUAL_INT(18, solarTermOn(2026, 10, 8));
  TEST_ASSERT_EQUAL_STRING("寒露", solarTermName(18));
  TEST_ASSERT_EQUAL_INT(23, solarTermOn(2026, 12, 22));
  TEST_ASSERT_EQUAL_INT(-1, solarTermOn(2026, 10, 14));
  TEST_ASSERT_EQUAL_INT(1, solarTermOn(2034, 1, 20));  // 大寒
  for (int y = 1901; y <= 2100; y++) {
    for (int i = 0; i < 24; i++) {
      const int day = solarTermDay(y, i);
      TEST_ASSERT_TRUE(day >= 3 && day <= 24);
    }
  }
}

void test_names() {
  TEST_ASSERT_EQUAL_STRING("丙午", ganzhiYear(2026).c_str());
  TEST_ASSERT_EQUAL_STRING("马", zodiac(2026));
  TEST_ASSERT_EQUAL_STRING("甲子", ganzhiYear(1984).c_str());
  TEST_ASSERT_EQUAL_STRING("鼠", zodiac(1984));
  TEST_ASSERT_EQUAL_STRING("甲辰", ganzhiYear(2024).c_str());

  TEST_ASSERT_EQUAL_STRING("初一", lunarDayName(1).c_str());
  TEST_ASSERT_EQUAL_STRING("初十", lunarDayName(10).c_str());
  TEST_ASSERT_EQUAL_STRING("十五", lunarDayName(15).c_str());
  TEST_ASSERT_EQUAL_STRING("二十", lunarDayName(20).c_str());
  TEST_ASSERT_EQUAL_STRING("廿三", lunarDayName(23).c_str());
  TEST_ASSERT_EQUAL_STRING("三十", lunarDayName(30).c_str());

  LunarDate ld;
  toLunar(2025, 7, 25, ld);
  TEST_ASSERT_EQUAL_STRING("闰六月", lunarMonthName(ld).c_str());
  toLunar(2026, 12, 22, ld);
  TEST_ASSERT_EQUAL_STRING("冬月", lunarMonthName(ld).c_str());
}

void test_day_labels() {
  struct Case {
    int y, m, d;
    const char* text;
    LabelKind kind;
  } cases[] = {
      {2026, 2, 16, "除夕", LabelKind::Festival},
      {2026, 2, 17, "春节", LabelKind::Festival},
      {2026, 6, 19, "端午节", LabelKind::Festival},
      {2026, 9, 25, "中秋节", LabelKind::Festival},
      {2026, 10, 1, "国庆节", LabelKind::Festival},
      {2026, 5, 10, "母亲节", LabelKind::Festival},
      {2026, 6, 21, "父亲节", LabelKind::Festival},
      {2026, 10, 8, "寒露", LabelKind::SolarTerm},
      {2026, 10, 10, "九月", LabelKind::LunarMonth},
      {2026, 10, 14, "初五", LabelKind::LunarDay},
      {2025, 7, 25, "闰六月", LabelKind::LunarMonth},
  };
  for (const auto& c : cases) {
    const DayLabel label = dayLabel(c.y, c.m, c.d);
    TEST_ASSERT_EQUAL_STRING(c.text, label.text.c_str());
    TEST_ASSERT_TRUE(c.kind == label.kind);
  }
  // 闰月不过节：2025 闰六月初七不是七夕
  LunarDate ld;
  toLunar(2025, 7, 31, ld);
  TEST_ASSERT_TRUE(ld.leap);
  TEST_ASSERT_NULL(lunarFestival(ld));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_civil_roundtrip);
  RUN_TEST(test_known_dates);
  RUN_TEST(test_continuity);
  RUN_TEST(test_solar_terms);
  RUN_TEST(test_names);
  RUN_TEST(test_day_labels);
  return UNITY_END();
}
