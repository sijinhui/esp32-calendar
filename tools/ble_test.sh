#!/usr/bin/env bash
# 用电脑蓝牙测试设备（模拟微信小程序）。用法见 tools/ble_test.py 开头。
# macOS 请在系统自带的“终端”App 里运行（第一次会询问是否允许使用蓝牙）。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENV="$ROOT/.pio/ble-venv"
if [ ! -x "$VENV/bin/python" ]; then
  echo "第一次运行：安装 bleak（Python 蓝牙库）…"
  python3 -m venv "$VENV"
  "$VENV/bin/pip" install -q bleak
fi
exec "$VENV/bin/python" "$ROOT/tools/ble_test.py" "$@"
