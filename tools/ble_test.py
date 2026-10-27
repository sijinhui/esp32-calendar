#!/usr/bin/env python3
"""用电脑蓝牙测试设备的蓝牙通道（模拟微信小程序），协议见 src/BleControl.h。

    tools/ble_test.sh                     只读测试：搜索设备、查状态、扫 WiFi、检查错误处理（不改设备配置）
    tools/ble_test.sh --wifi 家里的WiFi    再用蓝牙给设备配网（密码在终端里输入，不会显示）

macOS 要在系统自带的“终端”App 里运行，第一次会询问是否允许“终端”使用蓝牙。
"""
import argparse
import asyncio
import getpass
import json
import sys

from bleak import BleakClient, BleakScanner

SERVICE = "9b6d0001-3c1a-4f7e-a1d2-5e8c7b4a2f10"
RX = "9b6d0002-3c1a-4f7e-a1d2-5e8c7b4a2f10"
TX = "9b6d0003-3c1a-4f7e-a1d2-5e8c7b4a2f10"

passed = failed = 0


def check(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
    else:
        failed += 1
    print(f"  [{'通过' if ok else '失败'}] {name}" + (f"：{detail}" if detail else ""))


def parse_mfg(data):
    i = data.find(b"EP\x01")
    if i < 0 or len(data) < i + 8:
        return None
    flags = data[i + 3]
    return {"provisioning": bool(flags & 1), "online": bool(flags & 2), "ip": ".".join(str(b) for b in data[i + 4:i + 8])}


async def main(args):
    print("1. 搜索设备（8 秒）…")
    found = {}

    def on_adv(dev, adv):
        if SERVICE in [u.lower() for u in adv.service_uuids]:
            found[dev.address] = (dev, adv)

    async with BleakScanner(on_adv):
        await asyncio.sleep(8)
    if not found:
        print("  没有搜到设备：确认设备已通电、在附近，并且终端有蓝牙权限")
        return 1
    dev, adv = max(found.values(), key=lambda x: x[1].rssi)
    info = None
    for data in adv.manufacturer_data.values():
        info = parse_mfg(b"\xff\xff" + data) or info
    print(f"  {adv.local_name}  信号 {adv.rssi} dBm  广播信息 {info}")
    check("广播里有设备名", bool(adv.local_name and adv.local_name.startswith("EPaper-")))
    check("广播里有状态 / IP", info is not None)

    lines = asyncio.Queue()
    buf = bytearray()

    def on_notify(_, data):
        buf.extend(data)
        while b"\n" in buf:
            i = buf.index(b"\n")
            lines.put_nowait(bytes(buf[:i]).decode("utf-8", "replace"))
            del buf[:i + 1]

    print("2. 连接…")
    async with BleakClient(dev) as client:
        print(f"  已连接，MTU {client.mtu_size}")
        await client.start_notify(TX, on_notify)

        async def cmd(obj, until=lambda m: True, timeout=25):
            data = (obj if isinstance(obj, str) else json.dumps(obj, ensure_ascii=False)) + "\n"
            data = data.encode("utf-8")
            for i in range(0, len(data), 20):  # 和小程序一样每次写 20 字节
                await client.write_gatt_char(RX, data[i:i + 20], response=True)
            while True:
                msg = json.loads(await asyncio.wait_for(lines.get(), timeout))
                if until(msg):
                    return msg

        print("3. 命令测试")
        st = await cmd({"cmd": "status"})
        check("status", "free_heap" in st, f"WiFi={'已连接 ' + st.get('ip', '') if st.get('wifi') else '未连接'}，"
              f"配网模式={st.get('provisioning')}，日历 {st.get('calendars')} 个")
        sc = await cmd({"cmd": "scan"})
        aps = sc.get("aps", [])
        check("scan", len(aps) > 0, f"{len(aps)} 个 WiFi：" + "、".join(a["ssid"] for a in aps[:5]))
        r = await cmd("这不是 JSON")
        check("格式错误的命令", "error" in r, r.get("error", ""))
        r = await cmd({"cmd": "nope"})
        check("未知命令", "error" in r, r.get("error", ""))
        # 不在配网模式时，改配置的命令会先被权限检查挡住，测不到参数校验
        prov = st.get("provisioning")
        r = await cmd({"cmd": "wifi", "ssid": "x", "password": "123"})
        check("太短的密码被拒绝" if prov else "非配网模式下不能改 WiFi", "error" in r, r.get("error", ""))
        r = await cmd({"cmd": "ics", "urls": ["ftp://x"]})
        check("错误的日历地址被拒绝" if prov else "非配网模式下不能改日历", "error" in r, r.get("error", ""))
        if not prov:
            print("  （长按 BOOT 键 3 秒进入配网模式后再跑一次，可以测到参数校验和蓝牙配网）")

        if args.wifi:
            password = getpass.getpass(f"4. 输入「{args.wifi}」的密码（不会显示，没有密码直接回车）：")
            print("  设备正在连接，最多 15 秒…")
            r = await cmd({"cmd": "wifi", "ssid": args.wifi, "password": password},
                          until=lambda m: m.get("state") != "connecting" or "error" in m, timeout=40)
            ok = r.get("state") == "connected"
            check("蓝牙配网", ok, f"设备 IP {r.get('ip')}" if ok else r.get("error", str(r)))
            if ok:
                print(f"  在同一个 WiFi 下打开 http://{r.get('ip')}/ 或 http://esp32-calendar.local/")
        await client.stop_notify(TX)

    print(f"\n通过 {passed} 项，失败 {failed} 项")
    return 1 if failed else 0


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--wifi", metavar="SSID", help="测试完再用蓝牙给设备配这个 WiFi")
    sys.exit(asyncio.run(main(p.parse_args())))
