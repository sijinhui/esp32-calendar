#include "HolidayCN.h"

#include <ArduinoJson.h>
#include <ChineseCalendar.h>

#include <algorithm>
#include <cstdio>

int HolidayCN::loadJson(const char* json, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) return -1;
  const int year = doc["year"] | 0;
  JsonArrayConst days = doc["days"].as<JsonArrayConst>();
  if (year <= 0 || days.isNull()) return -1;

  std::vector<Entry> parsed;
  for (JsonObjectConst item : days) {
    const char* date = item["date"] | "";
    int y = 0, m = 0, d = 0;
    if (std::sscanf(date, "%d-%d-%d", &y, &m, &d) != 3 || item["isOffDay"].isNull()) return -1;
    parsed.push_back({cal::daysFromCivil(y, m, d), item["isOffDay"].as<bool>(), item["name"] | "", year});
  }

  // 替换掉同一年份文件之前加载的数据
  if (hasYear(year)) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const Entry& e) { return e.source == year; }),
                   entries_.end());
  } else {
    years_.push_back(year);
  }
  for (auto& e : parsed) {
    auto it = std::lower_bound(entries_.begin(), entries_.end(), e.day,
                               [](const Entry& a, int32_t day) { return a.day < day; });
    if (it != entries_.end() && it->day == e.day) {
      *it = std::move(e);
    } else {
      entries_.insert(it, std::move(e));
    }
  }
  return static_cast<int>(parsed.size());
}

HolidayCN::Info HolidayCN::lookup(int y, int m, int d) const {
  const int32_t day = cal::daysFromCivil(y, m, d);
  auto it = std::lower_bound(entries_.begin(), entries_.end(), day,
                             [](const Entry& a, int32_t v) { return a.day < v; });
  if (it == entries_.end() || it->day != day) return {};
  return {it->off ? Status::OffDay : Status::WorkDay, it->name};
}

bool HolidayCN::hasYear(int year) const {
  return std::find(years_.begin(), years_.end(), year) != years_.end();
}

void HolidayCN::clear() {
  entries_.clear();
  years_.clear();
}
