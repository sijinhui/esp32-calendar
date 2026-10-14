#!/usr/bin/env python3
"""
生成 lib/ChineseCalendar/src/lunar_data.h —— 农历月份表 + 二十四节气日期表。

数据来源：lunar_python（6tail，基于寿星天文历算法，北京时间）。
    python3 -m venv .venv && .venv/bin/pip install lunar_python
    .venv/bin/python tools/gen_lunar_data.py

编码格式（与 ChineseCalendar.cpp 对应）：
  LUNAR_YEARS[i]  —— 农历年 LUNAR_FIRST_YEAR + i
      bit 0..12   该年按顺序第 k 个月（含闰月）是否为大月（30 天）
      bit 13..16  闰几月（0 = 无闰月；闰月紧跟在该月之后）
      bit 17..23  正月初一距离同一公历年 1 月 1 日的天数
  SOLAR_TERMS[i][m] —— 公历年 TERM_FIRST_YEAR + i 的第 m+1 月
      低 4 位     本月第一个节气的日（小寒、立春、惊蛰……）
      高 4 位     本月第二个节气的日 - 15（大寒、雨水、春分……）
"""
import datetime
import pathlib

from lunar_python import LunarYear, Solar

LUNAR_FIRST, LUNAR_LAST = 1900, 2100
TERM_FIRST, TERM_LAST = 1901, 2100

TERM_NAMES = [
    "小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨",
    "立夏", "小满", "芒种", "夏至", "小暑", "大暑", "立秋", "处暑",
    "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至",
]


def lunar_year_word(year: int) -> int:
    months = [m for m in LunarYear.fromYear(year).getMonths() if m.getYear() == year]
    assert len(months) in (12, 13), (year, len(months))
    assert months[0].getMonth() == 1, year

    bits = 0
    leap = 0
    for k, m in enumerate(months):
        assert m.getDayCount() in (29, 30), (year, m.getMonth())
        if m.getDayCount() == 30:
            bits |= 1 << k
        if m.getMonth() < 0:
            leap = -m.getMonth()
    assert (leap != 0) == (len(months) == 13), year

    ny = Solar.fromJulianDay(months[0].getFirstJulianDay())
    assert ny.getYear() == year, year
    offset = (datetime.date(year, ny.getMonth(), ny.getDay()) - datetime.date(year, 1, 1)).days
    assert 0 <= offset < 128, (year, offset)
    return bits | (leap << 13) | (offset << 17)


def solar_term_row(year: int) -> list[int]:
    row = []
    for month in range(1, 13):
        days_in_month = (datetime.date(year + (month == 12), month % 12 + 1, 1) - datetime.date(year, month, 1)).days
        found = []
        for day in range(1, days_in_month + 1):
            name = Solar.fromYmd(year, month, day).getLunar().getJieQi()
            if name:
                found.append((day, name))
        assert [n for _, n in found] == TERM_NAMES[2 * (month - 1): 2 * month], (year, month, found)
        first, second = found[0][0], found[1][0]
        assert 0 <= first <= 15 and 0 <= second - 15 <= 15, (year, month, found)
        row.append(first | ((second - 15) << 4))
    return row


def main() -> None:
    out = pathlib.Path(__file__).resolve().parent.parent / "lib/ChineseCalendar/src/lunar_data.h"
    lines = [
        "// 自动生成，请勿手改。生成脚本：tools/gen_lunar_data.py（数据来源 lunar_python）",
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"static const int LUNAR_FIRST_YEAR = {LUNAR_FIRST};",
        f"static const int LUNAR_LAST_YEAR = {LUNAR_LAST};",
        f"static const int TERM_FIRST_YEAR = {TERM_FIRST};",
        f"static const int TERM_LAST_YEAR = {TERM_LAST};",
        "",
        f"static const uint32_t LUNAR_YEARS[{LUNAR_LAST - LUNAR_FIRST + 1}] = {{",
    ]
    words = [lunar_year_word(y) for y in range(LUNAR_FIRST, LUNAR_LAST + 1)]
    for i in range(0, len(words), 8):
        chunk = words[i:i + 8]
        lines.append("    " + ", ".join(f"0x{w:06x}" for w in chunk) + f",  // {LUNAR_FIRST + i}")
    lines += ["};", "", f"static const uint8_t SOLAR_TERMS[{TERM_LAST - TERM_FIRST + 1}][12] = {{"]
    for y in range(TERM_FIRST, TERM_LAST + 1):
        row = solar_term_row(y)
        lines.append("    {" + ", ".join(f"0x{b:02x}" for b in row) + f"}},  // {y}")
    lines += ["};", ""]
    out.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
