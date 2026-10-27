// 硬件与功能配置
#pragma once

// 下面按芯片（CONFIG_IDF_TARGET_ESP32C5 等）选引脚，要先拿到 sdkconfig；电脑上跑单元测试时没有这个文件
#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif

// ===================== 墨水屏 =====================
// 1 = 驱动墨水屏。网页预览（http://esp32-calendar.local/）不受影响，开不开都能看画面。
// 没接屏幕时 BUSY 脚悬空，刷屏可能要等超时（串口打印 Busy Timeout!，最多卡住约 1 分钟），
// 只用网页预览、不想等就改成 0。CI 编译的 Release 固件也用这个默认值，自动升级后照样驱动屏幕
#ifndef EPD_ENABLED
#define EPD_ENABLED 1
#endif

// 4.2 寸三色（黑白红）400x300，驱动 IC：UC8176 → GxEPD2_420c（GDEW042Z15 / 微雪 4.2" B V1 同款）
// 以后换屏可参考：UC8276 → GxEPD2_420c_Z21，SSD1683 → GxEPD2_420c_GDEY042Z98
#ifndef EPD_DRIVER
#define EPD_DRIVER GxEPD2_420c
#endif

// 微雪（Waveshare）的驱动板 / HAT 复位电路比较特殊，需要 2ms 的短复位脉冲；其他转接板用 10
#define EPD_RESET_MS 2

#if CONFIG_IDF_TARGET_ESP32C5
// Seeed XIAO ESP32-C5（https://wiki.seeedstudio.com/cn/xiao_esp32c5_getting_started/）：
//   8PIN 驱动板的丝印顺序 BUSY CS D/C RES SDI SCL，6 根信号线一排顺着插到左排 D1~D6；屏幕的 3.3V、GND 接右排 3V3、GND
//   （SPI 走 GPIO 矩阵，用哪个脚都行，不必守着 D8/D10）
//   D0 空着：它和 D1 是排针上仅有的低功耗引脚（LP GPIO），以后改深度睡眠时外接唤醒按键用（BOOT 键 GPIO28 叫不醒深度睡眠）
//   BUSY 是屏幕往外输出的信号，不能放 D6（开机时串口往外发日志，会和屏幕顶着）或 D2（GPIO25 启动引脚），所以只能正着插
#define EPD_PIN_BUSY 0  // D1
#define EPD_PIN_CS 25   // D2（GPIO25 是启动引脚，CS 是屏幕的输入，不会影响开机）
#define EPD_PIN_DC 7    // D3，驱动板上标 D/C（GPIO7 是启动引脚，但只影响 SDIO 下载模式，当输出没问题）
#define EPD_PIN_RST 23  // D4，驱动板上标 RES
#define EPD_PIN_MOSI 24 // D5，驱动板上标 SDI（有的屏标 DIN / SDA）
#define EPD_PIN_SCK 11  // D6，驱动板上标 SCL（有的屏标 CLK）；开机串口日志会让它抖几下，屏幕初始化前会先复位，没影响
#else
// 接线（ESP32-S3 任意 GPIO 都可以，避开 0/3/45/46 启动引脚、19/20 USB、26~37 Flash/PSRAM、43/44 串口日志、48 RGB 灯）
// 8PIN 通用驱动板的丝印顺序是 BUSY CS D/C RES SDI SCL；DevKitC 有 3V3 的那排排针第 4~9 位是 4 5 6 7 15 16，
// 6 根信号线用一排杜邦线倒着插：BUSY 接 16、CS 接 15 …… SCL 接 4；3.3V、GND 单独接（别插到 5V 上）
#define EPD_PIN_BUSY 16
#define EPD_PIN_CS 15
#define EPD_PIN_DC 7    // 驱动板上标 D/C
#define EPD_PIN_RST 6   // 驱动板上标 RES
#define EPD_PIN_MOSI 5  // 驱动板上标 SDI（有的屏标 DIN / SDA）
#define EPD_PIN_SCK 4   // 驱动板上标 SCL（有的屏标 CLK）
#endif

#define SCREEN_W 400
#define SCREEN_H 300

// ===================== 时间 =====================
#define TZ_POSIX "CST-8"          // 北京时间
#define TZ_OFFSET_SEC (8 * 3600)  // iCal 里的 UTC 时间换算成本地时间用（中国没有夏令时）
#define NTP_SERVER_1 "ntp.aliyun.com"
#define NTP_SERVER_2 "ntp.tencent.com"
#define NTP_SERVER_3 "pool.ntp.org"

// ===================== 显示 =====================
#define WEEK_STARTS_MONDAY 0  // 1 = 周一在最左列，0 = 周日在最左列
#define AGENDA_DAYS 7         // 右侧日程列表显示今天起几天内的日程

// ===================== 数据同步 =====================
#ifndef ICS_SYNC_MINUTES
#define ICS_SYNC_MINUTES 30  // 在线日历多久同步一次（省电模式下每次都要打开 WiFi）
#endif
// 夜间不自动同步：本地时间 [QUIET_START_HOUR, QUIET_END_HOUR) 点之间不联网（零点照样换日期重画），
// 到 QUIET_END_HOUR 点再同步。两个值相同 = 不限制。网页 / 小程序里手动同步不受影响
#ifndef QUIET_START_HOUR
#define QUIET_START_HOUR 0
#endif
#ifndef QUIET_END_HOUR
#define QUIET_END_HOUR 7
#endif
#define SYNC_FAIL_SHOW_AFTER 3  // 连续失败几次才在屏幕上显示“WiFi 未连接 / 同步失败”（偶尔失败一次不值得刷一次三色屏）
#define HTTP_CONNECT_TIMEOUT_MS 5000  // 每个请求：TCP 连接超时
#define HTTP_TLS_TIMEOUT_S 10         //           HTTPS 握手超时（库默认 120 秒）
#define HTTP_READ_TIMEOUT_MS 10000    //           等待数据超时（多久收不到数据算失败，不是整个下载的时间）
// 法定节假日 / 调休数据：https://github.com/NateScarlet/holiday-cn
#define HOLIDAY_URL_FMT "https://fastly.jsdelivr.net/gh/NateScarlet/holiday-cn@master/%d.json"

#define HOSTNAME "esp32-calendar"  // 局域网访问 http://esp32-calendar.local/

// ===================== 在线升级 =====================
// 每天第一次同步后检查 GitHub 上的最新 Release，有新版本就自动下载升级（见 src/Ota.h）
// Release 目前只发布 S3 固件；C5 先禁用 OTA，避免把另一芯片的固件刷进来。
// 将来 CI 按芯片分别发布固件、并让设备选择对应文件后，才能给 C5 打开。
#ifndef OTA_ENABLED
#if CONFIG_IDF_TARGET_ESP32C5
#define OTA_ENABLED 0
#else
#define OTA_ENABLED 1
#endif
#endif
#define OTA_REPO "sijinhui/esp32-calendar"  // 从哪个仓库的 Release 下载
#define OTA_ASSET "firmware.bin"            // Release 里的固件文件名（CI 自动上传）
// 国内 GitHub 镜像（加速下载）：地址前缀，后面直接拼 https://github.com/... 的完整地址。
// 按顺序各试一次，都失败就等明天再查（不再直连 GitHub）。每项后面都要有逗号；"" 表示直连 GitHub，
// 比如不用镜像：#define OTA_MIRRORS "",
#ifndef OTA_MIRRORS
#define OTA_MIRRORS "https://cdn.gh-proxy.org/", "https://proxy.xiaosi.cc/",
#endif
// 固件版本：CI 编译 Release 时设成 tag 名（如 v1.2.0）；自己编译的用 git describe（如 v1.2.0-3-gabc1234，见 scripts/version.py），
// 拿不到时是 dev。只有 Release 的版本号比它新才会升级
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// ===================== 状态灯 =====================
// 板载 WS2818 RGB 灯，平时熄灭，只在同步中 / 同步结果 / 配网中亮（见 src/StatusLed.h）
// 乐鑫 ESP32-S3-DevKitC-1 v1.0 接 GPIO48，v1.1 接 GPIO38；板子不同请改这里
// XIAO ESP32-C5 没有 RGB 灯，只有一颗黄色用户灯（GPIO27，低电平亮）：颜色没法区分，只保留亮 / 灭 / 闪烁
#if CONFIG_IDF_TARGET_ESP32C5
#define RGB_LED_PIN 27
#define STATUS_LED_MONO 1
#else
#define RGB_LED_PIN 48
#define STATUS_LED_MONO 0
#endif
#define RGB_LED_BRIGHTNESS 10  // 0~255
#define RGB_LED_DONE_MS 3000   // 同步成功（绿）/ 失败（红）后灯亮多久
#define RGB_LED_BLINK_MS 500   // 慢闪：亮 / 灭各多久（同步中、配网）

// ===================== 功耗 =====================
// 开发模式：1 = WiFi 一直连着、不睡眠（串口日志不断，网页随时能打开，跟以前一样）
//           0 = 省电模式：WiFi 平时关闭、CPU 浅睡眠，到点醒来同步；短按 BOOT 键打开 WiFi 和网页 WIFI_WINDOW_S 秒
// 默认 0（Release 固件也是省电模式）；调试时要串口日志不断、网页随时能打开，就改成 1。
// 注意：省电模式睡眠时 USB 串口会断开，要烧录先短按 BOOT 键把设备叫醒（或者按住 BOOT 再按 RST 进下载模式）
#ifndef DEBUG_MODE
#define DEBUG_MODE 0
#endif
#ifndef WIFI_WINDOW_S
#define WIFI_WINDOW_S 300  // 开机后 / 短按 BOOT 键后 / 配网成功后，WiFi 和网页保持打开多久
#endif
#define WIFI_SYNC_TIMEOUT_S 20  // 省电模式：同步时打开 WiFi，多久连不上就放弃，等下次同步

// 平时只需要维持 WiFi 和网页服务，80MHz 足够（WiFi 要求不低于 80MHz）；
// 同步数据时临时升到 240MHz，HTTPS 握手和 iCal 解析快一些，早点回到低频
#define CPU_MHZ_IDLE 80
#define CPU_MHZ_SYNC 240

// ===================== 配网 =====================
// 进入配网模式后，设备开热点 EPaper-XXXX，同时打开蓝牙接受微信小程序的配置；平时蓝牙关闭省电
// 开发板上的 BOOT 键：长按进入配网模式，短按打开 WiFi 和网页（省电模式）。S3 是 GPIO0，XIAO ESP32-C5 是 GPIO28
#if CONFIG_IDF_TARGET_ESP32C5
#define BUTTON_PIN 28
#else
#define BUTTON_PIN 0
#endif
#define BUTTON_HOLD_MS 3000
#define WIFI_PROVISION_AFTER_S 30   // 开发模式：已保存的 WiFi 连不上多久后自动打开配网热点（省电模式不自动开）
#define AP_PASSWORD ""              // 配网热点密码（8 位以上），空 = 不设密码
