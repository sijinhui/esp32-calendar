#include "CalendarRenderer.h"

#include <ChineseCalendar.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <qrcodegen.h>

#include <algorithm>
#include <cstdio>

#include "Canvas3C.h"
#include "config.h"

namespace {

// 字体（U8g2 自带，文泉驿点阵中文字体包含全部 GB2312 汉字）
const uint8_t* const kFontCn12 = u8g2_font_wqy12_t_gb2312;
const uint8_t* const kFontCn16 = u8g2_font_wqy16_t_gb2312;
const uint8_t* const kFontBig = u8g2_font_logisoso38_tn;  // 左上角大号日期
const uint8_t* const kFontNum = u8g2_font_helvB10_tn;     // 小月历里的日期数字
constexpr int kNumHeight = 11;                            // helvB10 数字：基线以上 11px
constexpr int kCnHeight = 11;                             // wqy12 汉字：基线以上 10px + 基线 1px

// 布局：左栏（今天 + 小月历），右栏整列都是日程
constexpr int kLeftW = 190;  // 左栏宽度，右边是分隔线
constexpr int kGridX = 4;    // 小月历左边界
constexpr int kColW = 26;    // 7 列 × 26 = 182，刚好放下两个 12px 汉字的农历
constexpr int kGridLineY = 51;
constexpr int kHeadBaseline = 64;
constexpr int kGridY = 67;
constexpr int kGridBottom = SCREEN_H - 1;
constexpr int kMaxRowH = 44;
constexpr int kAgendaX = kLeftW + 8;
constexpr int kAgendaR = SCREEN_W - 6;
// 右栏日程的字体：日期行、时间、提示用 14px，日程标题再大一号用 16px
const uint8_t* const kFontAgenda = u8g2_font_wqy14_t_gb2312;
const uint8_t* const kFontEvent = u8g2_font_wqy16_t_gb2312;
constexpr int kTimeW = 42;     // 左边时间列的宽度，日程标题从这里开始
constexpr int kEventH = 20;    // 每条日程第一行的行高
constexpr int kEventBase = 4;  // 基线到行底的距离
// 标题太长时折行显示：续行的行距（比两条日程之间紧一点，看得出是同一条），以及最多几行（再多就末尾加“…”）
constexpr int kEventWrapH = 17;
constexpr int kEventMaxLines = 4;
constexpr int kNoteH = 19;  // “没有日程”等提示的行高

const char* const kWeekNames[] = {"日", "一", "二", "三", "四", "五", "六"};

// 小月历右上角的“休 / 班”角标，9×9 点阵（每行 2 字节，高位在左，与 Adafruit_GFX::drawBitmap 格式相同）。
// 比 12px 字体小一号，才放得进格子的右上角。取自 Fusion Pixel Font 10px
// （Copyright (c) 2022, TakWolf，SIL Open Font License 1.1，https://github.com/TakWolf/fusion-pixel-font）
constexpr int kBadgeW = 9, kBadgeH = 9;
const uint8_t kBadgeOff[] = {0x24, 0x00, 0x24, 0x00, 0x5F, 0x80, 0xC4, 0x00, 0x4E,
                             0x00, 0x55, 0x00, 0x64, 0x80, 0x44, 0x00, 0x44, 0x00};  // 休
const uint8_t kBadgeWork[] = {0xE7, 0x80, 0x55, 0x00, 0x55, 0x00, 0x55, 0x00, 0xF7,
                              0x80, 0x55, 0x00, 0x45, 0x00, 0x49, 0x00, 0xEB, 0x80};  // 班

U8G2_FOR_ADAFRUIT_GFX u8g2;

// 注意：U8g2 的 setFont() 会把字体模式重置为“不透明”（用背景色填满字形框），所以每次都要重新设成透明
void setFont(const uint8_t* font) {
  u8g2.setFont(font);
  u8g2.setFontMode(1);
}

void text(int x, int baseline, const char* s, uint16_t color) {
  u8g2.setForegroundColor(color);
  u8g2.drawUTF8(x, baseline, s);
}

void textCentered(int cx, int baseline, const char* s, uint16_t color) {
  text(cx - u8g2.getUTF8Width(s) / 2, baseline, s, color);
}

void textRight(int right, int baseline, const char* s, uint16_t color) {
  text(right - u8g2.getUTF8Width(s), baseline, s, color);
}

// 按当前字体截断 UTF-8 字符串，使宽度不超过 maxW；ellipsis = 截断时末尾加“…”
std::string fit(std::string s, int maxW, bool ellipsis = false) {
  if (u8g2.getUTF8Width(s.c_str()) <= maxW) return s;
  const char* tail = ellipsis ? "…" : "";
  while (!s.empty() && u8g2.getUTF8Width((s + tail).c_str()) > maxW) {
    size_t i = s.size() - 1;
    while (i > 0 && (static_cast<uint8_t>(s[i]) & 0xC0) == 0x80) i--;  // 退到字符首字节
    s.resize(i);
  }
  while (!s.empty() && s.back() == ' ') s.pop_back();
  return s + tail;
}

// 按当前字体把 UTF-8 字符串折成若干行，每行宽度不超过 maxW；英文单词不从中间断开。
// 超过 maxLines 行时，最后一行截断并加“…”。至少返回一行
std::vector<std::string> wrap(const std::string& s, int maxW, int maxLines) {
  auto wordByte = [](char c) { return static_cast<uint8_t>(c) < 0x80 && c != ' '; };
  std::vector<std::string> lines;
  std::string line;
  size_t i = 0;
  while (i < s.size()) {
    size_t n = 1;
    while (i + n < s.size() && (static_cast<uint8_t>(s[i + n]) & 0xC0) == 0x80) n++;
    const std::string ch = s.substr(i, n);
    if (!line.empty() && u8g2.getUTF8Width((line + ch).c_str()) > maxW) {
      // 断在英文单词中间时，把这个单词整个挪到下一行（整行都是一个单词就只能硬断）
      std::string rest;
      if (wordByte(ch[0]) && wordByte(line.back())) {
        size_t k = line.size();
        while (k > 0 && wordByte(line[k - 1])) k--;
        if (k > 0) {
          rest = line.substr(k);
          line.resize(k);
        }
      }
      while (!line.empty() && line.back() == ' ') line.pop_back();
      if (static_cast<int>(lines.size()) == maxLines - 1) {
        lines.push_back(fit(line + rest + s.substr(i), maxW, true));
        return lines;
      }
      lines.push_back(line);
      line = rest;
      if (line.empty() && ch == " ") {  // 行首不留空格
        i += n;
        continue;
      }
    }
    line += ch;
    i += n;
  }
  if (!line.empty() || lines.empty()) lines.push_back(line);
  return lines;
}

int columnOfWeekday(int wd) { return WEEK_STARTS_MONDAY ? (wd + 6) % 7 : wd; }
int weekdayOfColumn(int col) { return WEEK_STARTS_MONDAY ? (col + 1) % 7 : col; }

bool hasEventOn(const RenderInput& in, int32_t day) {
  if (!in.events) return false;
  for (const auto& e : *in.events) {
    if (e.coversDay(day)) return true;
  }
  return false;
}

// 今天的节假日 / 节日 / 节气，或者距离下一个节气还有几天
std::string todayInfo(const RenderInput& in, uint16_t& color) {
  color = C_RED;
  if (in.holidays) {
    const HolidayCN::Info h = in.holidays->lookup(in.year, in.month, in.day);
    if (h.status == HolidayCN::Status::OffDay) return h.name + "假期";
    if (h.status == HolidayCN::Status::WorkDay) {
      color = C_BLACK;
      return h.name + "调休上班";
    }
  }
  const cal::DayLabel label = cal::dayLabel(in.year, in.month, in.day);
  if (label.kind == cal::LabelKind::Festival || label.kind == cal::LabelKind::SolarTerm) {
    return "今日" + label.text;
  }

  color = C_BLACK;
  const int32_t today = cal::daysFromCivil(in.year, in.month, in.day);
  for (int y = in.year; y <= in.year + 1; y++) {
    for (int i = 0; i < 24; i++) {
      const int d = cal::solarTermDay(y, i);
      if (d == 0) continue;
      const int32_t termDay = cal::daysFromCivil(y, i / 2 + 1, d);
      if (termDay > today) {
        char buf[48];
        snprintf(buf, sizeof buf, "距%s还有%d天", cal::solarTermName(i), static_cast<int>(termDay - today));
        return buf;
      }
    }
  }
  return "";
}

// 左上角：大号日期，右边三行是年月星期、农历、今日信息
void drawHeader(const RenderInput& in) {
  char buf[48];

  setFont(kFontBig);
  snprintf(buf, sizeof buf, "%d", in.day);
  text(6, 44, buf, C_RED);
  const int left = 6 + u8g2.getUTF8Width(buf) + 6;
  const int cx = (left + kLeftW) / 2;  // 大号日期右边剩下的区域居中
  const int maxW = kLeftW - 4 - left;

  setFont(kFontCn12);
  snprintf(buf, sizeof buf, "%d年%d月 星期%s", in.year, in.month,
           kWeekNames[cal::weekday(in.year, in.month, in.day)]);
  textCentered(cx, 13, fit(buf, maxW).c_str(), C_BLACK);

  // 农历日期（大字）+ 干支年（小字）放一行，放不下就只显示农历日期
  cal::LunarDate ld;
  if (cal::toLunar(in.year, in.month, in.day, ld)) {
    const std::string lunar = cal::lunarMonthName(ld) + cal::lunarDayName(ld.day);
    const std::string year = cal::ganzhiYear(ld.year) + cal::zodiac(ld.year) + "年";
    setFont(kFontCn16);
    const int lunarW = u8g2.getUTF8Width(lunar.c_str());
    setFont(kFontCn12);
    const int yearW = u8g2.getUTF8Width(year.c_str()) + 6;
    const bool withYear = lunarW + yearW <= maxW;
    const int x = cx - (lunarW + (withYear ? yearW : 0)) / 2;
    if (withYear) text(x + lunarW + 6, 30, year.c_str(), C_BLACK);
    setFont(kFontCn16);
    text(x, 30, lunar.c_str(), C_RED);
  }

  setFont(kFontCn12);
  uint16_t color;
  const std::string info = todayInfo(in, color);
  textCentered(cx, 46, fit(info, maxW).c_str(), color);
}

// 格子里的农历 / 节日 / 节气：最多两个汉字宽，“国庆节”这类放不下就去掉“节”字
std::string cellLabel(std::string s) {
  const int maxW = kColW - 2;
  const std::string jie = "节";
  if (u8g2.getUTF8Width(s.c_str()) > maxW && s.size() > jie.size() &&
      s.compare(s.size() - jie.size(), jie.size(), jie) == 0) {
    s.resize(s.size() - jie.size());
  }
  return fit(s, maxW);
}

void drawDayCell(Adafruit_GFX& gfx, const RenderInput& in, int d, int x0, int y0, int rowH) {
  const bool isToday = d == in.day;
  const int wd = cal::weekday(in.year, in.month, d);
  const HolidayCN::Info hol = in.holidays ? in.holidays->lookup(in.year, in.month, d) : HolidayCN::Info{};
  const cal::DayLabel label = cal::dayLabel(in.year, in.month, d);

  if (isToday) gfx.fillRoundRect(x0, y0 + 1, kColW, rowH - 2, 4, C_RED);

  const bool weekend = wd == 0 || wd == 6;
  const bool redNumber = hol.status == HolidayCN::Status::OffDay ||
                         (weekend && hol.status != HolidayCN::Status::WorkDay);
  const uint16_t numColor = isToday ? C_WHITE : redNumber ? C_RED : C_BLACK;
  const uint16_t labelColor = isToday ? C_WHITE : label.kind == cal::LabelKind::Festival ? C_RED : C_BLACK;

  // 从上到下：日程标记（左上）和“休/班”角标（右上）、日期数字、农历，整体在格子里垂直居中
  constexpr int kNumBase = kBadgeH + 1 + kNumHeight;
  constexpr int kLabelBase = kNumBase + 3 + kCnHeight;
  constexpr int kBlockH = kLabelBase + 1;
  const int top = y0 + std::max(0, (rowH - kBlockH) / 2);

  char buf[4];
  snprintf(buf, sizeof buf, "%d", d);
  setFont(kFontNum);
  textCentered(x0 + kColW / 2, top + kNumBase, buf, numColor);

  setFont(kFontCn12);
  textCentered(x0 + kColW / 2, top + kLabelBase, cellLabel(label.text).c_str(), labelColor);

  if (hol.status != HolidayCN::Status::Normal) {
    const bool off = hol.status == HolidayCN::Status::OffDay;
    gfx.drawBitmap(x0 + kColW - kBadgeW - 1, top, off ? kBadgeOff : kBadgeWork, kBadgeW, kBadgeH,
                   isToday ? C_WHITE : off ? C_RED : C_BLACK);
  }
  if (hasEventOn(in, cal::daysFromCivil(in.year, in.month, d))) {
    gfx.fillCircle(x0 + 5, top + 4, 2, isToday ? C_WHITE : C_BLACK);
  }
}

void drawMonthGrid(Adafruit_GFX& gfx, const RenderInput& in) {
  gfx.drawFastHLine(8, kGridLineY, kLeftW - 16, C_BLACK);
  setFont(kFontCn12);
  for (int c = 0; c < 7; c++) {
    const int wd = weekdayOfColumn(c);
    textCentered(kGridX + c * kColW + kColW / 2, kHeadBaseline, kWeekNames[wd],
                 (wd == 0 || wd == 6) ? C_RED : C_BLACK);
  }

  const int lead = columnOfWeekday(cal::weekday(in.year, in.month, 1));
  const int dim = cal::daysInMonth(in.year, in.month);
  const int rows = (lead + dim + 6) / 7;
  const int rowH = std::min(kMaxRowH, (kGridBottom - kGridY) / rows);
  for (int d = 1; d <= dim; d++) {
    const int idx = lead + d - 1;
    drawDayCell(gfx, in, d, kGridX + (idx % 7) * kColW, kGridY + (idx / 7) * rowH, rowH);
  }
}

std::string hhmm(ics::LocalTime t) {
  const int sec = static_cast<int>(t - static_cast<ics::LocalTime>(ics::dayOf(t)) * ics::kDay);
  char buf[8];
  snprintf(buf, sizeof buf, "%02d:%02d", sec / 3600, sec / 60 % 60);
  return buf;
}

std::string dayTitle(int32_t day, int offset) {
  int y, m, d;
  cal::civilFromDays(day, y, m, d);
  if (offset == 0) return "今天";
  const char* wd = kWeekNames[cal::weekday(y, m, d)];
  char buf[32];
  if (offset == 1) snprintf(buf, sizeof buf, "明天 周%s", wd);
  else if (offset == 2) snprintf(buf, sizeof buf, "后天 周%s", wd);
  else snprintf(buf, sizeof buf, "%d月%d日 周%s", m, d, wd);
  return buf;
}

// 日程里日期标题右侧的标注：节日、节气、放假、调休上班
struct DayTag {
  std::string text;
  uint16_t color = C_RED;
  bool standalone = false;  // 当天没有日程也单独列一行
};

DayTag dayTag(const RenderInput& in, int32_t day) {
  int y, m, d;
  cal::civilFromDays(day, y, m, d);
  const HolidayCN::Info h = in.holidays ? in.holidays->lookup(y, m, d) : HolidayCN::Info{};
  if (h.status == HolidayCN::Status::WorkDay) return {"调休上班", C_BLACK, true};

  const cal::DayLabel label = cal::dayLabel(y, m, d);
  if (label.kind == cal::LabelKind::Festival) return {label.text, C_RED, true};
  if (label.kind == cal::LabelKind::SolarTerm) return {label.text, C_BLACK, true};

  if (h.status == HolidayCN::Status::OffDay) {
    // 假期只在第一天单独列出，其余几天有日程时顺带标注
    int py, pm, pd;
    cal::civilFromDays(day - 1, py, pm, pd);
    const bool first = in.holidays->lookup(py, pm, pd).status != HolidayCN::Status::OffDay;
    return {h.name + "假期", C_RED, first};
  }
  return {};
}

void drawAgenda(Adafruit_GFX& gfx, const RenderInput& in) {
  struct Line {
    enum Kind { Title, Event, Note } kind;
    std::string left;   // Title：日期；Event：时间
    std::string right;  // Title：节日等标注；Event：日程标题
    uint16_t color = C_BLACK;  // Title / Note 文字之外那部分的颜色
    std::vector<std::string> body;  // Event：折好行的日程标题；Note：折好行的文字
  };
  auto height = [](const Line& l, bool first) {
    if (l.kind == Line::Title) return first ? 20 : 23;
    const int rest = static_cast<int>(l.body.size() - 1) * kEventWrapH;
    return (l.kind == Line::Event ? kEventH : kNoteH) + rest;
  };

  constexpr int kBodyW = kAgendaR - kAgendaX - kTimeW;
  setFont(kFontEvent);  // 折行按日程标题的字体量宽度
  std::vector<Line> lines;
  const int32_t today = cal::daysFromCivil(in.year, in.month, in.day);
  for (int k = 0; k < AGENDA_DAYS; k++) {
    const int32_t day = today + k;
    std::vector<Line> items;
    for (size_t i = 0; in.events && i < in.events->size(); i++) {
      const ics::Event& e = (*in.events)[i];
      if (!e.coversDay(day)) continue;
      if (k > 0 && e.coversDay(day - 1)) continue;  // 跨天事件只列一次
      const bool showTime = !e.allDay && ics::dayOf(e.start) == day;
      items.push_back({Line::Event, e.allDay ? "全天" : showTime ? hhmm(e.start) : "", e.summary, C_BLACK,
                       wrap(e.summary, kBodyW, kEventMaxLines)});
    }
    // 今天的节日已经显示在左上角，“今天”这一行右边放状态栏
    const DayTag tag = k == 0 ? DayTag{in.status, in.icsFailed ? C_RED : C_BLACK, true} : dayTag(in, day);
    if (k > 0 && items.empty() && !tag.standalone) continue;

    lines.push_back({Line::Title, dayTitle(day, k), tag.text, tag.color});
    lines.insert(lines.end(), items.begin(), items.end());
    if (k == 0 && items.empty()) {
      const std::string note = !in.icsConfigured ? "未配置在线日历，可在设置页面添加"
                               : in.icsFailed    ? "日程同步失败"
                                                 : "没有日程";
      setFont(kFontAgenda);
      lines.push_back({Line::Note, "", "", in.icsConfigured && in.icsFailed ? C_RED : C_BLACK,
                       wrap(note, kAgendaR - kAgendaX, 2)});
      setFont(kFontEvent);
    }
  }

  // 放不下时最后一行留给“还有 N 项”，且不要以一个孤零零的日期标题结尾
  constexpr int kTop = 4, kBottom = SCREEN_H - 4, kMoreH = 19;
  size_t shown = 0;
  int total = 0;
  while (shown < lines.size() && total + height(lines[shown], shown == 0) <= kBottom - kTop) {
    total += height(lines[shown], shown == 0);
    shown++;
  }
  if (shown < lines.size() && lines[shown].kind == Line::Event) {
    // 折了好几行的日程放不全时，先显示放得下的前几行（末尾加“…”），免得底下空一大块
    const int room = kBottom - kTop - total - (shown + 1 < lines.size() ? kMoreH : 0);
    const int rows = room >= kEventH ? 1 + (room - kEventH) / kEventWrapH : 0;
    Line& l = lines[shown];
    if (rows > 0 && rows < static_cast<int>(l.body.size())) {
      l.body = wrap(l.right, kBodyW, rows);
      total += height(l, false);
      shown++;
    }
  }
  if (shown < lines.size()) {
    while (shown > 0 && total + kMoreH > kBottom - kTop) {
      shown--;
      total -= height(lines[shown], shown == 0);
    }
    while (shown > 0 && lines[shown - 1].kind == Line::Title && lines[shown].kind != Line::Title) shown--;
  }
  int hidden = 0;
  for (size_t i = shown; i < lines.size(); i++) hidden += lines[i].kind == Line::Event;

  int y = kTop;
  for (size_t i = 0; i < shown; i++) {
    const Line& l = lines[i];
    y += height(l, i == 0);
    switch (l.kind) {
      case Line::Title:
        setFont(kFontAgenda);
        text(kAgendaX, y - 6, l.left.c_str(), C_RED);
        textRight(kAgendaR, y - 6, fit(l.right, kAgendaR - kAgendaX - u8g2.getUTF8Width(l.left.c_str()) - 12).c_str(),
                  l.color);
        gfx.drawFastHLine(kAgendaX, y - 2, kAgendaR - kAgendaX, C_BLACK);
        break;
      case Line::Event: {
        const int base = y - height(l, false) + kEventH - kEventBase;  // 第一行的基线
        setFont(kFontAgenda);
        text(kAgendaX, base, l.left.c_str(), C_BLACK);
        setFont(kFontEvent);
        for (size_t r = 0; r < l.body.size(); r++) {
          text(kAgendaX + kTimeW, base + static_cast<int>(r) * kEventWrapH, l.body[r].c_str(), C_BLACK);
        }
        break;
      }
      case Line::Note: {
        const int base = y - height(l, false) + kNoteH - 4;
        setFont(kFontAgenda);
        for (size_t r = 0; r < l.body.size(); r++) {
          text(kAgendaX, base + static_cast<int>(r) * kEventWrapH, l.body[r].c_str(), l.color);
        }
        break;
      }
    }
  }
  if (hidden > 0) {
    char buf[32];
    snprintf(buf, sizeof buf, "…还有 %d 项", hidden);
    setFont(kFontAgenda);
    text(kAgendaX + kTimeW, y + kMoreH - 4, buf, C_BLACK);
  }
}

// WiFi 二维码里的特殊字符要转义
std::string qrEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') out += '\\';
    out += c;
  }
  return out;
}

// 在 (x, y) 左上角画二维码，模块边长取不超过 maxSize 的最大整数倍；返回实际边长
int drawQr(Adafruit_GFX& gfx, const std::string& data, int x, int y, int maxSize) {
  uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
  uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
  if (!qrcodegen_encodeText(data.c_str(), tmp, qr, qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true)) {
    return 0;
  }
  const int n = qrcodegen_getSize(qr);
  const int scale = std::max(1, maxSize / n);
  for (int r = 0; r < n; r++) {
    for (int c = 0; c < n; c++) {
      if (qrcodegen_getModule(qr, c, r)) gfx.fillRect(x + c * scale, y + r * scale, scale, scale, C_BLACK);
    }
  }
  return n * scale;
}

}  // namespace

void renderSetup(Adafruit_GFX& gfx, const SetupInput& in) {
  u8g2.begin(gfx);
  u8g2.setFontDirection(0);
  gfx.fillScreen(C_WHITE);

  // 左侧：扫码连接热点（iOS / Android 相机都能识别 WIFI: 格式）
  const std::string qr = in.apPassword.empty()
                             ? "WIFI:T:nopass;S:" + qrEscape(in.apName) + ";;"
                             : "WIFI:T:WPA;S:" + qrEscape(in.apName) + ";P:" + qrEscape(in.apPassword) + ";;";
  constexpr int kQrX = 20, kQrY = 56, kQrMax = 150;
  const int size = drawQr(gfx, qr, kQrX, kQrY, kQrMax);
  setFont(kFontCn12);
  textCentered(kQrX + size / 2, kQrY + size + 20, "手机相机扫码连接热点", C_BLACK);

  // 右侧：步骤说明
  constexpr int kX = 184, kPitch = 18;
  int y = 40;
  setFont(kFontCn16);
  text(kX, y, "设置 WiFi", C_RED);
  y += 28;
  setFont(kFontCn12);
  text(kX, y, "1. 手机连接热点", C_BLACK);
  y += kPitch;
  setFont(kFontCn16);
  text(kX + 14, y + 2, in.apName.c_str(), C_RED);
  y += kPitch + 4;
  setFont(kFontCn12);
  if (!in.apPassword.empty()) {
    text(kX + 14, y, ("密码 " + in.apPassword).c_str(), C_BLACK);
    y += kPitch;
  }
  text(kX, y, "2. 会自动弹出设置页面，", C_BLACK);
  y += kPitch;
  text(kX + 14, y, ("没弹出就打开 " + in.apIp).c_str(), C_BLACK);
  y += kPitch;
  text(kX, y, "3. 选择家里的 WiFi，", C_BLACK);
  y += kPitch;
  text(kX + 14, y, "输入密码（仅支持 2.4G）", C_BLACK);
  y += kPitch + 12;
  gfx.drawFastHLine(kX, y - 12, SCREEN_W - kX - 12, C_BLACK);
  text(kX, y, "也可以在微信小程序里", C_BLACK);
  y += kPitch;
  text(kX, y, "用蓝牙配网", C_BLACK);

  if (!in.error.empty()) {
    textCentered(SCREEN_W / 2, SCREEN_H - 12, fit(in.error, SCREEN_W - 20).c_str(), C_RED);
  }
}

void renderCalendar(Adafruit_GFX& gfx, const RenderInput& in) {
  u8g2.begin(gfx);
  u8g2.setFontDirection(0);

  gfx.fillScreen(C_WHITE);
  drawHeader(in);
  drawMonthGrid(gfx, in);
  gfx.drawFastVLine(kLeftW, 6, SCREEN_H - 12, C_BLACK);
  drawAgenda(gfx, in);
}

void renderMessage(Adafruit_GFX& gfx, const std::string& title, const std::string& detail) {
  u8g2.begin(gfx);
  u8g2.setFontDirection(0);
  gfx.fillScreen(C_WHITE);
  setFont(kFontCn16);
  textCentered(SCREEN_W / 2, SCREEN_H / 2 - 8, title.c_str(), C_RED);
  setFont(kFontCn12);
  textCentered(SCREEN_W / 2, SCREEN_H / 2 + 18, fit(detail, SCREEN_W - 20).c_str(), C_BLACK);
}

void eventWindow(int y, int m, int d, ics::LocalTime& start, ics::LocalTime& end) {
  start = ics::makeLocal(y, m, 1);
  const ics::LocalTime monthEnd = ics::makeLocal(y, m, cal::daysInMonth(y, m)) + ics::kDay;
  const ics::LocalTime agendaEnd = ics::makeLocal(y, m, d) + AGENDA_DAYS * ics::kDay;
  end = monthEnd > agendaEnd ? monthEnd : agendaEnd;
}
