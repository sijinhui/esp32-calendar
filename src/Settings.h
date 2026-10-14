// 用户配置：WiFi 和 iCal 订阅地址。保存在 NVS 里，掉电不丢，重新烧录固件也不丢。
// 还没保存过时使用 include/secrets.h 里的默认值（可选文件）。
#pragma once

#include <Arduino.h>

#include <string>
#include <vector>

namespace settings {

void begin();

const String& wifiSsid();  // 空 = 还没配置 WiFi
const String& wifiPassword();
void setWifi(const String& ssid, const String& password);

const std::vector<std::string>& icsUrls();
const std::string& icsText();  // 网页原始文本，保留注释 / 空行；下载只用 icsUrls()
void setIcsText(const std::string& text);  // 先用 subscriptions::parse 验证再调用
void setIcsUrls(const std::vector<std::string>& urls);
std::string checkIcsUrls(const std::vector<std::string>& urls);  // 返回错误提示，空 = 没问题

}  // namespace settings
