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
#define ICS_SYNC_MINUTES 30  // 在线日历多久同步一次
// 法定节假日 / 调休数据：https://github.com/NateScarlet/holiday-cn
#define HOLIDAY_URL_FMT "https://fastly.jsdelivr.net/gh/NateScarlet/holiday-cn@master/%d.json"

#define HOSTNAME "epaper-calendar"  // 局域网访问 http://epaper-calendar.local/

// ===================== 配网 =====================
// 进入配网模式后，设备开热点 EPaper-XXXX，同时蓝牙接受微信小程序的配置
#define BUTTON_PIN 0                // 开发板上的 BOOT 键：长按进入配网模式
#define BUTTON_HOLD_MS 3000
#define WIFI_PROVISION_AFTER_S 30   // 已保存的 WiFi 连不上多久后自动打开配网热点
#define AP_PASSWORD ""              // 配网热点密码（8 位以上），空 = 不设密码
