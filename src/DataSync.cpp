#include "DataSync.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <esp_timer.h>
#include <sys/time.h>
#include <time.h>

#include <algorithm>
#include <memory>

#include "Log.h"
#include "Settings.h"
#include "WifiManager.h"
#include "config.h"

namespace net {

namespace {

constexpr uint16_t kNtpLocalPort = 4123;

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
      secure->setHandshakeTimeout(HTTP_TLS_TIMEOUT_S);
      client.reset(secure);
    } else {
      client.reset(new WiFiClient);
    }

    HTTPClient http;
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
    http.setTimeout(HTTP_READ_TIMEOUT_MS);
    http.setUserAgent("esp32-calendar");
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

namespace {

// 向一个 NTP 服务器要时间，成功时把系统时间设好，返回校正量（毫秒）。
// 不用系统自带的 SNTP：它每次启动都先随机等 0~5 秒再发请求，省电模式下每次联网都要对时，等不起
bool queryNtp(const char* host, uint32_t timeoutMs, int64_t& driftMs) {
  IPAddress ip;
  if (!WiFi.hostByName(host, ip)) return false;
  WiFiUDP udp;
  if (!udp.begin(kNtpLocalPort)) return false;
  uint8_t pkt[48] = {0x23};  // LI = 0，版本 4，模式 3（客户端）
  const int64_t sentUs = esp_timer_get_time();
  udp.beginPacket(ip, 123);
  udp.write(pkt, sizeof pkt);
  udp.endPacket();
  while (esp_timer_get_time() - sentUs < timeoutMs * 1000LL) {
    if (udp.parsePacket() >= static_cast<int>(sizeof pkt) && udp.read(pkt, sizeof pkt) == sizeof pkt) {
      const int64_t rttUs = esp_timer_get_time() - sentUs;
      if ((pkt[0] & 7) != 4 || pkt[1] == 0) break;  // 不是服务器回复，或者服务器还没对上时（stratum 0）
      auto be32 = [&](int at) {
        return static_cast<uint32_t>(pkt[at]) << 24 | pkt[at + 1] << 16 | pkt[at + 2] << 8 | pkt[at + 3];
      };
      // 发送时间戳：1900 年起的秒数 + 2^-32 秒的小数；加上半个往返时间作为现在的时间
      const int64_t serverUs = (static_cast<int64_t>(be32(40)) - 2208988800LL) * 1000000LL +
                               ((static_cast<uint64_t>(be32(44)) * 1000000ULL) >> 32) + rttUs / 2;
      timeval now;
      gettimeofday(&now, nullptr);
      driftMs = (serverUs - (now.tv_sec * 1000000LL + now.tv_usec)) / 1000;
      const timeval tv{static_cast<time_t>(serverUs / 1000000), static_cast<suseconds_t>(serverUs % 1000000)};
      settimeofday(&tv, nullptr);
      udp.stop();
      return true;
    }
    delay(5);
  }
  udp.stop();
  return false;
}

}  // namespace

bool syncTime(uint32_t timeoutMs) {
  const bool wasValid = timeValid();
  const uint32_t t0 = millis();
  for (const char* host : {NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3}) {
    const uint32_t spent = millis() - t0;
    if (spent >= timeoutMs) break;
    int64_t driftMs = 0;
    if (!queryNtp(host, std::min<uint32_t>(1500, timeoutMs - spent), driftMs)) continue;
    const time_t now = time(nullptr);
    tm t;
    localtime_r(&now, &t);
    // 校正量：浅睡眠期间时钟走得准不准
    LOGF("NTP 对时成功（%s，用时 %lu ms）：%04d-%02d-%02d %02d:%02d:%02d%s", host, millis() - t0, t.tm_year + 1900,
         t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
         wasValid ? ("，校正 " + std::to_string(driftMs) + " ms").c_str() : "");
    return true;
  }
  LOGF("NTP 对时失败");
  return false;
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
