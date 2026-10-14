// 在电脑上预览日历画面：与设备使用完全相同的排版代码，输出 BMP。用法见 tools/preview.sh
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "BmpWriter.h"
#include "CalendarRenderer.h"
#include "Canvas3C.h"
#include "config.h"

static bool readFile(const char* path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
  return true;
}

int main(int argc, char** argv) {
  int y = 0, m = 0, d = 0;
  std::vector<const char*> icsFiles, holidayFiles;
  const char* out = "preview.bmp";
  bool setup = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--setup")) {
      setup = true;
    } else if (!strcmp(argv[i], "--ics") && i + 1 < argc) {
      icsFiles.push_back(argv[++i]);
    } else if (!strcmp(argv[i], "--holiday") && i + 1 < argc) {
      holidayFiles.push_back(argv[++i]);
    } else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
      out = argv[++i];
    } else if (sscanf(argv[i], "%d-%d-%d", &y, &m, &d) != 3) {
      fprintf(stderr, "用法: preview [YYYY-MM-DD] [--ics 文件]... [--holiday 文件]... [--setup] [-o 输出.bmp]\n");
      return 2;
    }
  }
  if (y == 0) {
    const time_t now = time(nullptr);
    tm t;
    localtime_r(&now, &t);
    y = t.tm_year + 1900;
    m = t.tm_mon + 1;
    d = t.tm_mday;
  }

  HolidayCN holidays;
  for (const char* path : holidayFiles) {
    std::string json;
    if (!readFile(path, json) || holidays.loadJson(json.data(), json.size()) < 0) {
      fprintf(stderr, "跳过无法解析的节假日文件 %s\n", path);
    }
  }

  ics::LocalTime from, to;
  eventWindow(y, m, d, from, to);
  std::vector<ics::Event> events;
  for (const char* path : icsFiles) {
    std::string data;
    if (!readFile(path, data)) {
      fprintf(stderr, "读不到 %s\n", path);
      continue;
    }
    ics::Parser parser(from, to, TZ_OFFSET_SEC);
    parser.feed(data.data(), data.size());
    parser.finish();
    events.insert(events.end(), parser.events().begin(), parser.events().end());
  }

  RenderInput in;
  in.year = y;
  in.month = m;
  in.day = d;
  in.holidays = &holidays;
  in.events = &events;
  in.icsConfigured = !icsFiles.empty();
  in.status = "电脑预览";

  Canvas3C canvas(SCREEN_W, SCREEN_H);
  if (setup) {
    SetupInput si;
    si.apName = "EPaper-7D24";
    si.apIp = "192.168.4.1";
    si.error = "连不上「HomeWiFi」：密码错误";
    renderSetup(canvas, si);
  } else {
    renderCalendar(canvas, in);
  }

  FILE* f = fopen(out, "wb");
  if (!f) {
    perror(out);
    return 1;
  }
  bmp::write(canvas, [&](const uint8_t* p, size_t n) { fwrite(p, 1, n, f); });
  fclose(f);
  printf("%04d-%02d-%02d：日程 %zu 条，节假日数据 %zu 份\n", y, m, d, events.size(), holidayFiles.size());
  return 0;
}
