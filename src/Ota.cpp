#include "Ota.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFiClientSecure.h>

#include "GithubRootCAs.h"
#include "Log.h"
#include "WifiManager.h"
#include "config.h"

namespace ota {

namespace {

constexpr const char* kPrefs = "ota";
// 上次装好的 tag：重启后版本号还不是它（比如 CI 没把版本号编译进去），就不再自动重复安装，免得每天都刷一遍
constexpr const char* kTriedKey = "tried";

void setupClient(WiFiClientSecure& client, HTTPClient& http) {
  client.setCACert(kGithubRootCAs);
  client.setHandshakeTimeout(HTTP_TLS_TIMEOUT_S);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(HTTP_READ_TIMEOUT_MS);
  http.setUserAgent("esp32-epaper-calendar");  // GitHub API 要求带 User-Agent
}

std::string httpError(HTTPClient& http, int code) {
  return code > 0 ? "HTTP " + std::to_string(code) : http.errorToString(code).c_str();
}

// 查最新 Release：tag 和固件下载地址
bool fetchLatest(std::string& tag, std::string& url, std::string& error) {
  WiFiClientSecure client;
  HTTPClient http;
  setupClient(client, http);
  if (!http.begin(client, "https://api.github.com/repos/" OTA_REPO "/releases/latest")) {
    error = "URL 无效";
    return false;
  }
  http.addHeader("Accept", "application/vnd.github+json");
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    error = code == 404 ? "还没有发布过 Release" : httpError(http, code);
    http.end();
    return false;
  }
  // 响应里有发布说明、上传者信息等，只留需要的字段
  JsonDocument filter;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  JsonDocument doc;
  const String body = http.getString();
  http.end();
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) != DeserializationError::Ok) {
    error = "Release 信息格式错误";
    return false;
  }
  tag = doc["tag_name"] | "";
  for (JsonVariant asset : doc["assets"].as<JsonArray>()) {
    if (strcmp(asset["name"] | "", OTA_ASSET) == 0) url = asset["browser_download_url"] | "";
  }
  if (tag.empty()) {
    error = "Release 没有 tag";
    return false;
  }
  if (url.empty()) {
    error = tag + " 里没有 " OTA_ASSET "（CI 可能还在编译）";
    return false;
  }
  return true;
}

bool download(const std::string& url, std::string& error) {
  WiFiClientSecure client;
  HTTPClient http;
  setupClient(client, http);
  // 下载地址会跳转到 GitHub 的文件服务器（同样是 https，根证书也在列表里）
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  if (!http.begin(client, url.c_str())) {
    error = "URL 无效";
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    error = "下载失败：" + httpError(http, code);
    http.end();
    return false;
  }
  const int size = http.getSize();
  if (size <= 0) {
    error = "下载失败：不知道固件大小";
    http.end();
    return false;
  }
  if (!Update.begin(size, U_FLASH)) {
    error = std::string("无法开始写入：") + Update.errorString();
    http.end();
    return false;
  }
  const uint32_t t0 = millis();
  const size_t written = Update.writeStream(*http.getStreamPtr());
  http.end();
  // end() 会检查写入的字节数和固件头，通过后才把下次启动切到新分区
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
  std::string url;
  if (!fetchLatest(r.latest, url, r.error)) {
    LOGF("检查更新失败：%s", r.error.c_str());
    return r;
  }
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
  r.updated = download(url, r.error);
  if (r.updated) {
    prefs.putString(kTriedKey, r.latest.c_str());
  } else {
    LOGF("升级失败：%s", r.error.c_str());  // 下载失败不记录，明天再试
  }
  prefs.end();
  return r;
}

}  // namespace ota
