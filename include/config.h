// 硬件与功能配置
#pragma once

// ===================== 墨水屏 =====================
// 屏幕还没到时保持 0：程序照常运行，画面在浏览器里预览（http://epaper-calendar.local/）
// 接好屏幕后改成 1
#ifndef EPD_ENABLED
#define EPD_ENABLED 0
#endif

// 4.2 寸三色（黑白红）400x300，驱动 IC：UC8176 → GxEPD2_420c（GDEW042Z15 / 微雪 4.2" B V1 同款）
// 以后换屏可参考：UC8276 → GxEPD2_420c_Z21，SSD1683 → GxEPD2_420c_GDEY042Z98
#ifndef EPD_DRIVER
#define EPD_DRIVER GxEPD2_420c
#endif

// 微雪（Waveshare）的驱动板 / HAT 复位电路比较特殊，需要 2ms 的短复位脉冲；其他转接板用 10
#define EPD_RESET_MS 2

// 接线（ESP32-S3 任意 GPIO 都可以，避开 0/3/45/46 启动引脚、19/20 USB、26~37 Flash/PSRAM）
#define EPD_PIN_SCK 12
#define EPD_PIN_MOSI 11  // 屏幕上标 DIN / SDA
#define EPD_PIN_CS 10
#define EPD_PIN_DC 9
#define EPD_PIN_RST 8
#define EPD_PIN_BUSY 7

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
#define SYNC_FAIL_SHOW_AFTER 3  // 连续失败几次才在屏幕上显示“WiFi 未连接 / 同步失败”（偶尔失败一次不值得刷一次三色屏）
#define HTTP_CONNECT_TIMEOUT_MS 5000  // 每个请求：TCP 连接超时
#define HTTP_TLS_TIMEOUT_S 10         //           HTTPS 握手超时（库默认 120 秒）
#define HTTP_READ_TIMEOUT_MS 10000    //           等待数据超时（多久收不到数据算失败，不是整个下载的时间）
// 法定节假日 / 调休数据：https://github.com/NateScarlet/holiday-cn
#define HOLIDAY_URL_FMT "https://fastly.jsdelivr.net/gh/NateScarlet/holiday-cn@master/%d.json"

#define HOSTNAME "epaper-calendar"  // 局域网访问 http://epaper-calendar.local/

// ===================== 在线升级 =====================
// 每天第一次同步后检查 GitHub 上的最新 Release，有新版本就自动下载升级（见 src/Ota.h）
#ifndef OTA_ENABLED
#define OTA_ENABLED 1
#endif
#define OTA_REPO "sijinhui/esp32-calendar"  // 从哪个仓库的 Release 下载
#define OTA_ASSET "firmware.bin"            // Release 里的固件文件名（CI 自动上传）
// 国内 GitHub 镜像（加速下载）：地址前缀，后面直接拼 https://github.com/... 的完整地址。
// 按顺序各试一次，都失败就等明天再查（不再直连 GitHub）。每项后面都要有逗号；"" 表示直连 GitHub，
// 比如不用镜像：#define OTA_MIRRORS "",
#ifndef OTA_MIRRORS
#define OTA_MIRRORS "https://cdn.gh-proxy.org/", "https://proxy.xiaosi.cc/",
#endif
// 固件版本：CI 编译 Release 时设成 tag 名（如 v1.2.0）；自己编译的是 dev，不会自动升级
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// ===================== 状态灯 =====================
// 板载 WS2818 RGB 灯，平时熄灭，只在同步中 / 同步结果 / 配网中亮（见 src/StatusLed.h）
// 乐鑫 ESP32-S3-DevKitC-1 v1.0 接 GPIO48，v1.1 接 GPIO38；板子不同请改这里
#define RGB_LED_PIN 48
#define RGB_LED_BRIGHTNESS 10  // 0~255
#define RGB_LED_DONE_MS 3000   // 同步成功（绿）/ 失败（红）后灯亮多久
#define RGB_LED_BLINK_MS 500   // 慢闪：亮 / 灭各多久（同步中、配网）

// ===================== 功耗 =====================
// 开发模式：1 = WiFi 一直连着、不睡眠（串口日志不断，网页随时能打开，跟以前一样）
//           0 = 省电模式：WiFi 平时关闭、CPU 浅睡眠，到点醒来同步；短按 BOOT 键打开 WiFi 和网页 WIFI_WINDOW_S 秒
// 屏幕还没到、要靠网页看画面时用 1；接好屏幕、调试完再改成 0。
// 注意：省电模式睡眠时 USB 串口会断开，要烧录先短按 BOOT 键把设备叫醒（或者按住 BOOT 再按 RST 进下载模式）
#ifndef DEBUG_MODE
#define DEBUG_MODE 1
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
#define BUTTON_PIN 0                // 开发板上的 BOOT 键：长按进入配网模式，短按打开 WiFi 和网页（省电模式）
#define BUTTON_HOLD_MS 3000
#define WIFI_PROVISION_AFTER_S 30   // 开发模式：已保存的 WiFi 连不上多久后自动打开配网热点（省电模式不自动开）
#define AP_PASSWORD ""              // 配网热点密码（8 位以上），空 = 不设密码
