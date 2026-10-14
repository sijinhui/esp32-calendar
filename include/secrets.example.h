// 可选：开发时用的默认配置。复制一份为 include/secrets.h 再填写（secrets.h 已加入 .gitignore，不会被提交）
//
// 一般不需要这个文件：设备第一次开机会进入配网模式，用手机（热点网页或微信小程序）设置 WiFi 和日历即可。
// 手机设置的配置保存在设备里，优先于这里的值；重新烧录固件也不会丢。
#pragma once

#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// 在线日历的 iCal 订阅地址，可以填多个（每行一个，末尾的 nullptr 保留）
//   Google 日历：网页版 设置 → 左侧选中某个日历 → “iCal 格式的私密地址”
//   iCloud：日历 App 里右键日历 → 共享 → 勾选“公开日历”，复制链接并把 webcal:// 改成 https://
//   Outlook：设置 → 日历 → 共享日历 → 发布日历 → ICS 链接
static const char* const ICS_URLS[] = {
    // "https://calendar.google.com/calendar/ical/xxxx%40gmail.com/private-xxxx/basic.ics",
    nullptr,
};
