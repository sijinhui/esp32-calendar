// 在线升级：从 GitHub Release 下载 CI 编译好的固件（firmware.bin）写到另一个 app 分区，重启后生效
//   有新版本 = 最新 Release 的 latest.json 里的版本跟本机固件版本（FW_VERSION，CI 用 tag 名编译进去）不同
//   下载走国内镜像（OTA_MIRRORS），各试一次；写入后校验大小和 MD5，失败时继续用当前固件
//   自己编译的固件版本是 dev，不自动升级（免得被 Release 覆盖），网页上点“检查更新”可以手动升级
#pragma once

#include <string>

namespace ota {

struct Result {
  std::string latest;  // 最新 Release 的 tag，空 = 没查到
  std::string error;   // 空 = 没出错
  bool updated = false;  // 已写入新固件（调用方负责重启）
};

const char* version();  // 本机固件版本
bool autoUpdate();      // 是否参与每天的自动升级（dev 版本不参与）

// 查最新 Release，有新版本就下载写入。force = 手动触发（dev 版本也升级，之前失败过的版本也重试）
Result checkAndUpdate(bool force);

}  // namespace ota
