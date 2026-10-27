#!/usr/bin/env bash
# 在电脑上预览日历画面（不需要开发板和屏幕），排版代码与设备上完全一样。
#
#   tools/preview.sh                                   今天
#   tools/preview.sh 2026-02-17                        指定日期
#   tools/preview.sh 2026-10-14 --ics tools/preview/sample.ics
#   tools/preview.sh --setup                           配网模式的画面
#
# 结果在 .pio/preview/preview.png；法定节假日数据会自动下载并缓存。
# 依赖库来自 .pio/libdeps/esp32s3，第一次需要先编译过一次固件（或 pio pkg install -e esp32s3）。
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LIBDEPS="$ROOT/.pio/libdeps/esp32s3"
BUILD="$ROOT/.pio/preview"
mkdir -p "$BUILD/obj"

if [ ! -d "$LIBDEPS/U8g2_for_Adafruit_GFX" ]; then
  echo "找不到依赖库，请先运行：pio pkg install -e esp32s3" >&2
  exit 1
fi

INC=(
  -I"$ROOT/tools/preview/shim" -I"$ROOT/include" -I"$ROOT/src"
  -I"$ROOT/lib/ChineseCalendar/src" -I"$ROOT/lib/HolidayCN/src" -I"$ROOT/lib/IcsParser/src" -I"$ROOT/lib/QrCodeGen/src"
  -I"$LIBDEPS/Adafruit GFX Library" -I"$LIBDEPS/U8g2_for_Adafruit_GFX/src" -I"$LIBDEPS/ArduinoJson/src"
)
# 图形库按 Arduino 环境编译（用 shim 里的替身）；ArduinoJson 不定义 ARDUINO，走标准 C++ 接口
DEFS=(-O2 -DU8G2_USE_LARGE_FONTS)
ARDUINO_DEF=(-DARDUINO=100)

SOURCES=(
  "$LIBDEPS/Adafruit GFX Library/Adafruit_GFX.cpp"
  "$LIBDEPS/U8g2_for_Adafruit_GFX/src/U8g2_for_Adafruit_GFX.cpp"
  "$LIBDEPS/U8g2_for_Adafruit_GFX/src/u8g2_fonts.c"
  "$ROOT/lib/ChineseCalendar/src/ChineseCalendar.cpp"
  "$ROOT/lib/HolidayCN/src/HolidayCN.cpp"
  "$ROOT/lib/IcsParser/src/IcsParser.cpp"
  "$ROOT/lib/QrCodeGen/src/qrcodegen.c"
  "$ROOT/src/Canvas3C.cpp"
  "$ROOT/src/CalendarRenderer.cpp"
  "$ROOT/tools/preview/preview.cpp"
)

OBJS=()
for src in "${SOURCES[@]}"; do
  obj="$BUILD/obj/$(basename "${src%.*}").o"
  OBJS+=("$obj")
  if [[ "$src" == "$LIBDEPS"* ]]; then
    # 第三方库只编译一次（字体文件很大，要编译几十秒）
    [ -f "$obj" ] && [ "$obj" -nt "$src" ] && continue
    case "$src" in
      *.c) cc "${DEFS[@]}" "${ARDUINO_DEF[@]}" -w "${INC[@]}" -c "$src" -o "$obj" ;;
      *) c++ -std=gnu++17 "${DEFS[@]}" "${ARDUINO_DEF[@]}" -w "${INC[@]}" -c "$src" -o "$obj" ;;
    esac
  else
    case "$src" in
      "$ROOT/lib/"*.c) cc -std=c99 "${DEFS[@]}" -Wall "${INC[@]}" -c "$src" -o "$obj" ;;
      "$ROOT/lib/"*) c++ -std=gnu++17 "${DEFS[@]}" -Wall "${INC[@]}" -c "$src" -o "$obj" ;;
      *) c++ -std=gnu++17 "${DEFS[@]}" "${ARDUINO_DEF[@]}" -Wall "${INC[@]}" -c "$src" -o "$obj" ;;
    esac
  fi
done
c++ "${OBJS[@]}" -o "$BUILD/preview"

# 当年和次年的法定节假日数据
YEAR=$(date +%Y)
for arg in "$@"; do
  if [[ "$arg" =~ ^([0-9]{4})-[0-9]{1,2}-[0-9]{1,2}$ ]]; then YEAR="${BASH_REMATCH[1]}"; fi
done
HOLIDAY_ARGS=()
for y in "$YEAR" $((YEAR + 1)); do
  f="$BUILD/holiday-$y.json"
  if [ ! -s "$f" ]; then
    curl -sfL --max-time 10 "https://gcore.jsdelivr.net/gh/NateScarlet/holiday-cn@master/$y.json" -o "$f" || rm -f "$f"
  fi
  if [ -s "$f" ]; then HOLIDAY_ARGS+=(--holiday "$f"); fi
done

"$BUILD/preview" "$@" ${HOLIDAY_ARGS[@]+"${HOLIDAY_ARGS[@]}"} -o "$BUILD/preview.bmp"
if command -v sips >/dev/null; then
  sips -s format png "$BUILD/preview.bmp" --out "$BUILD/preview.png" >/dev/null
  echo "→ $BUILD/preview.png"
else
  echo "→ $BUILD/preview.bmp"
fi
