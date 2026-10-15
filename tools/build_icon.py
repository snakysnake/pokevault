#!/usr/bin/env python3
"""Write the 8-bit banner bitmap ndstool embeds as the DS icon.

grit 0.9 writes a GRF that ndstool 2.3 reads one byte off, so the menu
icon comes out scrambled. ndstool's bitmap path is the one that matches
the pixels. Palette index 0 is the transparent color.
"""

import struct
import sys
from pathlib import Path

TRANSPARENT = (0xFF, 0x00, 0xFF)


def read_bmp24(path):
    data = Path(path).read_bytes()
    if data[:2] != b"BM":
        raise SystemExit(f"{path} is not a BMP")
    off = struct.unpack_from("<I", data, 10)[0]
    dib = struct.unpack_from("<I", data, 14)[0]
    w, h, planes, bpp = struct.unpack_from("<iiHH", data, 18)
    if dib < 40 or planes != 1 or bpp != 24 or w != 32 or h != 32:
        raise SystemExit(f"{path} must be a 32x32 24-bit BMP")
    stride = (w * 3 + 3) & ~3
    rows = []
    for y in range(h - 1, -1, -1):
        start = off + y * stride
        row = []
        for x in range(w):
            b, g, r = data[start + x * 3 : start + x * 3 + 3]
            row.append((r, g, b))
        rows.append(row)
    return rows


def palette_for(rows):
    colors = []
    seen = set()
    for row in rows:
        for pixel in row:
            if pixel not in seen:
                seen.add(pixel)
                colors.append(pixel)
    if TRANSPARENT not in colors:
        colors.insert(0, TRANSPARENT)
    else:
        colors.remove(TRANSPARENT)
        colors.insert(0, TRANSPARENT)
    if len(colors) > 16:
        raise SystemExit(f"icon uses {len(colors)} colors; a DS icon allows 16")
    colors.extend([(0, 0, 0)] * (16 - len(colors)))
    return colors


def write_bmp8(path, rows, palette):
    index = {color: i for i, color in enumerate(palette)}
    w = h = 32
    header = 14 + 40 + 16 * 4
    pixels = bytearray()
    for row in reversed(rows):
        pixels += bytes(index[pixel] for pixel in row)
    out = bytearray()
    out += struct.pack("<2sIHHI", b"BM", header + len(pixels), 0, 0, header)
    out += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 8, 0, len(pixels), 2835, 2835, 16, 0)
    for r, g, b in palette:
        out += bytes((b, g, r, 0))
    out += pixels
    Path(path).write_bytes(out)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} icon.bmp build/icon.bmp")
    src, dst = sys.argv[1], sys.argv[2]
    rows = read_bmp24(src)
    write_bmp8(dst, rows, palette_for(rows))


if __name__ == "__main__":
    main()
