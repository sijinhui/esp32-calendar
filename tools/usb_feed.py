#!/usr/bin/env python3
"""板子连不上网时，用电脑代为下载数据，通过 USB 串口推给板子刷墨水屏（协议见 src/main.cpp 的 usbFeed）。

    ~/.platformio/penv/bin/python tools/usb_feed.py                     用 include/secrets.h 里的日历订阅
    ~/.platformio/penv/bin/python tools/usb_feed.py https://.../a.ics   指定订阅地址（可以写多个）
    ~/.platformio/penv/bin/python tools/usb_feed.py --every 30          每 30 分钟推一次，Ctrl+C 退出

板子要刷开发模式固件（不睡眠，USB 串口一直在）：
    PLATFORMIO_BUILD_FLAGS="-DDEBUG_MODE=1" pio run -e esp32c5 -t upload
用之前先关掉 VS Code 里的串口监视器，否则串口被占用。
"""
import argparse
import glob
import re
import ssl
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

import serial

ROOT = Path(__file__).resolve().parent.parent
CHUNK = 1024  # 和固件里的 USB_FEED_CHUNK 一致


def config_value(name):
    m = re.search(r'#define\s+%s\s+"([^"]*)"' % name, (ROOT / "include/config.h").read_text())
    return m.group(1) if m else None


def secrets_urls():
    path = ROOT / "include/secrets.h"
    if not path.exists():
        return []
    text = re.sub(r"(?m)^\s*//.*$", "", path.read_text())  # 只去掉整行注释：地址里也有 //
    m = re.search(r"ICS_URLS\[\]\s*=\s*\{(.*?)\}", text, re.S)
    return re.findall(r'"([^"]+)"', m.group(1)) if m else []


def ssl_context():
    # PlatformIO 自带的 Python 没有系统根证书，用 certifi 的
    try:
        import certifi
        return ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        return ssl.create_default_context()


SSL = ssl_context()


def download(url):
    if url.startswith("webcal://"):
        url = "https://" + url[len("webcal://"):]
    req = urllib.request.Request(url, headers={"User-Agent": "esp32-calendar"})
    with urllib.request.urlopen(req, timeout=30, context=SSL) as r:
        return r.read()


class Board:
    def __init__(self, port):
        self.ser = serial.Serial(port, 115200, timeout=0.02)  # 短超时：每块数据都要等回复，超时长了传得很慢
        time.sleep(0.5)
        self.ser.reset_input_buffer()

    def wait_reply(self, timeout):
        """等板子回 #ok / #err；中间的日志原样打出来"""
        deadline = time.time() + timeout
        buf = b""
        while time.time() < deadline:
            buf += self.ser.read(256)
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode("utf-8", "replace").rstrip("\r")
                if text.startswith("#ok"):
                    return text[3:].strip()
                if text.startswith("#err"):
                    raise RuntimeError("板子报错：" + text[4:].strip())
                if text:
                    print("  板子>", text)
        raise TimeoutError("等板子回复超时（是不是在睡眠？请刷开发模式固件）")

    def command(self, cmd, timeout=10):
        self.ser.write((cmd + "\n").encode())
        return self.wait_reply(timeout)

    def send_data(self, data):
        for off in range(0, len(data), CHUNK):
            part = data[off:off + CHUNK]
            self.ser.write(b"#data %d\n" % len(part))
            self.ser.write(part)
            self.wait_reply(10)


def push(board, urls):
    now = int(time.time())
    board.command("#time %d" % now)
    print("已对时：", time.strftime("%Y-%m-%d %H:%M:%S"))

    year = time.localtime().tm_year
    holiday_fmt = config_value("HOLIDAY_URL_FMT")
    for y in (year, year + 1):
        try:
            body = download(holiday_fmt % y)
        except urllib.error.HTTPError as e:
            print("节假日 %d：下载失败（HTTP %d，次年安排一般 11 月才公布）" % (y, e.code))
            continue
        board.command("#holiday %d" % y)
        board.send_data(body)
        board.command("#done")
        print("节假日 %d：已发送（%d 字节）" % (y, len(body)))

    for i, url in enumerate(urls, 1):
        try:
            body = download(url)
        except Exception as e:  # noqa: BLE001 单个日历失败不影响其他
            print("日历 #%d：下载失败（%s）" % (i, e))
            continue
        board.command("#ics")
        board.send_data(body)
        n = board.command("#done", timeout=30).split()[-1]
        print("日历 #%d：已发送（%d 字节，窗口内 %s 条）" % (i, len(body), n))

    print("刷新墨水屏（三色屏约 15 秒）…")
    result = board.command("#render", timeout=90)
    print("完成：日程 %s 条" % result.split()[-1])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("urls", nargs="*", help="iCal 订阅地址；不写就用 include/secrets.h 里的")
    ap.add_argument("--port", help="串口，默认自动找 /dev/cu.usbmodem*")
    ap.add_argument("--every", type=int, metavar="分钟", help="每隔几分钟推一次")
    args = ap.parse_args()

    urls = args.urls or secrets_urls()
    if not urls:
        print("没有日历订阅地址：在命令行写上，或者填到 include/secrets.h 的 ICS_URLS 里")
    port = args.port or next(iter(sorted(glob.glob("/dev/cu.usbmodem*"))), None)
    if not port:
        sys.exit("找不到板子的串口（/dev/cu.usbmodem*），检查 USB 线")
    print("串口：", port)
    board = Board(port)
    while True:
        push(board, urls)
        if not args.every:
            break
        print("%d 分钟后再推一次（Ctrl+C 退出）" % args.every)
        time.sleep(args.every * 60)


if __name__ == "__main__":
    main()
