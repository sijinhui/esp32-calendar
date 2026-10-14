#include "CalendarSubscriptions.h"

namespace subscriptions {
namespace {
bool whitespace(char c) { return c == ' ' || c == '\t' || c == '\r'; }
std::string trim(const std::string& s) {
  size_t first = 0, last = s.size();
  while (first < last && whitespace(s[first])) first++;
  while (last > first && whitespace(s[last - 1])) last--;
  return s.substr(first, last - first);
}
bool validUrl(const std::string& url) {
  return url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0 || url.rfind("webcal://", 0) == 0;
}
}  // namespace

std::string checkUrls(const std::vector<std::string>& urls) {
  if (urls.size() > kMaxSources) return "最多 8 个订阅地址（注释不计入）";
  size_t total = 0;
  for (const auto& url : urls) {
    if (!validUrl(url)) return "订阅地址要以 https://、http:// 或 webcal:// 开头";
    total += url.size() + 1;
  }
  if (total > kMaxTextBytes) return "订阅地址太长";
  return "";
}

Result parse(const std::string& text) {
  Result result;
  if (text.size() > kMaxTextBytes) {
    result.error = "订阅内容太长（含注释最多 3000 字节）";
    return result;
  }
  size_t start = 0, lineNumber = 0;
  while (start < text.size()) {
    const size_t end = text.find('\n', start);
    std::string line = trim(text.substr(start, end == std::string::npos ? end : end - start));
    lineNumber++;
    if (!line.empty() && line[0] != '#') {
      for (size_t i = 1; i < line.size(); i++) {
        if (line[i] == '#' && whitespace(line[i - 1])) {
          line = trim(line.substr(0, i));
          break;
        }
      }
      if (!validUrl(line)) {
        result.error = "第 " + std::to_string(lineNumber) + " 行：订阅地址要以 https://、http:// 或 webcal:// 开头";
        return result;
      }
      result.urls.push_back(line);
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  result.error = checkUrls(result.urls);
  return result;
}

}  // namespace subscriptions
