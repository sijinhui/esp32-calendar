#include "Settings.h"

#include <Preferences.h>
#include <CalendarSubscriptions.h>

#include "Log.h"

#if __has_include("secrets.h")
#include "secrets.h"
#define HAS_SECRETS 1
#else
#define HAS_SECRETS 0
#endif

namespace settings {

namespace {

constexpr const char* kNamespace = "calendar";

String ssid, password;
std::vector<std::string> urls;
std::string subscriptionText;

std::vector<std::string> splitLines(const String& s) {
  std::vector<std::string> out;
  int start = 0;
  while (start < static_cast<int>(s.length())) {
    int end = s.indexOf('\n', start);
    if (end < 0) end = s.length();
    if (end > start) out.emplace_back(s.substring(start, end).c_str());
    start = end + 1;
  }
  return out;
}

}  // namespace

void begin() {
  Preferences prefs;
  prefs.begin(kNamespace, false);  // 只读打开一个还不存在的命名空间会报错，所以用读写模式
  if (prefs.isKey("ssid")) {
    ssid = prefs.getString("ssid");
    password = prefs.getString("pass");
  }
#if HAS_SECRETS
  else if (*WIFI_SSID) {
    // 写进 NVS：之后 OTA 换成不带 secrets.h 的 Release 固件，WiFi 也还在
    ssid = WIFI_SSID;
    password = WIFI_PASSWORD;
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
  }
#endif

  if (prefs.isKey("ics")) {
    urls = splitLines(prefs.getString("ics"));
    if (prefs.isKey("ics_text")) {
      subscriptionText = prefs.getString("ics_text").c_str();
      const auto parsed = subscriptions::parse(subscriptionText);
      // 双键写入中断等情况：旧客户端看到的 URL 与网页文本必须一致。
      if (!parsed.error.empty() || parsed.urls != urls) subscriptionText.clear();
    }
  }
#if HAS_SECRETS
  else {
    for (const char* url : ICS_URLS) {
      if (url && *url) urls.emplace_back(url);
    }
  }
#endif
  prefs.end();
  if (subscriptionText.empty()) {
    for (size_t i = 0; i < urls.size(); i++) {
      if (i) subscriptionText += '\n';
      subscriptionText += urls[i];
    }
  }

  LOGF("配置：WiFi「%s」，日历订阅 %u 个", ssid.isEmpty() ? "（未配置）" : ssid.c_str(), urls.size());
}

const String& wifiSsid() { return ssid; }
const String& wifiPassword() { return password; }

void setWifi(const String& newSsid, const String& newPassword) {
  ssid = newSsid;
  password = newPassword;
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", password);
  prefs.end();
  LOGF("已保存 WiFi「%s」", ssid.c_str());
}

const std::vector<std::string>& icsUrls() { return urls; }

const std::string& icsText() { return subscriptionText; }

// 同时保存可读文本和有效 URL，保留原有 ics 键 / API 给小程序、蓝牙使用。
void setIcsText(const std::string& text) {
  const auto parsed = subscriptions::parse(text);
  if (!parsed.error.empty()) return;
  subscriptionText = text;
  urls = parsed.urls;
  String joined;
  for (const auto& url : urls) {
    joined += url.c_str();
    joined += '\n';
  }
  Preferences prefs;
  prefs.begin(kNamespace, false);
  prefs.putString("ics", joined);
  prefs.putString("ics_text", subscriptionText.c_str());
  prefs.end();
  LOGF("已保存日历订阅 %u 个", urls.size());
}

void setIcsUrls(const std::vector<std::string>& newUrls) {
  if (newUrls == urls) {
    setIcsText(subscriptionText);  // 保存相同地址时保留网页备注，也持久化开发默认值
    return;
  }
  std::string text;
  for (size_t i = 0; i < newUrls.size(); i++) {
    if (i) text += '\n';
    text += newUrls[i];
  }
  setIcsText(text);
}

std::string checkIcsUrls(const std::vector<std::string>& list) { return subscriptions::checkUrls(list); }

}  // namespace settings
