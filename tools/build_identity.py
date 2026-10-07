#!/usr/bin/env python3
"""Write gender ratios, abilities, items, and met-location names.

Gender, abilities, and items come from PokeAPI. Location names are the
in-game place lists, one name per location index, for Generations 3, 4, and 5.
"""

import csv
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "tools" / "cache"
OUT = ROOT / "source" / "identity.inc"

API = "https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/"
LOC = "https://raw.githubusercontent.com/kwsch/PKHeX/master/PKHeX.Core/Resources/text/locations/"

GENDER = { -1: 255, 0: 0, 1: 31, 2: 63, 4: 127, 6: 191, 8: 254 }


def load_csv(name):
    path = CACHE / name
    if not path.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(API + name, path)
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def load_lines(name):
    path = CACHE / name.replace("/", "_")
    if not path.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(LOC + name, path)
    text = path.read_text(encoding="utf-8-sig")
    return [fold(line.strip()) for line in text.splitlines()]


def fold(text):
    out = []
    for ch in text:
        if ch in "\u2019\u2018`\u00b4":
            out.append("'")
            continue
        if ch in "\u2013\u2014\u2212":
            out.append("-")
            continue
        low = ch.lower()
        accent = {
            "á": "a", "à": "a", "â": "a", "ä": "a", "ã": "a", "å": "a",
            "é": "e", "è": "e", "ê": "e", "ë": "e",
            "í": "i", "ì": "i", "î": "i", "ï": "i",
            "ó": "o", "ò": "o", "ô": "o", "ö": "o", "õ": "o",
            "ú": "u", "ù": "u", "û": "u", "ü": "u",
            "ñ": "n", "ç": "c", "œ": "oe", "æ": "ae",
        }
        if low in accent:
            piece = accent[low]
            out.append(piece.upper() if ch.isupper() else piece)
            continue
        if ch == "é":
            out.append("e")
            continue
        if " " <= ch < "\x7f":
            out.append(ch)
    name = "".join(out).strip()
    if name in {"", "-", "--", "---", "?", "???"}:
        return ""
    return name


def c_string(text):
    if not text:
        return "0"
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return '"' + escaped + '"'


def write_ptrs(out, name, values):
    out.write("static const char *const %s[%d] = {\n" % (name, len(values)))
    for value in values:
        out.write("    %s,\n" % c_string(value))
    out.write("};\n\n")


def write_bytes(out, name, values):
    out.write("static const uint8_t %s[%d] = {\n" % (name, len(values)))
    for i in range(0, len(values), 16):
        chunk = ", ".join("%3d" % v for v in values[i:i + 16])
        out.write("    %s,\n" % chunk)
    out.write("};\n\n")


def main():
    species_rows = load_csv("pokemon_species.csv")
    pokemon = load_csv("pokemon.csv")
    abilities = load_csv("pokemon_abilities.csv")
    ability_names = load_csv("ability_names.csv")
    item_names = load_csv("item_names.csv")
    item_index = load_csv("item_game_indices.csv")

    gender = [255] * 650
    for row in species_rows:
        sid = int(row["id"])
        if 1 <= sid <= 649:
            gender[sid] = GENDER[int(row["gender_rate"])]

    base_id = {}
    for row in pokemon:
        pid = int(row["id"])
        sid = int(row["species_id"])
        if pid == sid and 1 <= sid <= 649:
            base_id[sid] = pid

    ability1 = [0] * 650
    ability2 = [0] * 650
    for row in abilities:
        pid = int(row["pokemon_id"])
        slot = int(row["slot"])
        if row.get("is_hidden", "0") in {"1", "true", "True"}:
            continue
        for sid, bid in base_id.items():
            if bid != pid:
                continue
            ability = int(row["ability_id"])
            if ability > 255:
                ability = 0
            if slot == 1:
                ability1[sid] = ability
            elif slot == 2:
                ability2[sid] = ability

    names = {}
    for row in ability_names:
        if row["local_language_id"] != "9":
            continue
        names[int(row["ability_id"])] = fold(row["name"])
    ability_max = max(i for i in names if i <= 164)
    ability_table = [""] * (ability_max + 1)
    for i, name in names.items():
        if 1 <= i <= ability_max:
            ability_table[i] = name

    english = {}
    for row in item_names:
        if row["local_language_id"] != "9":
            continue
        english[int(row["item_id"])] = fold(row["name"])

    def item_table(generations):
        by_index = {}
        for gen in generations:
            for row in item_index:
                if int(row["generation_id"]) != gen:
                    continue
                index = int(row["game_index"])
                name = english.get(int(row["item_id"]), "")
                if index <= 0 or not name:
                    continue
                by_index[index] = name
        if not by_index:
            raise SystemExit("no items for generations %s" % (generations,))
        table = [""] * (max(by_index) + 1)
        for index, name in by_index.items():
            table[index] = name
        return table

    items3 = item_table((3,))
    items45 = item_table((4, 5))

    if ability_table[65] != "Overgrow":
        raise SystemExit("Overgrow was not ability 65")
    if ability1[1] != 65:
        raise SystemExit("Bulbasaur ability was not Overgrow")
    if fold_check(items3, 4, "Poke Ball"):
        raise SystemExit("Generation 3 item 4 was not a Poke Ball")
    if gender[25] != 127 or gender[81] != 255 or gender[29] != 254 or gender[32] != 0:
        raise SystemExit("gender ratios did not match")

    loc3 = load_lines("gen3/text_rsefrlg_00000_en.txt")
    loc4 = load_lines("gen4/text_hgss_00000_en.txt")
    loc4_2000 = load_lines("gen4/text_hgss_02000_en.txt")
    loc4_3000 = load_lines("gen4/text_hgss_03000_en.txt")
    loc5 = load_lines("gen5/text_bw2_00000_en.txt")
    loc5_30000 = load_lines("gen5/text_bw2_30000_en.txt")
    loc5_40000 = load_lines("gen5/text_bw2_40000_en.txt")
    loc5_60000 = load_lines("gen5/text_bw2_60000_en.txt")

    eterna = loc4.index("Eterna Forest") if "Eterna Forest" in loc4 else -1
    if eterna < 0:
        raise SystemExit("Eterna Forest was missing from the Generation 4 list")
    littleroot = loc3.index("Littleroot Town") if "Littleroot Town" in loc3 else -1
    if littleroot < 0:
        raise SystemExit("Littleroot Town was missing from the Generation 3 list")

    with OUT.open("w") as out:
        out.write("/* Generated by tools/build_identity.py. */\n\n")
        write_bytes(out, "gender_ratio", gender)
        write_bytes(out, "ability_slot1", ability1)
        write_bytes(out, "ability_slot2", ability2)
        write_ptrs(out, "ability_names", ability_table)
        write_ptrs(out, "item_names3", items3)
        write_ptrs(out, "item_names45", items45)
        write_ptrs(out, "loc3_names", loc3)
        write_ptrs(out, "loc4_names", loc4)
        write_ptrs(out, "loc4_names_2000", loc4_2000)
        write_ptrs(out, "loc4_names_3000", loc4_3000)
        write_ptrs(out, "loc5_names", loc5)
        write_ptrs(out, "loc5_names_30000", loc5_30000)
        write_ptrs(out, "loc5_names_40000", loc5_40000)
        write_ptrs(out, "loc5_names_60000", loc5_60000)
    print("wrote %s" % OUT)
    print("eterna %d littleroot %d items3 %d items45 %d" % (
        eterna, littleroot, len(items3), len(items45)))


def fold_check(table, index, expect):
    if index >= len(table):
        return True
    return table[index] != expect


if __name__ == "__main__":
    main()
