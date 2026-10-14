#include "IcsParser.h"

#include <ChineseCalendar.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstdlib>

namespace ics {

namespace {

constexpr size_t kMaxLine = 2048;  // 更长的行（如内嵌附件）直接截断，我们只关心短属性
constexpr int kMaxIterations = 20000;

int weekdayOfDay(int32_t day) { return static_cast<int>(((day + 4) % 7 + 7) % 7); }

int parseWeekday(const std::string& s) {
  static const char* const kNames[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
  for (int i = 0; i < 7; i++) {
    if (s == kNames[i]) return i;
  }
  return -1;
}

// 当月第 n 个（n<0 为倒数第 -n 个）星期 wd 是几号，不存在返回 0
int nthWeekdayOfMonth(int y, int m, int wd, int n) {
  const int dim = cal::daysInMonth(y, m);
  int d;
  if (n > 0) {
    d = 1 + (wd - cal::weekday(y, m, 1) + 7) % 7 + (n - 1) * 7;
  } else {
    d = dim - (cal::weekday(y, m, dim) - wd + 7) % 7 + (n + 1) * 7;
  }
  return (d >= 1 && d <= dim) ? d : 0;
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t pos = 0;
  while (true) {
    const size_t next = s.find(sep, pos);
    out.push_back(s.substr(pos, next == std::string::npos ? std::string::npos : next - pos));
    if (next == std::string::npos) break;
    pos = next + 1;
  }
  return out;
}

}  // namespace

LocalTime makeLocal(int y, int m, int d, int hh, int mm, int ss) {
  return static_cast<LocalTime>(cal::daysFromCivil(y, m, d)) * kDay + hh * 3600 + mm * 60 + ss;
}

bool Event::coversDay(int32_t day) const {
  const LocalTime ds = static_cast<LocalTime>(day) * kDay;
  const LocalTime de = ds + kDay;
  if (end <= start) return start >= ds && start < de;
  return start < de && end > ds;
}

Parser::Parser(LocalTime windowStart, LocalTime windowEnd, int32_t utcOffsetSec)
    : windowStart_(windowStart), windowEnd_(windowEnd), utcOffset_(utcOffsetSec) {}

// ---------- 分行 ----------

void Parser::feed(const char* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const char c = data[i];
    if (c == '\n') {
      if (!raw_.empty() && raw_.back() == '\r') raw_.pop_back();
      onPhysicalLine(raw_);
      raw_.clear();
    } else if (raw_.size() < kMaxLine) {
      raw_ += c;
    }
  }
}

void Parser::onPhysicalLine(std::string& line) {
  if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {  // 折叠行：接到上一行后面
    if (logical_.size() < kMaxLine) logical_.append(line, 1, std::string::npos);
    return;
  }
  if (!logical_.empty()) onLogicalLine(logical_);
  logical_.swap(line);
}

void Parser::onLogicalLine(const std::string& line) {
  // NAME;PARAM=...;PARAM="a:b":VALUE —— 引号里的冒号不是分隔符
  size_t semi = std::string::npos, colon = std::string::npos;
  bool quoted = false;
  for (size_t i = 0; i < line.size(); i++) {
    const char c = line[i];
    if (c == '"') {
      quoted = !quoted;
    } else if (!quoted && c == ';' && semi == std::string::npos) {
      semi = i;
    } else if (!quoted && c == ':') {
      colon = i;
      break;
    }
  }
  if (colon == std::string::npos) return;
  std::string name = line.substr(0, semi != std::string::npos ? semi : colon);
  for (auto& ch : name) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
  const std::string params = semi != std::string::npos ? line.substr(semi + 1, colon - semi - 1) : "";
  onProperty(name, params, line.substr(colon + 1));
}

// ---------- 属性 ----------

void Parser::onProperty(const std::string& name, const std::string& params, const std::string& value) {
  (void)params;  // VALUE=DATE 可以从值的长度判断；TZID 统一按本地时间处理
  if (name == "BEGIN") {
    if (!inEvent_ && value == "VEVENT") {
      inEvent_ = true;
      nested_ = 0;
      cur_ = Pending();
      veventCount_++;
    } else if (inEvent_) {
      nested_++;
    }
    return;
  }
  if (name == "END") {
    if (!inEvent_) return;
    if (nested_ > 0) {
      nested_--;
    } else if (value == "VEVENT") {
      endEvent();
      inEvent_ = false;
    }
    return;
  }
  if (!inEvent_ || nested_ > 0) return;

  if (name == "UID") {
    cur_.uid = value;
  } else if (name == "SUMMARY") {
    cur_.summary = unescape(value);
  } else if (name == "DTSTART") {
    cur_.start = parseTime(value);
  } else if (name == "DTEND") {
    cur_.end = parseTime(value);
  } else if (name == "DURATION") {
    cur_.duration = parseDuration(value);
  } else if (name == "RRULE") {
    cur_.rrule = value;
  } else if (name == "RECURRENCE-ID") {
    cur_.recurrenceId = parseTime(value);
  } else if (name == "STATUS") {
    cur_.status = value;
  } else if (name == "EXDATE") {
    for (const auto& part : split(value, ',')) {
      const Time t = parseTime(part);
      if (t.valid) cur_.exdates.push_back(t);
    }
  }
}

Parser::Time Parser::parseTime(const std::string& v) const {
  Time t;
  int y = 0, mo = 0, d = 0, hh = 0, mi = 0, ss = 0;
  if (v.size() < 8 || std::sscanf(v.c_str(), "%4d%2d%2d", &y, &mo, &d) != 3) return t;
  if (mo < 1 || mo > 12 || d < 1 || d > 31) return t;
  if (v.size() >= 15 && v[8] == 'T') {
    if (std::sscanf(v.c_str() + 9, "%2d%2d%2d", &hh, &mi, &ss) != 3) return t;
    t.t = makeLocal(y, mo, d, hh, mi, ss);
    if (v.size() >= 16 && v[15] == 'Z') t.t += utcOffset_;
  } else {
    t.t = makeLocal(y, mo, d);
    t.dateOnly = true;
  }
  t.valid = true;
  return t;
}

int64_t Parser::parseDuration(const std::string& v) {
  int64_t total = 0, num = 0;
  bool negative = false, any = false;
  for (const char c : v) {
    if (c == '-') {
      negative = true;
    } else if (c == '+' || c == 'P' || c == 'T') {
      // 分隔符
    } else if (c >= '0' && c <= '9') {
      num = num * 10 + (c - '0');
    } else {
      switch (c) {
        case 'W': total += num * 7 * kDay; break;
        case 'D': total += num * kDay; break;
        case 'H': total += num * 3600; break;
        case 'M': total += num * 60; break;
        case 'S': total += num; break;
        default: return -1;
      }
      num = 0;
      any = true;
    }
  }
  if (!any) return -1;
  return negative ? -total : total;
}

std::string Parser::unescape(const std::string& v) {
  std::string out;
  out.reserve(v.size());
  for (size_t i = 0; i < v.size(); i++) {
    if (v[i] == '\\' && i + 1 < v.size()) {
      const char n = v[++i];
      out += (n == 'n' || n == 'N') ? ' ' : n;
    } else {
      out += v[i];
    }
  }
  return out;
}

Parser::Rule Parser::parseRule(const std::string& value) const {
  Rule r;
  std::vector<std::string> byday;
  for (const auto& part : split(value, ';')) {
    const size_t eq = part.find('=');
    if (eq == std::string::npos) continue;
    const std::string k = part.substr(0, eq);
    const std::string v = part.substr(eq + 1);
    if (k == "FREQ") {
      r.freq = v == "DAILY"     ? Rule::Daily
               : v == "WEEKLY"  ? Rule::Weekly
               : v == "MONTHLY" ? Rule::Monthly
               : v == "YEARLY"  ? Rule::Yearly
                                : Rule::None;
    } else if (k == "INTERVAL") {
      r.interval = std::max(1, atoi(v.c_str()));
    } else if (k == "COUNT") {
      r.count = std::max(0, atoi(v.c_str()));
    } else if (k == "UNTIL") {
      r.until = parseTime(v);
    } else if (k == "BYDAY") {
      byday = split(v, ',');
    } else if (k == "BYMONTHDAY" && v.find(',') == std::string::npos) {
      r.monthDay = atoi(v.c_str());
    } else if (k == "BYMONTH" && v.find(',') == std::string::npos) {
      r.month = atoi(v.c_str());
    } else if (k != "WKST") {
      r.unsupported = true;  // BYSETPOS、BYYEARDAY、多个 BYMONTHDAY 等
    }
  }
  if (r.freq == Rule::None) r.unsupported = true;

  for (const auto& item : byday) {
    if (item.size() < 2) {
      r.unsupported = true;
      continue;
    }
    const int wd = parseWeekday(item.substr(item.size() - 2));
    const int nth = item.size() > 2 ? atoi(item.substr(0, item.size() - 2).c_str()) : 0;
    if (wd < 0) {
      r.unsupported = true;
    } else if (r.freq == Rule::Daily || r.freq == Rule::Weekly) {
      if (nth != 0) r.unsupported = true;
      r.weekdays |= static_cast<uint8_t>(1 << wd);
    } else if (nth != 0 && byday.size() == 1) {  // MONTHLY / YEARLY：只支持单个“第 n 个星期几”
      r.nthWeekday = nth;
      r.nthWeekdayWd = wd;
    } else {
      r.unsupported = true;
    }
  }
  if (r.month != 0 && r.freq != Rule::Yearly) r.unsupported = true;
  return r;
}

// ---------- 事件 ----------

void Parser::endEvent() {
  Pending& e = cur_;
  if (!e.start.valid) return;

  int64_t duration;
  if (e.end.valid) {
    duration = e.end.t - e.start.t;
  } else if (e.duration >= 0) {
    duration = e.duration;
  } else {
    duration = e.start.dateOnly ? kDay : 0;
  }
  if (duration < 0) duration = 0;
  const bool cancelled = e.status == "CANCELLED";

  if (e.recurrenceId.valid) {  // 重复事件中被单独修改（或取消）的某一次
    overrides_[e.uid].push_back(e.recurrenceId);
    if (!cancelled) addIfInWindow(e.summary, e.start.t, e.start.t + duration, e.start.dateOnly);
    return;
  }
  if (cancelled) return;

  if (!e.rrule.empty()) {
    if (e.start.t >= windowEnd_) return;  // 第一次就在窗口之后
    Master m;
    m.uid = std::move(e.uid);
    m.summary = std::move(e.summary);
    m.start = e.start;
    m.duration = duration;
    m.rule = parseRule(e.rrule);
    m.exdates = std::move(e.exdates);
    if (m.rule.unsupported) unsupportedRules_++;
    masters_.push_back(std::move(m));
    return;
  }
  addIfInWindow(e.summary, e.start.t, e.start.t + duration, e.start.dateOnly);
}

void Parser::addIfInWindow(const std::string& summary, LocalTime start, LocalTime end, bool allDay) {
  const bool overlaps = end > start ? (start < windowEnd_ && end > windowStart_)
                                    : (start >= windowStart_ && start < windowEnd_);
  if (overlaps) events_.push_back({summary, start, end, allDay});
}

bool Parser::excluded(const Master& m, LocalTime start) const {
  auto same = [&](const Time& t) {
    if (t.dateOnly || m.start.dateOnly) return dayOf(t.t) == dayOf(start);
    return t.t == start;
  };
  for (const auto& t : m.exdates) {
    if (same(t)) return true;
  }
  auto it = overrides_.find(m.uid);
  if (it != overrides_.end()) {
    for (const auto& t : it->second) {
      if (same(t)) return true;
    }
  }
  return false;
}

void Parser::expand(const Master& m) {
  const Rule& r = m.rule;
  const int32_t startDay = dayOf(m.start.t);
  const LocalTime timeOfDay = m.start.t - static_cast<LocalTime>(startDay) * kDay;
  LocalTime until = LLONG_MAX;
  if (r.until.valid) until = r.until.dateOnly ? r.until.t + kDay - 1 : r.until.t;
  int emitted = 0;

  auto startOf = [&](int32_t day) { return static_cast<LocalTime>(day) * kDay + timeOfDay; };
  auto pastEnd = [&](int32_t day) { return startOf(day) > until || startOf(day) >= windowEnd_; };
  // 产生一次发生；返回 false 表示该停止了
  auto emit = [&](int32_t day) {
    const LocalTime s = startOf(day);
    if (s < m.start.t) return true;  // 早于 DTSTART 的不算
    if (s > until || s >= windowEnd_) return false;
    if (r.count && emitted >= r.count) return false;
    emitted++;
    if (!excluded(m, s)) addIfInWindow(m.summary, s, s + m.duration, m.start.dateOnly);
    return true;
  };

  if (r.unsupported) {
    emit(startDay);
    return;
  }

  // 没有 COUNT 时可以直接跳到窗口附近，不必从很多年前的 DTSTART 一次次数过来
  const int32_t spanDays = static_cast<int32_t>(m.duration / kDay) + 1;
  const int32_t skipDays = r.count ? 0 : std::max<int32_t>(0, dayOf(windowStart_) - startDay - spanDays);

  int sy, sm, sd;
  cal::civilFromDays(startDay, sy, sm, sd);

  switch (r.freq) {
    case Rule::Daily: {
      const int first = skipDays / r.interval;
      for (int i = first; i < first + kMaxIterations; i++) {
        const int32_t day = startDay + i * r.interval;
        if (pastEnd(day)) break;
        if (r.weekdays && !((r.weekdays >> weekdayOfDay(day)) & 1)) continue;
        if (!emit(day)) break;
      }
      break;
    }
    case Rule::Weekly: {
      const uint8_t mask = r.weekdays ? r.weekdays : static_cast<uint8_t>(1 << weekdayOfDay(startDay));
      const int32_t weekStart = startDay - (weekdayOfDay(startDay) + 6) % 7;  // WKST 默认周一
      const int first = skipDays / (7 * r.interval);
      bool stop = false;
      for (int w = first; w < first + kMaxIterations && !stop; w++) {
        const int32_t ws = weekStart + w * 7 * r.interval;
        if (pastEnd(ws)) break;
        for (int k = 0; k < 7 && !stop; k++) {  // 周一 … 周日
          if ((mask >> ((k + 1) % 7)) & 1) stop = !emit(ws + k);
        }
      }
      break;
    }
    case Rule::Monthly: {
      for (int i = 0; i < kMaxIterations; i++) {
        const int mi = (sm - 1) + i * r.interval;
        const int y = sy + mi / 12, mo = mi % 12 + 1;
        if (pastEnd(cal::daysFromCivil(y, mo, 1))) break;
        const int dim = cal::daysInMonth(y, mo);
        int d = sd;
        if (r.nthWeekday) {
          d = nthWeekdayOfMonth(y, mo, r.nthWeekdayWd, r.nthWeekday);
        } else if (r.monthDay > 0) {
          d = r.monthDay;
        } else if (r.monthDay < 0) {
          d = dim + r.monthDay + 1;
        }
        if (d < 1 || d > dim) continue;  // 例如 31 号在小月不存在：跳过
        if (!emit(cal::daysFromCivil(y, mo, d))) break;
      }
      break;
    }
    case Rule::Yearly: {
      const int mo = r.month ? r.month : sm;
      for (int i = 0; i < kMaxIterations; i++) {
        const int y = sy + i * r.interval;
        if (pastEnd(cal::daysFromCivil(y, 1, 1))) break;
        const int dim = cal::daysInMonth(y, mo);
        int d = r.monthDay ? (r.monthDay > 0 ? r.monthDay : dim + r.monthDay + 1) : sd;
        if (r.nthWeekday) d = nthWeekdayOfMonth(y, mo, r.nthWeekdayWd, r.nthWeekday);
        if (d < 1 || d > dim) continue;  // 例如 2 月 29 日在平年不存在
        if (!emit(cal::daysFromCivil(y, mo, d))) break;
      }
      break;
    }
    case Rule::None:
      break;
  }
}

void Parser::finish() {
  if (!raw_.empty()) {
    onPhysicalLine(raw_);
    raw_.clear();
  }
  if (!logical_.empty()) {
    onLogicalLine(logical_);
    logical_.clear();
  }
  for (const auto& m : masters_) expand(m);
  masters_.clear();
  overrides_.clear();

  sortEvents(events_);
}

void sortEvents(std::vector<Event>& events) {
  std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
    const int32_t da = dayOf(a.start), db = dayOf(b.start);
    if (da != db) return da < db;
    if (a.allDay != b.allDay) return a.allDay;
    return a.start < b.start;
  });
}

}  // namespace ics
