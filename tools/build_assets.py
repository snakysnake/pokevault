#!/usr/bin/env python3
"""Download Black & White sprites and write the ROM sprite bank plus move names.

Sprite pages follow the Pokemon Database gallery, for example
https://pokemondb.net/sprites/kingdra
which serves
https://img.pokemondb.net/sprites/black-white/normal/kingdra.png

The DS sprite hardware tops out at 64x64, so each image is cropped to its
opaque pixels and fitted into a 64x64 256-color sprite. One copy is loaded
from nitro:/sprites.bin when the selection changes.
"""

import csv
import io
import struct
import sys
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "tools" / "cache" / "black-white"
MOVES_CSV = ROOT / "tools" / "cache" / "move_names.csv"
OUT_BIN = ROOT / "nitro" / "sprites.bin"
OUT_MOVES = ROOT / "source" / "moves.c"

COUNT = 649
PX = 64
PAL_BYTES = 256 * 2
TILE_BYTES = PX * PX
IMAGE_BYTES = PAL_BYTES + TILE_BYTES
FLAG_BYTES = COUNT + 1

EXTRA = {
    "wormadam": ["wormadam-plant"],
    "giratina": ["giratina-altered"],
    "shaymin": ["shaymin-land"],
    "basculin": ["basculin-red-striped"],
    "darmanitan": ["darmanitan-standard"],
    "tornadus": ["tornadus-incarnate"],
    "thundurus": ["thundurus-incarnate"],
    "landorus": ["landorus-incarnate"],
    "keldeo": ["keldeo-ordinary"],
    "meloetta": ["meloetta-aria"],
    "shellos": ["shellos-west"],
    "gastrodon": ["gastrodon-west"],
    "burmy": ["burmy-plant"],
    "cherrim": ["cherrim-overcast"],
    "deerling": ["deerling-spring"],
    "sawsbuck": ["sawsbuck-spring"],
    "deoxys": ["deoxys-normal"],
}

HEADERS = {
    "User-Agent": "pokevault/1.0 (personal save viewer)",
    "Referer": "https://pokemondb.net/",
}


def species_names():
    text = (ROOT / "source" / "species.c").read_text()
    start = text.index("static const char *const names[650] = {")
    end = text.index("};", start)
    names = []
    for line in text[start:end].splitlines():
        line = line.strip().rstrip(",")
        if len(line) >= 2 and line[0] == '"' and line[-1] == '"':
            names.append(line[1:-1])
    if len(names) != 650 or names[1] != "Bulbasaur" or names[230] != "Kingdra" or names[649] != "Genesect":
        raise SystemExit("species name table was not where the sprite tool expected")
    return names


def slugify(name):
    out = []
    for ch in name.lower():
        if ch in " .'":
            if ch == " " and (not out or out[-1] != "-"):
                out.append("-")
            continue
        if ch == "é":
            out.append("e")
            continue
        out.append(ch)
    return "".join(out).strip("-")


def candidates(slug):
    names = [slug]
    for extra in EXTRA.get(slug, []):
        if extra not in names:
            names.append(extra)
    return names


def fetch(url, dest, png=False):
    if dest.exists() and dest.stat().st_size > 32:
        cached = dest.read_bytes()
        if not png or cached[:8] == b"\x89PNG\r\n\x1a\n":
            return cached
    req = urllib.request.Request(url, headers=HEADERS)
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            data = resp.read()
    except Exception:
        return None
    if png and data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    if not data:
        return None
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(data)
    return data


def fetch_sprite(kind, slug):
    for name in candidates(slug):
        url = "https://img.pokemondb.net/sprites/black-white/%s/%s.png" % (kind, name)
        data = fetch(url, CACHE / kind / (name + ".png"), png=True)
        if data:
            return data
    return None


def rgb555(r, g, b):
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)


def tile_image(indices):
    out = bytearray()
    for ty in range(PX // 8):
        for tx in range(PX // 8):
            for py in range(8):
                for px in range(8):
                    out.append(indices[(ty * 8 + py) * PX + (tx * 8 + px)])
    return out


def pack_image(im):
    im = im.convert("RGBA")
    mask = im.getchannel("A").point(lambda a: 255 if a >= 128 else 0)
    bbox = mask.getbbox()
    if not bbox:
        return None
    im = im.crop(bbox)
    fitted = Image.new("RGBA", (PX, PX), (0, 0, 0, 0))
    resample = getattr(getattr(Image, "Resampling", Image), "LANCZOS")
    im.thumbnail((PX - 2, PX - 2), resample)
    fitted.paste(im, ((PX - im.width) // 2, (PX - im.height) // 2), im)

    sentinel = (255, 0, 255)
    rgb = Image.new("RGB", (PX, PX), sentinel)
    opaque = bytearray(PX * PX)
    src = fitted.load()
    dst = rgb.load()
    for y in range(PX):
        for x in range(PX):
            r, g, b, a = src[x, y]
            if a >= 128:
                if (r, g, b) == sentinel:
                    b = 254
                dst[x, y] = (r, g, b)
                opaque[y * PX + x] = 1

    method = getattr(getattr(Image, "Quantize", Image), "MEDIANCUT", 0)
    dither = getattr(getattr(Image, "Dither", Image), "NONE", 0)
    quantized = rgb.quantize(colors=256, method=method, dither=dither)
    palette = quantized.getpalette() or []
    ncolors = len(palette) // 3
    if ncolors < 1:
        return None
    pixels = list(quantized.getdata())
    key = min(
        range(ncolors),
        key=lambda i: abs(palette[i * 3] - 255) + palette[i * 3 + 1] + abs(palette[i * 3 + 2] - 255),
    )

    remap = {key: 0}
    colors = [(0, 0, 0)]
    nxt = 1
    for i in range(ncolors):
        if i == key:
            continue
        remap[i] = nxt
        colors.append((palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2]))
        nxt += 1

    indices = bytearray(PX * PX)
    for i, value in enumerate(pixels):
        if opaque[i]:
            indices[i] = remap[value] if value != key else 1
        else:
            indices[i] = 0

    pal = bytearray(PAL_BYTES)
    for i, (r, g, b) in enumerate(colors[:256]):
        struct.pack_into("<H", pal, i * 2, rgb555(r, g, b))
    return bytes(pal) + bytes(tile_image(indices))


def blank_image():
    return bytes(IMAGE_BYTES)


def make_egg():
    im = Image.new("RGBA", (PX, PX), (0, 0, 0, 0))
    draw = ImageDraw.Draw(im)
    draw.ellipse((18, 8, 46, 56), fill=(248, 248, 252, 255), outline=(70, 78, 96, 255))
    draw.ellipse((26, 16, 34, 24), fill=(255, 255, 255, 230))
    draw.ellipse((24, 34, 32, 44), fill=(190, 214, 232, 255))
    draw.ellipse((34, 38, 42, 48), fill=(190, 214, 232, 255))
    return im


def c_escape(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def write_moves():
    url = "https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/move_names.csv"
    data = fetch(url, MOVES_CSV)
    if not data:
        raise SystemExit("could not download move names")
    text = data.decode("utf-8-sig")
    found = {}
    for row in csv.DictReader(io.StringIO(text)):
        if row.get("local_language_id") != "9":
            continue
        found[int(row["move_id"])] = row["name"]
    if found.get(1) != "Pound" or found.get(33) != "Tackle" or found.get(85) != "Thunderbolt":
        raise SystemExit("move name table did not match the expected ids")
    lines = ["#include \"moves.h\"", "", "static const char *const names[] = {"]
    lines.append("    \"????\",")
    for move_id in range(1, 560):
        lines.append("    %s," % c_escape(found.get(move_id, "????")))
    lines.append("};")
    lines.append("")
    lines.append("const char *move_name(unsigned move)")
    lines.append("{")
    lines.append("    if (move >= sizeof names / sizeof names[0])")
    lines.append("        return \"????\";")
    lines.append("    return names[move];")
    lines.append("}")
    lines.append("")
    OUT_MOVES.write_text("\n".join(lines))
    print("wrote %s (%d names)" % (OUT_MOVES, 559))


def build_sprites(names):
    jobs = []
    for species in range(1, COUNT + 1):
        slug = slugify(names[species])
        jobs.append((species, "normal", slug))
        jobs.append((species, "shiny", slug))

    pngs = {}
    done = 0
    with ThreadPoolExecutor(max_workers=8) as pool:
        future_map = {
            pool.submit(fetch_sprite, kind, slug): (species, kind)
            for species, kind, slug in jobs
        }
        for future in as_completed(future_map):
            species, kind = future_map[future]
            pngs[(species, kind)] = future.result()
            done += 1
            if done % 100 == 0 or done == len(jobs):
                print("downloaded %d/%d" % (done, len(jobs)), flush=True)

    images = [blank_image()] * (1 + COUNT * 2)
    flags = bytearray(FLAG_BYTES)
    egg = pack_image(make_egg())
    if not egg or len(egg) != IMAGE_BYTES:
        raise SystemExit("egg sprite failed to pack")
    images[0] = egg
    flags[0] = 1

    missing = []
    normals = 0
    shinies = 0
    for species in range(1, COUNT + 1):
        normal = pngs.get((species, "normal"))
        shiny = pngs.get((species, "shiny"))
        if normal:
            packed = pack_image(Image.open(io.BytesIO(normal)))
            if packed:
                images[1 + (species - 1) * 2] = packed
                flags[species] |= 1
                normals += 1
        if shiny:
            packed = pack_image(Image.open(io.BytesIO(shiny)))
            if packed:
                images[1 + (species - 1) * 2 + 1] = packed
                flags[species] |= 2
                shinies += 1
        if (flags[species] & 1) == 0:
            missing.append(names[species])

    header_size = 20
    data_off = (header_size + FLAG_BYTES + 15) & ~15
    blob = bytearray()
    blob += struct.pack("<4sHHHHII", b"PVSP", 1, COUNT, PX, PX, IMAGE_BYTES, data_off)
    blob += flags
    blob += bytes(data_off - len(blob))
    for image in images:
        blob += image
    OUT_BIN.parent.mkdir(parents=True, exist_ok=True)
    OUT_BIN.write_bytes(blob)
    print("wrote %s (%d bytes)" % (OUT_BIN, len(blob)))
    print("normal %d  shiny %d  missing %d" % (normals, shinies, len(missing)))
    if missing:
        print("missing: " + ", ".join(missing))
    if normals < 600:
        raise SystemExit("too many sprites failed to download")


def main():
    sprites_only = "--sprites" in sys.argv[1:]
    names = species_names()
    assert slugify("Mr. Mime") == "mr-mime"
    assert slugify("Farfetch'd") == "farfetchd"
    assert slugify("Mime Jr.") == "mime-jr"
    assert slugify("Nidoran-F") == "nidoran-f"
    assert slugify("Ho-Oh") == "ho-oh"
    if not sprites_only:
        write_moves()
    build_sprites(names)


if __name__ == "__main__":
    sys.exit(main())
