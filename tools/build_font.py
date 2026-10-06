#!/usr/bin/env python3
"""Pack an 8x8 font for the PokeVault console.

The printable ASCII set is the public-domain font8x8 basic Latin glyphs.
Glyphs 0x11 through 0x18 are left-aligned stat bars, one to eight pixels
wide. Glyph 0x19 is the horizontal rule under a screen title.
"""

import re
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "tools" / "cache"
OUT = ROOT / "source" / "font.inc"
FONT_URL = "https://raw.githubusercontent.com/dhepper/font8x8/master/font8x8_basic.h"


def load_basic():
    path = CACHE / "font8x8_basic.h"
    if not path.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(FONT_URL, path)
    text = path.read_text()
    glyphs = []
    for body in re.findall(r"\{([^{}]+)\}", text):
        nums = [int(n, 16) for n in re.findall(r"0x[0-9A-Fa-f]+", body)]
        if len(nums) == 8:
            glyphs.append(nums)
    if len(glyphs) != 128:
        raise SystemExit(f"expected 128 glyphs, found {len(glyphs)}")
    if glyphs[ord("A")][0] == 0:
        raise SystemExit("letter A is blank")
    return glyphs


def bar(width):
    row = (1 << width) - 1
    glyph = [0] * 8
    for y in range(1, 7):
        glyph[y] = row
    return glyph


def rule():
    glyph = [0] * 8
    glyph[3] = 0xFF
    glyph[4] = 0xFF
    return glyph


def main():
    glyphs = [[0] * 8 for _ in range(256)]
    for i, glyph in enumerate(load_basic()):
        glyphs[i] = glyph
    for width in range(1, 9):
        glyphs[0x10 + width] = bar(width)
    glyphs[0x19] = rule()

    lines = []
    for glyph in glyphs:
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in glyph) + ",")
    OUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
