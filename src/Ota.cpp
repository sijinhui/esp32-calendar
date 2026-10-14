#include "Ota.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFiClientSecure.h>

#include "Log.h"
#include "OtaRootCAs.h"
#include "WifiManager.h"
#include "config.h"

namespace ota {

namespace {

constexpr const char* kPrefs = "ota";
// 上次装好的 tag：重启后版本号还不是它（比如 CI 没把版本号编译进去），就不再自动重复安装，免得每天都刷一遍
constexpr const char* kTriedKey = "tried";

// 下载来源：先走镜像（国内直连 GitHub 经常很慢或连不上），都不行再直连。"" = 直连
const char* const kSources[] = {OTA_MIRRORS ""};

struct Latest {
  std::string version;
  std::string md5;
  int size = 0;
};

// GET 一个 GitHub 地址（依次试各个来源），返回 200 的那个连接；失败时 error 是最后一个错误
bool open(const std::string& githubUrl, WiFiClientSecure& client, HTTPClient& http, std::string& error) {
  for (const char* prefix : kSources) {
    const std::string url = prefix + githubUrl;
    client.setCACert(kOtaRootCAs);
    client.setHandshakeTimeout(HTTP_TLS_TIMEOUT_S);
    http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
    http.setTimeout(HTTP_READ_TIMEOUT_MS);
    http.setUserAgent("esp32-epaper-calendar");
    // GitHub 的下载地址会跳转到文件服务器（同样是 https，根证书也在列表里）
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    if (!http.begin(client, url.c_str())) {
      error = "URL 无效";
      continue;
    }
    const int code = http.GET();
    if (code == HTTP_CODE_OK) return true;
    error = (*prefix ? std::string(prefix) : std::string("直连 GitHub")) + "：" +
            (code > 0 ? "HTTP " + std::to_string(code) : std::string(http.errorToString(code).c_str()));
    LOGF("下载失败（%s）", error.c_str());
    http.end();
    client.stop();
  }
  return false;
}

// 版本号会拼进下载地址，只允许常见的 tag 字符
bool validVersion(const std::string& v) {
  if (v.empty() || v.size() > 32) return false;
  for (char c : v) {
    if (!isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '_') return false;
  }
  return true;
}

// 最新 Release 里的 latest.json（CI 生成）：版本号、固件大小和 MD5。
// 不用 api.github.com：镜像一般只代理 github.com 的下载地址
bool fetchLatest(Latest& latest, std::string& error) {
  WiFiClientSecure client;
  HTTPClient http;
  if (!open("https://github.com/" OTA_REPO "/releases/latest/download/latest.json", client, http, error)) {
    if (error.find("HTTP 404") != std::string::npos) error = "最新 Release 里没有 latest.json（CI 可能还在编译）";
    return false;
  }
  JsonDocument doc;
  const String body = http.getString();
  http.end();
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    error = "latest.json 格式错误";
    return false;
  }
  latest.version = doc["version"] | "";
  latest.md5 = doc["md5"] | "";
  latest.size = doc["size"] | 0;
  if (!validVersion(latest.version) || latest.md5.size() != 32 || latest.size <= 0) {
    error = "latest.json 内容不完整";
    return false;
  }
  return true;
}

bool download(const Latest& latest, std::string& error) {
  WiFiClientSecure client;
  HTTPClient http;
  const std::string url = "https://github.com/" OTA_REPO "/releases/download/" + latest.version + "/" OTA_ASSET;
  if (!open(url, client, http, error)) return false;
  const int size = http.getSize();
  if (size != latest.size) {
    error = "固件大小不对（" + std::to_string(size) + "，应为 " + std::to_string(latest.size) + "）";
    http.end();
    return false;
  }
  if (!Update.begin(size, U_FLASH)) {
    error = std::string("无法开始写入：") + Update.errorString();
    http.end();
    return false;
  }
  Update.setMD5(latest.md5.c_str());  // end() 时校验，不对就不切换分区
  const uint32_t t0 = millis();
  const size_t written = Update.writeStream(*http.getStreamPtr());
  http.end();
  // end() 会检查字节数、MD5 和固件头，通过后才把下次启动切到新分区
  if (written != static_cast<size_t>(size) || !Update.end()) {
    error = "写入失败（" + std::to_string(written) + "/" + std::to_string(size) + " 字节）：" + Update.errorString();
    Update.abort();
    return false;
  }
  LOGF("固件下载写入完成：%d 字节，用时 %lu ms", size, millis() - t0);
  return true;
}

}  // namespace

const char* version() { return FW_VERSION; }

bool autoUpdate() { return OTA_ENABLED && strncmp(FW_VERSION, "dev", 3) != 0; }

Result checkAndUpdate(bool force) {
  Result r;
  if (!wifi::connected()) {
    r.error = "WiFi 未连接";
    return r;
  }
  Latest latest;
  if (!fetchLatest(latest, r.error)) {
    LOGF("检查更新失败：%s", r.error.c_str());
    return r;
  }
  r.latest = latest.version;
  if (r.latest == FW_VERSION) {
    LOGF("固件已是最新版本 %s", FW_VERSION);
    return r;
  }

  Preferences prefs;
  prefs.begin(kPrefs, false);
  const bool triedBefore = prefs.getString(kTriedKey) == r.latest.c_str();
  if (triedBefore && !force) {
    LOGF("最新版本 %s 之前装过，但现在的版本是 %s，不再自动安装（网页上可以手动检查更新）", r.latest.c_str(),
         FW_VERSION);
    prefs.end();
    return r;
  }

  LOGF("发现新版本 %s（当前 %s），开始下载", r.latest.c_str(), FW_VERSION);
  r.updated = download(latest, r.error);
  if (r.updated) {
    prefs.putString(kTriedKey, r.latest.c_str());
  } else {
    LOGF("升级失败：%s", r.error.c_str());  // 下载失败不记录，明天再试
  }
  prefs.end();
  return r;
}

}  // namespace ota
