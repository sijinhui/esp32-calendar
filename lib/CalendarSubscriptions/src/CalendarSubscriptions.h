// 订阅文本解析：支持整行 # 注释和空白分隔的行尾注释，不改动 URL 内的 #。
// 纯 C++，设备配置和 native 单元测试共用。
#pragma once

#include <string>
#include <vector>

namespace subscriptions {

constexpr size_t kMaxSources = 8;
constexpr size_t kMaxTextBytes = 3000;

struct Result {
  std::vector<std::string> urls;
  std::string error;
};

std::string checkUrls(const std::vector<std::string>& urls);
Result parse(const std::string& text);

}  // namespace subscriptions
