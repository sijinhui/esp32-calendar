#include <CalendarSubscriptions.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_comments_and_blank_lines() {
  const auto r = subscriptions::parse(
      "  # Outlook 工作日历\r\n\n https://example.com/work.ics # 工作安排\r\n"
      "\t# 家庭日历\nwebcal://example.com/family.ics\t# 家庭\n"
      "# https://example.com/disabled.ics\n http://example.com/local.ics\n");
  TEST_ASSERT_TRUE(r.error.empty());
  TEST_ASSERT_EQUAL_UINT(3, r.urls.size());
  TEST_ASSERT_EQUAL_STRING("https://example.com/work.ics", r.urls[0].c_str());
  TEST_ASSERT_EQUAL_STRING("webcal://example.com/family.ics", r.urls[1].c_str());
  TEST_ASSERT_EQUAL_STRING("http://example.com/local.ics", r.urls[2].c_str());
}

void test_url_hash_is_preserved() {
  const auto r = subscriptions::parse("https://example.com/a.ics?token=a%23b#fragment # 备注");
  TEST_ASSERT_TRUE(r.error.empty());
  TEST_ASSERT_EQUAL_STRING("https://example.com/a.ics?token=a%23b#fragment", r.urls[0].c_str());
}

void test_empty_and_comment_only() {
  TEST_ASSERT_TRUE(subscriptions::parse("").urls.empty());
  const auto r = subscriptions::parse(" # 工作\n\r\n# https://example.com/a.ics\n");
  TEST_ASSERT_TRUE(r.error.empty());
  TEST_ASSERT_TRUE(r.urls.empty());
}

void test_invalid_line_number() {
  const auto r = subscriptions::parse("# 备注\n\n不是订阅地址\nhttps://example.com/a.ics");
  TEST_ASSERT_TRUE(r.error.find("第 3 行") != std::string::npos);
}

void test_source_limit() {
  std::string text;
  for (int i = 0; i < 8; i++) text += "# 备注\nhttps://example.com/" + std::to_string(i) + ".ics\n";
  TEST_ASSERT_TRUE(subscriptions::parse(text).error.empty());
  text += "# https://example.com/disabled.ics\n";
  TEST_ASSERT_TRUE(subscriptions::parse(text).error.empty());
  text += "https://example.com/extra.ics";
  TEST_ASSERT_FALSE(subscriptions::parse(text).error.empty());
}

void test_byte_limits() {
  TEST_ASSERT_TRUE(subscriptions::parse("#" + std::string(2999, 'x')).error.empty());
  TEST_ASSERT_FALSE(subscriptions::parse("#" + std::string(3000, 'x')).error.empty());
  std::string text = "#";
  for (int i = 0; i < 1000; i++) text += "中";
  TEST_ASSERT_FALSE(subscriptions::parse(text).error.empty());
  TEST_ASSERT_FALSE(subscriptions::checkUrls({"ftp://example.com/a.ics"}).empty());
  TEST_ASSERT_TRUE(subscriptions::checkUrls({"https://example.com/a.ics"}).empty());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_comments_and_blank_lines);
  RUN_TEST(test_url_hash_is_preserved);
  RUN_TEST(test_empty_and_comment_only);
  RUN_TEST(test_invalid_line_number);
  RUN_TEST(test_source_limit);
  RUN_TEST(test_byte_limits);
  return UNITY_END();
}
