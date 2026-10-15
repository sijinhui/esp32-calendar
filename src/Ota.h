// 在线升级：从 GitHub Release 下载 CI 编译好的固件（firmware.bin）写到另一个 app 分区，重启后生效
//   有新版本 = 最新 Release 的 latest.json 里的版本号比本机固件版本（FW_VERSION）大，按 vX.Y.Z 的数字比较
//   下载走国内镜像（OTA_MIRRORS），各试一次；写入后校验大小和 MD5，失败时继续用当前固件
//   自己编译的固件版本是 git describe（如 v1.0.2-3-gabc1234），同样参与自动升级：发了更新的 Release 就会被替换
//   网页上点“检查更新”：版本不同就装（可以把本地固件换回 Release）
#pragma once

#include <string>

namespace ota {

struct Result {
  std::string latest;  // 最新 Release 的 tag，空 = 没查到
  std::string error;   // 空 = 没出错
  bool updated = false;  // 已写入新固件（调用方负责重启）
};

const char* version();  // 本机固件版本
bool autoUpdate();      // 是否参与每天的自动升级（OTA_ENABLED）

// 查最新 Release，有新版本就下载写入。force = 手动触发（版本不同就装，包括比本机旧的；之前装过的版本也重试）
Result checkAndUpdate(bool force);

}  // namespace ota
