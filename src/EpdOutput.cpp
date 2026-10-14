#include "EpdOutput.h"

#include "Log.h"
#include "config.h"

#if EPD_ENABLED

#include <GxEPD2_3C.h>
#include <SPI.h>

namespace {
// 整屏缓冲（HEIGHT 行）：400x300 三色约 30KB，S3 内存足够，一次刷完不用分页
GxEPD2_3C<EPD_DRIVER, EPD_DRIVER::HEIGHT> display(EPD_DRIVER(EPD_PIN_CS, EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY));
}  // namespace

namespace epd {

bool enabled() { return true; }

bool begin() {
  SPI.begin(EPD_PIN_SCK, -1, EPD_PIN_MOSI, EPD_PIN_CS);
  display.epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(115200, true, EPD_RESET_MS, false);
  display.setRotation(0);
  LOGF("墨水屏初始化完成：%dx%d", display.width(), display.height());
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
  LOGF("墨水屏刷新完成，用时 %lu ms", millis() - t0);
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
