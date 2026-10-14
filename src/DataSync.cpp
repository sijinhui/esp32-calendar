#include "DataSync.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include <memory>

#include "Log.h"
#include "Settings.h"
#include "WifiManager.h"
#include "config.h"

namespace net {

namespace {

// 把 HTTP 响应体直接喂给 iCal 解析器（边下边解析，不占用大块内存）
class ParserSink : public Stream {
 public:
  explicit ParserSink(ics::Parser& parser) : parser_(parser) {}
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* buf, size_t n) override {
    parser_.feed(reinterpret_cast<const char*>(buf), n);
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() {}

 private:
  ics::Parser& parser_;
};

// 跳转地址：Location 可能是完整 URL、以 / 开头的绝对路径，或者相对路径
String resolveLocation(const String& base, const String& location) {
  if (location.indexOf("://") > 0) return location;
  const int hostStart = base.indexOf("://") + 3;
  const int pathStart = base.indexOf('/', hostStart);
  const String origin = pathStart < 0 ? base : base.substring(0, pathStart);
  if (location.startsWith("/")) return origin + location;
  const String dir = pathStart < 0 ? origin + "/" : base.substring(0, base.lastIndexOf('/') + 1);
  return dir + location;
}

// GET 请求：200 时把响应写入 sink（流式）或 body（字符串）。返回 HTTP 状态码或负数错误码
// 跳转自己处理：HTTPClient 不跟随换协议的跳转（http ↔ https），而日历服务经常这样跳
int httpGet(String url, Stream* sink, String* body, std::string& error) {
  if (url.startsWith("webcal://")) url = "https://" + url.substring(9);
  for (int hop = 0;; hop++) {
    std::unique_ptr<WiFiClient> client;
    if (url.startsWith("https://")) {
      auto* secure = new WiFiClientSecure;
      secure->setInsecure();  // 不校验证书：省去维护根证书的麻烦，数据是只读的日历，风险可接受
      client.reset(secure);
    } else {
      client.reset(new WiFiClient);
    }

    HTTPClient http;
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setTimeout(20000);
    http.setUserAgent("esp32-epaper-calendar");
    if (!http.begin(*client, url)) {
      error = "URL 无效";
      return -1;
    }
    int code = http.GET();
    const bool redirect = code == 301 || code == 302 || code == 303 || code == 307 || code == 308;
    if (redirect && hop < 5 && http.getLocation().length()) {
      url = resolveLocation(url, http.getLocation());
      http.end();
      continue;
    }
    if (code == HTTP_CODE_OK) {
      if (sink) {
        const int n = http.writeToStream(sink);
        if (n < 0) {
          error = http.errorToString(n).c_str();
          code = n;
        }
      } else if (body) {
        *body = http.getString();
      }
    } else {
      error = code > 0 ? "HTTP " + std::to_string(code) : http.errorToString(code).c_str();
      if (redirect) error += "（跳转次数过多）";
    }
    http.end();
    return code;
  }
}

}  // namespace

// ---------- 时间 ----------

bool syncTime(uint32_t timeoutMs) {
  configTzTime(TZ_POSIX, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  tm t;
  if (!getLocalTime(&t, timeoutMs)) {
    LOGF("NTP 对时超时");
    return false;
  }
  LOGF("NTP 对时成功：%04d-%02d-%02d %02d:%02d:%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour,
       t.tm_min, t.tm_sec);
  return true;
}

bool timeValid() { return time(nullptr) > 1700000000; }  // 2023 年之后才算对过时

// ---------- 法定节假日 ----------

void loadHolidays(HolidayCN& holidays, int year, bool online) {
  char path[32];
  snprintf(path, sizeof path, "/holiday-%d.json", year);

  if (online && wifi::connected()) {
    char url[128];
    snprintf(url, sizeof url, HOLIDAY_URL_FMT, year);
    String body;
    std::string error;
    const int code = httpGet(url, nullptr, &body, error);
    if (code == HTTP_CODE_OK && holidays.loadJson(body.c_str(), body.length()) >= 0) {
      File f = LittleFS.open(path, "w");
      if (f) {
        f.print(body);
        f.close();
      }
      LOGF("节假日数据 %d 已更新（%u 字节）", year, body.length());
      return;
    }
    // 次年的安排一般 11 月才公布，之前 404 是正常的
    LOGF("节假日数据 %d 下载失败：%s", year, code == HTTP_CODE_OK ? "格式错误" : error.c_str());
  }

  if (holidays.hasYear(year) || !LittleFS.exists(path)) return;
  File f = LittleFS.open(path, "r");
  if (!f) return;
  const String body = f.readString();
  f.close();
  if (holidays.loadJson(body.c_str(), body.length()) >= 0) LOGF("节假日数据 %d 从缓存加载", year);
}

// ---------- iCal ----------

int icsSourceCount() { return settings::icsUrls().size(); }

IcsResult fetchCalendars(ics::LocalTime from, ics::LocalTime to, std::vector<ics::Event>& events) {
  IcsResult result;
  result.sources = icsSourceCount();
  if (result.sources == 0) {
    events.clear();
    return result;
  }
  if (!wifi::connected()) {
    result.error = "WiFi 未连接";
    return result;
  }

  std::vector<ics::Event> merged;
  int index = 0;
  for (const std::string& url : settings::icsUrls()) {
    index++;
    const uint32_t t0 = millis();
    ics::Parser parser(from, to, TZ_OFFSET_SEC);
    ParserSink sink(parser);
    std::string error;
    const int code = httpGet(url.c_str(), &sink, nullptr, error);
    parser.finish();
    if (code != HTTP_CODE_OK) {
      LOGF("日历 #%d 同步失败：%s", index, error.c_str());
      result.error = error;
      continue;
    }
    LOGF("日历 #%d：%u 个 VEVENT，窗口内 %u 条，用时 %lu ms%s", index, parser.veventCount(),
         parser.events().size(), millis() - t0,
         parser.unsupportedRules() ? "（有不支持的重复规则，只显示首次）" : "");
    merged.insert(merged.end(), parser.events().begin(), parser.events().end());
    result.ok++;
  }

  if (result.ok > 0) {
    ics::sortEvents(merged);
    events.swap(merged);
  }
  return result;
}

}  // namespace net
