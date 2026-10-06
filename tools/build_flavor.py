#!/usr/bin/env python3
"""Write one English Pokedex entry for each Gen 5 species.

Entries come from PokeAPI's species flavor text. Black is preferred,
then White, the sequels, and earlier games, so species 1 through 649
all have a sentence. The console font is ASCII, so accents are folded.
"""

import csv
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "tools" / "cache"
OUT = ROOT / "source" / "flavor.inc"

URL = "https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/pokemon_species_flavor_text.csv"

# language 9 is English. Earlier ids win.
VERSION_RANK = {
    17: 0,   # black
    18: 1,   # white
    21: 2,   # black 2
    22: 3,   # white 2
    15: 4,   # heartgold
    16: 5,   # soulsilver
    14: 6,   # platinum
    12: 7,   # diamond
    13: 8,   # pearl
    9: 9,    # emerald
    10: 10,  # firered
    11: 11,  # leafgreen
    7: 12,   # ruby
    8: 13,   # sapphire
    4: 14,   # gold
    5: 15,   # silver
    6: 16,   # crystal
    1: 17,   # red
    2: 18,   # blue
    3: 19,   # yellow
}

FOLD = {
    "\u00e9": "e", "\u00c9": "E",
    "\u00e8": "e", "\u00ea": "e", "\u00eb": "e",
    "\u00e1": "a", "\u00e0": "a", "\u00e4": "a", "\u00e2": "a",
    "\u00f3": "o", "\u00f2": "o", "\u00f6": "o", "\u00f4": "o",
    "\u00fa": "u", "\u00f9": "u", "\u00fc": "u", "\u00fb": "u",
    "\u00ed": "i", "\u00ec": "i", "\u00ef": "i",
    "\u00f1": "n", "\u00df": "ss",
    "\u2019": "'", "\u2018": "'",
    "\u201c": '"', "\u201d": '"',
    "\u2014": "-", "\u2013": "-",
    "\u2026": "...",
    "\u2640": "F", "\u2642": "M",
}


def load_csv():
    path = CACHE / "pokemon_species_flavor_text.csv"
    if not path.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(URL, path)
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def fold(text):
    text = text.replace("\\f", " ").replace("\\n", " ").replace("\\r", " ")
    text = text.replace("\f", " ").replace("\n", " ").replace("\r", " ")
    for src, dst in FOLD.items():
        text = text.replace(src, dst)
    chars = []
    for ch in text:
        chars.append(ch if 32 <= ord(ch) <= 126 else " ")
    squashed = " ".join("".join(chars).split())
    if len(squashed) > 360:
        cut = squashed[:360]
        if " " in cut:
            cut = cut.rsplit(" ", 1)[0]
        squashed = cut
    return squashed


def c_string(text):
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def main():
    best = {}
    for row in load_csv():
        if int(row["language_id"]) != 9:
            continue
        species = int(row["species_id"])
        if not 1 <= species <= 649:
            continue
        version = int(row["version_id"])
        rank = VERSION_RANK.get(version, 100)
        text = fold(row["flavor_text"])
        if not text:
            continue
        current = best.get(species)
        if current is None or rank < current[0]:
            best[species] = (rank, text)

    missing = [species for species in range(1, 650) if species not in best]
    if missing:
        raise SystemExit(f"missing flavor text for {missing[:8]}")
    bulba = best[1][1].lower()
    if "seed" not in bulba:
        raise SystemExit(f"Bulbasaur entry looks wrong: {best[1][1]}")
    if any(ord(ch) > 126 for ch in best[649][1]):
        raise SystemExit("Genesect entry is not ASCII")

    lines = []
    lines.append('"",')
    for species in range(1, 650):
        lines.append(c_string(best[species][1]) + ",")
    OUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUT} ({len(best)} entries)")


if __name__ == "__main__":
    main()
