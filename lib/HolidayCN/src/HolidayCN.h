// 中国法定节假日与调休上班日。
// 每年的安排由国务院办公厅发布，无法推算，所以联网获取：
//   https://github.com/NateScarlet/holiday-cn  （每年一个 {year}.json，自动从 gov.cn 抓取）
// 纯 C++ + ArduinoJson，可在电脑上做单元测试。
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

class HolidayCN {
 public:
  enum class Status : uint8_t { Normal, OffDay, WorkDay };  // 普通日 / 放假（休）/ 调休上班（班）

  struct Info {
    Status status = Status::Normal;
    std::string name;  // 例如 "国庆节"
  };

  // 解析一份 holiday-cn 的 {year}.json，替换该年已有数据。返回条目数，解析失败返回 -1。
  int loadJson(const char* json, size_t len);

  Info lookup(int y, int m, int d) const;
  bool hasYear(int year) const;
  void clear();

 private:
  struct Entry {
    int32_t day;  // cal::daysFromCivil
    bool off;
    std::string name;
    int source;  // 来自哪一年的文件（文件里可能含有跨年的调休日）
  };
  std::vector<Entry> entries_;  // 按 day 升序
  std::vector<int> years_;
};
