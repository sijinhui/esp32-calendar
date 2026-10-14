# ESP32-S3 墨水屏日历

ESP32-S3 + 4.2 寸三色（黑白红）墨水屏，400×300。

- 公历月历 + 农历 + 二十四节气 + 传统 / 公历节日（离线计算，1900–2100 年）
- 法定节假日与调休（“休 / 班”角标，联网获取 [holiday-cn](https://github.com/NateScarlet/holiday-cn) 数据并缓存）
- 在线日程：Google / iCloud / Outlook 的 iCal 订阅链接，支持重复事件
- **手机配网**：设备开热点 + 自动弹出设置页，或者用微信小程序走蓝牙；不用改代码重新烧录
- **屏幕没到也能开发**：浏览器里看“虚拟屏幕”，或者直接在电脑上渲染预览图

## 目录

```
include/config.h           引脚、屏幕驱动、时区、同步间隔、配网等配置
include/secrets.h          可选：开发用的默认 WiFi / 日历（不提交到 git）
src/main.cpp               主流程：联网 → 对时 → 同步 → 绘制 → 刷屏
src/Settings.*             用户配置（WiFi、日历订阅），保存在 NVS
src/WifiManager.*          WiFi 连接、断线重连、配网模式（热点 + 强制门户）
src/BleControl.*           蓝牙通道（小程序配网），协议见头文件
src/CalendarRenderer.*     画面排版（设备和电脑预览共用）
src/Canvas3C.*             三色帧缓冲
src/EpdOutput.*            GxEPD2 墨水屏输出（EPD_ENABLED=0 时为空操作）
src/WebPreview.*           设备网页（画面预览、设置页）和 JSON 接口
src/DataSync.*             WiFi / NTP / 节假日 / iCal 下载
lib/ChineseCalendar/       农历、节气、节日（纯 C++）
lib/HolidayCN/             法定节假日数据解析（纯 C++）
lib/IcsParser/             流式 iCal 解析与重复事件展开（纯 C++）
lib/CalendarSubscriptions/ 订阅文本 / 注释解析与限制校验（纯 C++）
lib/QrCodeGen/             二维码生成（nayuki/QR-Code-generator v1.8.0，MIT）
miniprogram/               微信小程序：蓝牙配网、查看画面、管理日历订阅
test/                      日历、节假日、iCal、订阅文本的单元测试（在电脑上跑）
tools/preview.sh           电脑上渲染预览图
tools/ble_test.sh          用电脑蓝牙测试设备（模拟小程序）
docs/配网说明.md            配网步骤和常见问题
tools/gen_lunar_data.py    重新生成农历 / 节气数据表
```

## 开始

1. 编译烧录：VSCode 底部 PlatformIO 工具栏点 → Upload，或者
   ```bash
   pio run -e esp32s3 -t upload
   ```
2. 看串口日志：PlatformIO 工具栏点插头图标，或者 `pio device monitor`。开发板上两个 USB 口插哪个都行，日志会同时输出到两边。
3. 配网（见下一节），设备连上 WiFi 后会自动对时、同步。
4. 浏览器打开 `http://epaper-calendar.local/`（或者串口日志里打印的 IP），看到的就是屏幕上的画面。
   可以选任意日期预览（比如春节、国庆），也可以手动触发同步；点“设置”添加日历订阅。

> 分区按 N16R8 模组（16MB Flash + 8MB PSRAM）配置，留了两个固件分区给以后的 OTA 升级。
> 换成 4MB 的模组要把 `platformio.ini` 改回 `huge_app.csv`，并去掉 PSRAM 相关的两行。

## 配网

设备在这些情况下进入配网模式：第一次开机（没配过 WiFi）、已保存的 WiFi 连续 30 秒连不上、长按开发板上的 BOOT 键 3 秒。
配网模式下设备开一个热点 `EPaper-XXXX`，同时打开蓝牙；墨水屏上显示连热点的二维码和步骤。两种方式任选：

- **热点 + 网页**（什么都不用装）：手机扫屏幕上的二维码（或在 WiFi 列表里选 `EPaper-XXXX`），连上后会自动弹出设置页，
  没弹出就用浏览器打开 `http://192.168.4.1/`。选 WiFi、输密码，顺便可以填日历订阅地址。
- **微信小程序**（蓝牙）：见下面。

新 WiFi 连上之后才会保存，密码输错不会把原来能用的配置弄丢。连上路由器后热点自动关闭；路由器断电恢复后设备也会自己重连。
配置保存在 NVS 里，重新烧录固件不会丢；要清空可以 `pio run -t erase` 后重新烧录。

## 网页日历订阅与备注

在设备网页的“设置 → 日历订阅”中，每行填写一个 ICS 地址，最多启用 8 个订阅：

```text
# Outlook 工作日历
https://example.com/work.ics # 工作安排

# 家庭日历
webcal://example.com/family.ics

# 暂时停用某个订阅：在地址前加 #
# https://example.com/disabled.ics
```

- 支持整行 `#` 注释，以及用“空格 + `#`”分隔的行尾备注。URL 本身的 `#fragment` 不会被截断。
- 注释和空行会保存到设备，刷新网页 / 重启设备后仍保留，但不参与下载，也不占用订阅数量。
- 输入框提示有效订阅数量、格式错误和未保存状态；“保存并同步”会先保存，再开始同步。
- 含注释最多 3000 字节。Outlook 请使用发布后的 **ICS 链接**，不是 HTML 网页链接。
- 小程序 / 蓝牙仍使用原来的纯地址列表；保存相同列表会保留网页备注，修改列表后会重新生成无备注文本。

## 微信小程序

`miniprogram/` 是一个原生微信小程序：蓝牙配网，在家里 WiFi 下查看屏幕画面、手动同步、预览某天、管理日历订阅。

1. 在 [微信公众平台](https://mp.weixin.qq.com/) 注册一个小程序（个人主体即可），拿到 AppID。
2. 后台 → 设置 → 服务内容声明 → **用户隐私保护指引**：勾选“蓝牙”（不声明的话调用蓝牙接口会失败）。
3. 用微信开发者工具导入 `miniprogram/` 目录，把 `project.config.json` 里的 `appid` 改成自己的。
4. 点“预览”用手机微信扫码就能用；给家人用可以上传为体验版，把他们加为体验成员。自己用不需要提交审核。

说明：

- 局域网功能靠微信的“局域网通信”能力（直接请求 `http://设备IP/`，不需要域名和 https）。iOS 第一次会询问是否允许微信访问“本地网络”，要选允许。
- iOS 微信用不了 mDNS，所以小程序通过蓝牙广播拿到设备 IP（广播里带了 IP），IP 变了也能自动找到。
- 安卓手机搜索蓝牙设备需要打开系统定位。
- 蓝牙改 WiFi / 日历只在配网模式下有效，避免邻居连上来乱改；局域网接口和网页一样，同一 WiFi 下的设备都能访问。

## 不用开发板，在电脑上预览排版

```bash
tools/preview.sh                                        # 今天
tools/preview.sh 2026-02-17                             # 指定日期
tools/preview.sh 2026-10-14 --ics tools/preview/sample.ics
```

生成 `.pio/preview/preview.png`。排版代码与设备完全相同，改布局时用它最快，不用每次都烧录。
第一次运行要编译字体文件，需要等几十秒。依赖库来自 `.pio/libdeps`，所以要先编译过一次固件。

## 单元测试

```bash
pio test -e native
```

网页订阅脚本还有独立回归测试（需要 Node.js，无 npm 依赖）：

```bash
node tools/test_web_subscriptions.mjs
```

订阅文本测试覆盖整行 / 行尾注释、空行、URL 片段、行号提示、数量和 UTF-8 字节限制。
网页脚本测试另覆盖保存 / 回显、失败保留输入，以及旧配置兼容。

覆盖农历转换（逐日校验 1900–2100 年连续性）、节气、节日、节假日解析、iCal 解析（折叠行、时区、全天 / 跨天、
每周 / 每月 / 每年重复、EXDATE、单次修改）。农历和节气数据已与 lunar_python 逐日比对，1901–2100 年完全一致。

## 屏幕到货后

1. 接线（可在 `config.h` 里改）：

   | 屏幕 | ESP32-S3 |
   |------|----------|
   | VCC  | 3V3 |
   | GND  | GND |
   | DIN  | GPIO 11 |
   | CLK  | GPIO 12 |
   | CS   | GPIO 10 |
   | DC   | GPIO 9 |
   | RST  | GPIO 8 |
   | BUSY | GPIO 7 |
   | PWR（新款微雪模块才有） | 3V3 |

2. `include/config.h` 里把 `EPD_ENABLED` 改成 `1`。
3. 驱动已按屏幕 IC（UC8176）设为 `GxEPD2_420c`，不用改。刷不出来时先查接线，串口出现 `Busy Timeout` 先检查 BUSY 线。
   以后换别的屏：UC8276 用 `GxEPD2_420c_Z21`，SSD1683 用 `GxEPD2_420c_GDEY042Z98`。
4. 非微雪的转接板如果刷新异常，把 `EPD_RESET_MS` 改成 `10` 试试。

三色屏全刷一次要 15–20 秒，期间会闪烁，属正常现象。程序只在画面真正变化时（跨天、日程变化）才刷屏。

## 已知限制

- iCal 里带 TZID 的时间都按本地时间处理（日历和设备在同一时区时没问题）。
- 不支持的重复规则（BYSETPOS、每月多个 BYDAY 等）只显示第一次，串口日志里会提示。
- HTTPS 不校验证书（`setInsecure`），省去维护根证书。
- 字体是文泉驿 GB2312 点阵字体，emoji 和生僻字不会显示。小月历的“休 / 班”角标取自 [Fusion Pixel Font](https://github.com/TakWolf/fusion-pixel-font)（OFL 1.1）。
- 目前是常驻供电模式，没有做深度睡眠。
- 局域网网页和接口没有密码，同一 WiFi 下的人都能查看画面、修改日历订阅。
- 设备只支持 2.4GHz WiFi；不支持需要网页登录的 WiFi（酒店、公司访客网络）和企业级认证。
