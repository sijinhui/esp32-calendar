// pio test -e native -f test_holiday
#include <HolidayCN.h>
#include <unity.h>

#include <cstring>

void setUp() {}
void tearDown() {}

// holiday-cn 2026.json 的节选
static const char* k2026 = R"({
  "year": 2026,
  "papers": ["https://www.gov.cn/zhengce/zhengceku/202511/content_7047091.htm"],
  "days": [
    {"name": "元旦", "date": "2026-01-01", "isOffDay": true},
    {"name": "元旦", "date": "2026-01-04", "isOffDay": false},
    {"name": "春节", "date": "2026-02-14", "isOffDay": false},
    {"name": "春节", "date": "2026-02-15", "isOffDay": true},
    {"name": "国庆节", "date": "2026-10-01", "isOffDay": true}
  ]
})";

void test_lookup() {
  HolidayCN h;
  TEST_ASSERT_EQUAL_INT(5, h.loadJson(k2026, strlen(k2026)));
  TEST_ASSERT_TRUE(h.hasYear(2026));
  TEST_ASSERT_FALSE(h.hasYear(2027));

  auto info = h.lookup(2026, 10, 1);
  TEST_ASSERT_TRUE(info.status == HolidayCN::Status::OffDay);
  TEST_ASSERT_EQUAL_STRING("国庆节", info.name.c_str());

  info = h.lookup(2026, 2, 14);
  TEST_ASSERT_TRUE(info.status == HolidayCN::Status::WorkDay);

  info = h.lookup(2026, 10, 14);
  TEST_ASSERT_TRUE(info.status == HolidayCN::Status::Normal);
  TEST_ASSERT_TRUE(info.name.empty());
}

void test_reload_replaces_year() {
  HolidayCN h;
  h.loadJson(k2026, strlen(k2026));
  // 另一年的文件里包含跨年调休日，不应影响 2026 的数据
  const char* y2027 = R"({"year": 2027, "days": [
    {"name": "元旦", "date": "2026-12-31", "isOffDay": true},
    {"name": "元旦", "date": "2027-01-01", "isOffDay": true}]})";
  TEST_ASSERT_EQUAL_INT(2, h.loadJson(y2027, strlen(y2027)));
  // 重新加载 2026（数据更新），旧条目被替换，2027 文件带来的 2026-12-31 保留
  const char* updated = R"({"year": 2026, "days": [{"name": "国庆节", "date": "2026-10-02", "isOffDay": true}]})";
  TEST_ASSERT_EQUAL_INT(1, h.loadJson(updated, strlen(updated)));
  TEST_ASSERT_TRUE(h.lookup(2026, 10, 1).status == HolidayCN::Status::Normal);
  TEST_ASSERT_TRUE(h.lookup(2026, 10, 2).status == HolidayCN::Status::OffDay);
  TEST_ASSERT_TRUE(h.lookup(2026, 12, 31).status == HolidayCN::Status::OffDay);
  TEST_ASSERT_TRUE(h.lookup(2027, 1, 1).status == HolidayCN::Status::OffDay);
}

void test_bad_json() {
  HolidayCN h;
  const char* bad = "<html>404</html>";
  TEST_ASSERT_EQUAL_INT(-1, h.loadJson(bad, strlen(bad)));
  const char* noDays = R"({"year": 2026})";
  TEST_ASSERT_EQUAL_INT(-1, h.loadJson(noDays, strlen(noDays)));
  TEST_ASSERT_FALSE(h.hasYear(2026));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_lookup);
  RUN_TEST(test_reload_replaces_year);
  RUN_TEST(test_bad_json);
  return UNITY_END();
}
