// pio test -e native -f test_ics
#include <IcsParser.h>
#include <unity.h>

#include <cstring>
#include <string>

using namespace ics;

void setUp() {}
void tearDown() {}

static const int32_t kCST = 8 * 3600;

// 以 2026-10-01 ~ 2026-11-01 为窗口解析，按 chunk 字节分块喂入（模拟网络分包）
static Parser parse(const char* text, size_t chunk = 7) {
  Parser p(makeLocal(2026, 10, 1), makeLocal(2026, 11, 1), kCST);
  const size_t len = strlen(text);
  for (size_t i = 0; i < len; i += chunk) p.feed(text + i, std::min(chunk, len - i));
  p.finish();
  return p;
}

static std::string wrap(const char* events) {
  return std::string("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:test\r\n") + events + "END:VCALENDAR\r\n";
}

void test_single_events() {
  const std::string ics = wrap(
      "BEGIN:VEVENT\r\nUID:a\r\nDTSTART:20261014T010000Z\r\nDTEND:20261014T020000Z\r\n"
      "SUMMARY:组会\\, 周三\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:b\r\nDTSTART;VALUE=DATE:20261001\r\nDTEND;VALUE=DATE:20261008\r\n"
      "SUMMARY:国庆假期\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:c\r\nDTSTART;TZID=Asia/Shanghai:20261020T143000\r\nDURATION:PT1H30M\r\n"
      "SUMMARY:牙医\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:d\r\nDTSTART:20260920T100000\r\nSUMMARY:窗口之外\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:e\r\nDTSTART:20261015T100000\r\nSTATUS:CANCELLED\r\nSUMMARY:已取消\r\nEND:VEVENT\r\n");
  Parser p = parse(ics.c_str());
  const auto& ev = p.events();
  TEST_ASSERT_EQUAL_size_t(5, p.veventCount());
  TEST_ASSERT_EQUAL_size_t(3, ev.size());

  TEST_ASSERT_EQUAL_STRING("国庆假期", ev[0].summary.c_str());
  TEST_ASSERT_TRUE(ev[0].allDay);
  TEST_ASSERT_TRUE(ev[0].coversDay(dayOf(makeLocal(2026, 10, 7))));
  TEST_ASSERT_FALSE(ev[0].coversDay(dayOf(makeLocal(2026, 10, 8))));

  TEST_ASSERT_EQUAL_STRING("组会, 周三", ev[1].summary.c_str());
  TEST_ASSERT_EQUAL_INT64(makeLocal(2026, 10, 14, 9, 0), ev[1].start);  // UTC 01:00 → 北京 09:00
  TEST_ASSERT_EQUAL_INT64(makeLocal(2026, 10, 14, 10, 0), ev[1].end);
  TEST_ASSERT_FALSE(ev[1].allDay);

  TEST_ASSERT_EQUAL_STRING("牙医", ev[2].summary.c_str());
  TEST_ASSERT_EQUAL_INT64(makeLocal(2026, 10, 20, 16, 0), ev[2].end);
}

void test_folding_and_alarm() {
  // 折叠行（续行以空格开头）+ 嵌套 VALARM 不能覆盖事件属性；只有 LF 的换行也要支持
  const std::string ics = wrap(
      "BEGIN:VEVENT\nUID:x\nDTSTART:20261003T080000\nSUMMARY:第一部分\n 第二部分\n"
      "BEGIN:VALARM\nACTION:DISPLAY\nSUMMARY:提醒\nDTSTART:20991231T000000\nEND:VALARM\n"
      "END:VEVENT\n");
  Parser p = parse(ics.c_str(), 3);
  TEST_ASSERT_EQUAL_size_t(1, p.events().size());
  TEST_ASSERT_EQUAL_STRING("第一部分第二部分", p.events()[0].summary.c_str());
  TEST_ASSERT_EQUAL_INT64(makeLocal(2026, 10, 3, 8, 0), p.events()[0].start);

  // 在多字节字符中间折叠
  const char* split =
      "BEGIN:VEVENT\r\nUID:y\r\nDTSTART:20261004T080000\r\nSUMMARY:\xe4\xbd\r\n \xa0\xe5\xa5\xbd\r\nEND:VEVENT\r\n";
  Parser p2 = parse(wrap(split).c_str());
  TEST_ASSERT_EQUAL_STRING("你好", p2.events()[0].summary.c_str());
}

void test_weekly_rrule_with_exceptions() {
  // 每周一、三 10:00，2026-09-07 开始；10-14 那次被删除，10-19 那次改到 15:00
  const std::string ics = wrap(
      "BEGIN:VEVENT\r\nUID:w\r\nDTSTART;TZID=Asia/Shanghai:20260907T100000\r\n"
      "DTEND;TZID=Asia/Shanghai:20260907T110000\r\nRRULE:FREQ=WEEKLY;BYDAY=MO,WE\r\n"
      "EXDATE;TZID=Asia/Shanghai:20261014T100000\r\nSUMMARY:例会\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:w\r\nRECURRENCE-ID;TZID=Asia/Shanghai:20261019T100000\r\n"
      "DTSTART;TZID=Asia/Shanghai:20261019T150000\r\nDTEND;TZID=Asia/Shanghai:20261019T160000\r\n"
      "SUMMARY:例会（改期）\r\nEND:VEVENT\r\n");
  Parser p = parse(ics.c_str());
  const auto& ev = p.events();
  // 10 月的周一、周三：5 7 12 14 19 21 26 28 → 去掉 14，19 改期 → 仍是 7 条
  TEST_ASSERT_EQUAL_size_t(7, ev.size());
  const int days[] = {5, 7, 12, 19, 21, 26, 28};
  for (int i = 0; i < 7; i++) {
    TEST_ASSERT_EQUAL_INT32(dayOf(makeLocal(2026, 10, days[i])), dayOf(ev[i].start));
  }
  TEST_ASSERT_EQUAL_STRING("例会（改期）", ev[3].summary.c_str());
  TEST_ASSERT_EQUAL_INT64(makeLocal(2026, 10, 19, 15, 0), ev[3].start);
  TEST_ASSERT_EQUAL_INT64(makeLocal(2026, 10, 21, 11, 0), ev[4].end);
}

void test_daily_count_until_interval() {
  const std::string ics = wrap(
      // 每天一次，共 5 次：9-29 ~ 10-3 → 窗口内 3 次
      "BEGIN:VEVENT\r\nUID:d1\r\nDTSTART:20260929T070000\r\nRRULE:FREQ=DAILY;COUNT=5\r\nSUMMARY:晨跑\r\nEND:VEVENT\r\n"
      // 每两天一次，直到 10-06（含）：10-02 04 06 → 3 次
      "BEGIN:VEVENT\r\nUID:d2\r\nDTSTART:20261002T200000\r\nRRULE:FREQ=DAILY;INTERVAL=2;UNTIL=20261006T235959Z\r\n"
      "SUMMARY:吃药\r\nEND:VEVENT\r\n"
      // 工作日每天，2020 年开始（测试跳过窗口之前的大量重复）：10 月 22 个工作日
      "BEGIN:VEVENT\r\nUID:d3\r\nDTSTART:20200106T090000\r\nRRULE:FREQ=DAILY;BYDAY=MO,TU,WE,TH,FR\r\n"
      "SUMMARY:站会\r\nEND:VEVENT\r\n");
  Parser p = parse(ics.c_str());
  int run = 0, med = 0, standup = 0;
  for (const auto& e : p.events()) {
    if (e.summary == "晨跑") run++;
    if (e.summary == "吃药") med++;
    if (e.summary == "站会") standup++;
  }
  TEST_ASSERT_EQUAL_INT(3, run);
  TEST_ASSERT_EQUAL_INT(3, med);
  TEST_ASSERT_EQUAL_INT(22, standup);
}

void test_monthly_yearly() {
  const std::string ics = wrap(
      // 生日：每年 10-24 全天
      "BEGIN:VEVENT\r\nUID:y1\r\nDTSTART;VALUE=DATE:19900124\r\nRRULE:FREQ=YEARLY;BYMONTH=10;BYMONTHDAY=24\r\n"
      "SUMMARY:生日\r\nEND:VEVENT\r\n"
      // 每月最后一个周五
      "BEGIN:VEVENT\r\nUID:m1\r\nDTSTART:20260130T180000\r\nRRULE:FREQ=MONTHLY;BYDAY=-1FR\r\n"
      "SUMMARY:月末聚餐\r\nEND:VEVENT\r\n"
      // 每月 31 号（10 月有 31 号）
      "BEGIN:VEVENT\r\nUID:m2\r\nDTSTART:20260131T090000\r\nRRULE:FREQ=MONTHLY\r\nSUMMARY:还信用卡\r\nEND:VEVENT\r\n"
      // 不支持的规则：只显示第一次（在窗口外 → 不显示）
      "BEGIN:VEVENT\r\nUID:u1\r\nDTSTART:20260101T090000\r\nRRULE:FREQ=MONTHLY;BYDAY=1MO,3MO\r\n"
      "SUMMARY:不支持\r\nEND:VEVENT\r\n");
  Parser p = parse(ics.c_str());
  const auto& ev = p.events();
  TEST_ASSERT_EQUAL_size_t(3, ev.size());
  TEST_ASSERT_EQUAL_size_t(1, p.unsupportedRules());
  TEST_ASSERT_EQUAL_STRING("生日", ev[0].summary.c_str());
  TEST_ASSERT_EQUAL_INT64(makeLocal(2026, 10, 24), ev[0].start);
  TEST_ASSERT_TRUE(ev[0].allDay);
  TEST_ASSERT_EQUAL_STRING("月末聚餐", ev[1].summary.c_str());
  TEST_ASSERT_EQUAL_INT64(makeLocal(2026, 10, 30, 18, 0), ev[1].start);
  TEST_ASSERT_EQUAL_STRING("还信用卡", ev[2].summary.c_str());
  TEST_ASSERT_EQUAL_INT64(makeLocal(2026, 10, 31, 9, 0), ev[2].start);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_events);
  RUN_TEST(test_folding_and_alarm);
  RUN_TEST(test_weekly_rrule_with_exceptions);
  RUN_TEST(test_daily_count_until_interval);
  RUN_TEST(test_monthly_yearly);
  return UNITY_END();
}
