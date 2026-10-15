# 自己编译的固件也带版本号：用 git describe 生成（如 v1.0.2-3-gabc1234-dirty），写到编译目录的 fw_version.h。
# CI 编译 Release 时用 -DFW_VERSION 传 tag 名，优先于这里（fw_version.h 里是 #ifndef）。
# 内容没变就不重写，免得每次都重新编译 Ota.cpp
import os
import subprocess

Import("env")

try:
    version = subprocess.check_output(
        ["git", "describe", "--tags", "--match", "v*", "--always", "--dirty"],
        cwd=env["PROJECT_DIR"], stderr=subprocess.DEVNULL, text=True).strip()
except Exception:
    version = ""
version = version or "dev"

out_dir = os.path.join(env.subst("$BUILD_DIR"), "generated")
os.makedirs(out_dir, exist_ok=True)
path = os.path.join(out_dir, "fw_version.h")
content = '#pragma once\n#ifndef FW_VERSION\n#define FW_VERSION "%s"\n#endif\n' % version
old = open(path).read() if os.path.exists(path) else ""
if old != content:
    with open(path, "w") as f:
        f.write(content)
env.Append(CPPPATH=[out_dir])
print("固件版本：%s（CI 用 -DFW_VERSION 时以那个为准）" % version)
