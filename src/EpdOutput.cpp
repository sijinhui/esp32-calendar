#include "EpdOutput.h"

#include "Log.h"
#include "config.h"

#if EPD_ENABLED

#include <GxEPD2_3C.h>
#include <SPI.h>

namespace {
// 整屏缓冲（HEIGHT 行）：400x300 三色约 30KB，S3 内存足够，一次刷完不用分页
GxEPD2_3C<EPD_DRIVER, EPD_DRIVER::HEIGHT> display(EPD_DRIVER(EPD_PIN_CS, EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY));

// 三色屏真正刷一次要十几秒；BUSY 很快就变空闲，说明屏幕没在刷（接线不对 / 排线没插好 / 驱动型号不对）
constexpr uint32_t kMinRealRefreshMs = 5000;

// 屏幕上电后 BUSY 是主控芯片主动输出的电平，跟芯片内部上下拉无关。
// 分别开上拉、下拉各读一次，读数跟着变 = 这个 GPIO 没连到屏幕的 BUSY（插错位置、排线没插好、屏幕没供电）
bool busyPinFloating() {
  pinMode(EPD_PIN_BUSY, INPUT_PULLUP);
  delay(2);
  const int up = digitalRead(EPD_PIN_BUSY);
  pinMode(EPD_PIN_BUSY, INPUT_PULLDOWN);
  delay(2);
  const int down = digitalRead(EPD_PIN_BUSY);
  pinMode(EPD_PIN_BUSY, INPUT);
  return up == HIGH && down == LOW;
}

#ifdef EPD_PROBE
// 底层探测（编译时加 -DEPD_PROBE）：不经过 GxEPD2，直接用 GPIO 模拟 SPI 跟屏幕主控对话，
// 看复位 / 上电时 BUSY 怎么变、能不能从 SDI 线读回寄存器，用来区分接线问题和驱动芯片型号不对
void probeWriteByte(uint8_t b) {
  for (int i = 7; i >= 0; --i) {
    digitalWrite(EPD_PIN_MOSI, (b >> i) & 1);
    delayMicroseconds(2);
    digitalWrite(EPD_PIN_SCK, HIGH);
    delayMicroseconds(2);
    digitalWrite(EPD_PIN_SCK, LOW);
  }
}

void probeCommand(uint8_t cmd) {
  digitalWrite(EPD_PIN_DC, LOW);
  digitalWrite(EPD_PIN_CS, LOW);
  probeWriteByte(cmd);
  digitalWrite(EPD_PIN_CS, HIGH);
  digitalWrite(EPD_PIN_DC, HIGH);
}

void probeData(uint8_t d) {
  digitalWrite(EPD_PIN_CS, LOW);
  probeWriteByte(d);
  digitalWrite(EPD_PIN_CS, HIGH);
}

// 复位脉宽测试：先让 UC8176 断电（0x02）、进深度睡眠（0x07 0xA5），再用 ms 毫秒的复位脉冲叫醒。
// 叫醒了的话，上电命令 0x04 会让 BUSY 拉低约 40ms
void probeResetWidth(uint32_t ms) {
  probeCommand(0x02);
  delay(100);
  probeCommand(0x07);
  probeData(0xA5);
  delay(100);
  digitalWrite(EPD_PIN_RST, LOW);
  delay(ms);
  const int during = digitalRead(EPD_PIN_BUSY);
  digitalWrite(EPD_PIN_RST, HIGH);
  delay(20);
  probeCommand(0x04);
  uint32_t lowUs = 0;
  const uint32_t t0 = micros();
  uint32_t last = t0;
  while (micros() - t0 < 300000) {
    const uint32_t now = micros();
    if (digitalRead(EPD_PIN_BUSY) == LOW) lowUs += now - last;
    last = now;
  }
  LOGF("复位脉宽 %3lu ms：复位时 BUSY=%d，上电后 BUSY 低电平 %.1f ms → %s", (unsigned long)ms, during, lowUs / 1000.0,
       lowUs > 5000 ? "叫醒了" : "没叫醒");
}

// 读寄存器：命令发完把 SDI 切成输入，由主控芯片在 SDI 上回数据（SDA/SDI 是双向的）。
// 分别开上拉、下拉各读一遍：两遍一样说明芯片在回话，跟着上下拉变说明线上没人说话
void probeRead(const char* what, uint8_t cmd, size_t n) {
  char out[2][40];
  const uint8_t pulls[2] = {INPUT_PULLUP, INPUT_PULLDOWN};
  for (int p = 0; p < 2; ++p) {
    digitalWrite(EPD_PIN_DC, LOW);
    digitalWrite(EPD_PIN_CS, LOW);
    probeWriteByte(cmd);
    pinMode(EPD_PIN_MOSI, pulls[p]);
    digitalWrite(EPD_PIN_DC, HIGH);
    delayMicroseconds(5);
    int len = 0;
    for (size_t i = 0; i < n; ++i) {
      uint8_t v = 0;
      for (int b = 0; b < 8; ++b) {
        digitalWrite(EPD_PIN_SCK, HIGH);
        delayMicroseconds(2);
        v = (v << 1) | digitalRead(EPD_PIN_MOSI);
        digitalWrite(EPD_PIN_SCK, LOW);
        delayMicroseconds(2);
      }
      len += snprintf(out[p] + len, sizeof out[p] - len, "%02X ", v);
    }
    digitalWrite(EPD_PIN_CS, HIGH);
    pinMode(EPD_PIN_MOSI, OUTPUT);
    delay(2);
  }
  LOGF("读 %s（0x%02X）：上拉 %s/ 下拉 %s%s", what, cmd, out[0], out[1],
       strcmp(out[0], out[1]) == 0 ? "→ 一致，芯片有回应" : "→ 不一致，没有回应");
}

// 记录 BUSY 在 ms 毫秒内的变化：起始电平 + 每次跳变的时刻
void probeTrace(const char* what, uint32_t ms) {
  char buf[200];
  int level = digitalRead(EPD_PIN_BUSY);
  int len = snprintf(buf, sizeof buf, "%s：BUSY 起始=%d", what, level);
  int changes = 0;
  const uint32_t t0 = micros();
  while (micros() - t0 < ms * 1000) {
    const int v = digitalRead(EPD_PIN_BUSY);
    if (v == level) continue;
    level = v;
    if (changes < 8) len += snprintf(buf + len, sizeof buf - len, "，%.1fms→%d", (micros() - t0) / 1000.0, v);
    ++changes;
  }
  if (changes >= 8) snprintf(buf + len, sizeof buf - len, "……共跳变 %d 次", changes);
  LOGF("%s", buf);
}

void probeReset(const char* what) {
  digitalWrite(EPD_PIN_RST, LOW);
  delay(10);
  const int during = digitalRead(EPD_PIN_BUSY);
  digitalWrite(EPD_PIN_RST, HIGH);
  char label[64];
  snprintf(label, sizeof label, "%s（复位拉低时 BUSY=%d）松开后", what, during);
  probeTrace(label, 300);
}

void probe() {
  LOGF("==== 墨水屏底层探测 ====");
  pinMode(EPD_PIN_CS, OUTPUT);
  digitalWrite(EPD_PIN_CS, HIGH);
  pinMode(EPD_PIN_DC, OUTPUT);
  digitalWrite(EPD_PIN_DC, HIGH);
  pinMode(EPD_PIN_SCK, OUTPUT);
  digitalWrite(EPD_PIN_SCK, LOW);
  pinMode(EPD_PIN_MOSI, OUTPUT);
  digitalWrite(EPD_PIN_MOSI, LOW);
  pinMode(EPD_PIN_RST, OUTPUT);
  digitalWrite(EPD_PIN_RST, HIGH);
  delay(20);
  pinMode(EPD_PIN_BUSY, INPUT_PULLUP);
  delay(2);
  const int up = digitalRead(EPD_PIN_BUSY);
  pinMode(EPD_PIN_BUSY, INPUT_PULLDOWN);
  delay(2);
  const int down = digitalRead(EPD_PIN_BUSY);
  pinMode(EPD_PIN_BUSY, INPUT);
  LOGF("BUSY 静态电平：上拉读 %d，下拉读 %d", up, down);

  for (uint32_t ms : {2, 10, 50, 200, 2, 10}) probeResetWidth(ms);
  probeCommand(0x02);
  delay(100);

  // UC8176：BUSY 低 = 忙；上电 0x04 约 40ms 低电平；0x71 状态、0x70 版本
  probeReset("UC8176 测试：复位");
  probeRead("UC8176 状态", 0x71, 1);
  probeRead("UC8176 版本", 0x70, 4);
  probeCommand(0x04);
  probeTrace("UC8176 上电 0x04 后", 300);
  probeRead("UC8176 状态", 0x71, 1);
  probeCommand(0x02);
  probeTrace("UC8176 断电 0x02 后", 300);

  // SSD1683：BUSY 高 = 忙；软复位 0x12 会忙几毫秒；0x2F 状态、0x2E 用户 ID
  probeReset("SSD1683 测试：复位");
  probeCommand(0x12);
  probeTrace("SSD1683 软复位 0x12 后", 500);
  probeRead("SSD1683 状态", 0x2F, 1);
  probeRead("SSD1683 用户ID", 0x2E, 4);
  LOGF("==== 探测结束 ====");
}
#endif
}  // namespace

namespace epd {

bool enabled() { return true; }

bool begin() {
#ifdef EPD_PROBE
  probe();
#endif
  SPI.begin(EPD_PIN_SCK, -1, EPD_PIN_MOSI, EPD_PIN_CS);
  display.epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(115200, true, EPD_RESET_MS, false);
  display.setRotation(0);
  LOGF("墨水屏初始化完成：%dx%d（BUSY=%d CS=%d D/C=%d RES=%d SDI=%d SCL=%d）", display.width(), display.height(),
       EPD_PIN_BUSY, EPD_PIN_CS, EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_MOSI, EPD_PIN_SCK);
  if (busyPinFloating()) {
    LOGF("警告：GPIO %d 上读不到屏幕的 BUSY 信号（引脚悬空），请对照上面的引脚号检查杜邦线、排线和 3.3V 供电", EPD_PIN_BUSY);
  }
  return true;
}

void show(const Canvas3C& canvas) {
  const uint32_t t0 = millis();
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawBitmap(0, 0, canvas.blackPlane(), canvas.rawWidth(), canvas.rawHeight(), GxEPD_BLACK);
    display.drawBitmap(0, 0, canvas.redPlane(), canvas.rawWidth(), canvas.rawHeight(), GxEPD_RED);
  } while (display.nextPage());
  display.hibernate();
  const uint32_t elapsed = millis() - t0;
  if (elapsed < kMinRealRefreshMs) {
    LOGF("墨水屏没有真正刷新：只用了 %lu ms（正常约 16 秒，期间屏幕会闪几下）。请检查接线是否和 config.h 一致、排线是否插紧",
         (unsigned long)elapsed);
  } else {
    LOGF("墨水屏刷新完成，用时 %lu ms", (unsigned long)elapsed);
  }
}

}  // namespace epd

#else

namespace epd {
bool enabled() { return false; }
bool begin() {
  LOGF("EPD_ENABLED=0：不驱动墨水屏，画面请在浏览器预览");
  return false;
}
void show(const Canvas3C&) {}
}  // namespace epd

#endif
