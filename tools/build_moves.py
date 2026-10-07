#!/usr/bin/env python3
"""Write Gen 5 power, accuracy, PP, category, and a short effect.

Stats are reconstructed as of Black 2 and White 2. PokeAPI stores the
latest numbers, and move_changelog.csv records the value each field had
just before a later version group changed it. The earliest change after
Black 2 is therefore the Gen 5 value.

Effects are PokeAPI's short mechanical descriptions, with the Gen 5
effect chance filled in. The console font is ASCII.
"""

import csv
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "tools" / "cache"
OUT = ROOT / "source" / "move_info.inc"

MOVES_URL = "https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/moves.csv"
CHANGELOG_URL = "https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/move_changelog.csv"
EFFECTS_URL = "https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/move_effect_prose.csv"

# black-2-white-2. Later version groups are Gen 6 and on.
GEN5 = 14
MOVE_COUNT = 560

FOLD = {
    "\u00e9": "e", "\u00c9": "E",
    "\u2019": "'", "\u2018": "'",
    "\u00d7": "x",
}


def load_csv(name, url):
    path = CACHE / name
    if not path.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(url, path)
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def value_at(rows, field, current):
    best_vg = None
    best = None
    for row in rows:
        version = int(row["changed_in_version_group_id"])
        if version <= GEN5 or row[field] == "":
            continue
        if best_vg is None or version < best_vg:
            best_vg = version
            best = row[field]
    return current if best is None else best


def fold(text):
    for src, dst in FOLD.items():
        text = text.replace(src, dst)
    chars = []
    for ch in text:
        chars.append(ch if 32 <= ord(ch) <= 126 else " ")
    return " ".join("".join(chars).split())


def c_string(text):
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def main():
    moves = {int(row["id"]): row for row in load_csv("moves.csv", MOVES_URL)}
    logs = {}
    for row in load_csv("move_changelog.csv", CHANGELOG_URL):
        logs.setdefault(int(row["move_id"]), []).append(row)
    effects = {}
    for row in load_csv("move_effect_prose.csv", EFFECTS_URL):
        if row["local_language_id"] != "9":
            continue
        effects[int(row["move_effect_id"])] = row["short_effect"]

    category = [0] * MOVE_COUNT
    power = [0] * MOVE_COUNT
    accuracy = [0] * MOVE_COUNT
    pp = [0] * MOVE_COUNT
    priority = [0] * MOVE_COUNT
    text = [""] * MOVE_COUNT

    for move_id in range(1, MOVE_COUNT):
        row = moves.get(move_id)
        if row is None:
            raise SystemExit(f"missing move {move_id}")
        if int(row["generation_id"]) > 5:
            raise SystemExit(f"{row['identifier']} is newer than Gen 5")
        kind = int(row["damage_class_id"])
        if kind not in (1, 2, 3):
            raise SystemExit(f"{row['identifier']} has damage class {kind}")
        history = logs.get(move_id, [])
        raw_power = value_at(history, "power", row["power"])
        # PokeAPI uses 1 for Hidden Power's variable Gen 5 power.
        if raw_power == "1":
            if row["identifier"] != "hidden-power":
                raise SystemExit(f"{row['identifier']} has sentinel power 1")
            raw_power = ""
        raw_acc = value_at(history, "accuracy", row["accuracy"])
        raw_pp = value_at(history, "pp", row["pp"])
        raw_pri = value_at(history, "priority", row["priority"])
        if raw_pp == "":
            raise SystemExit(f"{row['identifier']} has no PP")
        chance = value_at(history, "effect_chance", row["effect_chance"])
        effect_id = int(value_at(history, "effect_id", row["effect_id"]) or 0)
        effect = effects.get(effect_id, "")
        if chance:
            effect = effect.replace("a chance", f"a {int(chance)}% chance")
        effect = fold(effect)
        if not effect:
            raise SystemExit(f"{row['identifier']} has no effect")
        category[move_id] = kind
        power[move_id] = int(raw_power) if raw_power else 0
        accuracy[move_id] = int(raw_acc) if raw_acc else 0
        pp[move_id] = int(raw_pp)
        priority[move_id] = int(raw_pri or 0)
        text[move_id] = effect
        if not 0 <= power[move_id] <= 255:
            raise SystemExit(f"{row['identifier']} power {power[move_id]}")
        if not 0 <= accuracy[move_id] <= 100:
            raise SystemExit(f"{row['identifier']} accuracy {accuracy[move_id]}")
        if not 1 <= pp[move_id] <= 64:
            raise SystemExit(f"{row['identifier']} PP {pp[move_id]}")
        if not -8 <= priority[move_id] <= 8:
            raise SystemExit(f"{row['identifier']} priority {priority[move_id]}")

    by_name = {row["identifier"]: int(row["id"]) for row in moves.values()}

    def expect(name, **want):
        move_id = by_name[name]
        got = {
            "category": category[move_id],
            "power": power[move_id],
            "accuracy": accuracy[move_id],
            "pp": pp[move_id],
            "priority": priority[move_id],
        }
        for key, value in want.items():
            if key == "effect":
                if value not in text[move_id]:
                    raise SystemExit(f"{name} effect is {text[move_id]!r}")
                continue
            if got[key] != value:
                raise SystemExit(f"{name} {key} is {got[key]}, expected {value}")

    expect("tackle", category=2, power=50, accuracy=100, pp=35, priority=0)
    expect("flamethrower", category=3, power=95, accuracy=100, pp=15)
    expect("thunderbolt", category=3, power=95, accuracy=100, pp=15, effect="10%")
    expect("knock-off", category=2, power=20, accuracy=100, pp=20)
    expect("swift", category=3, power=60, accuracy=0, pp=20)
    expect("charm", category=1, power=0, accuracy=100, pp=20)
    expect("hidden-power", category=3, power=0, accuracy=100, effect="30")
    expect("vine-whip", category=2, power=35, accuracy=100, pp=15)
    expect("quick-attack", category=2, power=40, priority=1)
    expect("roar", category=1, power=0, priority=-6)
    expect("extreme-speed", category=2, power=80, priority=2)
    expect("fire-fang", effect="10% chance to burn")
    if any(ord(ch) > 126 for ch in text[85]):
        raise SystemExit("Thunderbolt effect is not ASCII")

    def dump(values, width=20):
        lines = []
        for i in range(0, MOVE_COUNT, width):
            chunk = ", ".join(f"{n:3d}" for n in values[i:i + width])
            lines.append(f"    {chunk},")
        return lines

    lines = [
        "/* Generated by tools/build_moves.py.",
        "   Stats are Black 2 / White 2. Category is 1 status, 2 physical, 3 special.",
        "   Power 0 has no fixed power. Accuracy 0 does not check accuracy.",
        "   move_effect is a short mechanical description. */",
        "static const uint8_t move_category[560] = {",
        *dump(category),
        "};",
        "",
        "static const uint8_t move_power[560] = {",
        *dump(power),
        "};",
        "",
        "static const uint8_t move_accuracy[560] = {",
        *dump(accuracy),
        "};",
        "",
        "static const uint8_t move_pp[560] = {",
        *dump(pp),
        "};",
        "",
        "static const int8_t move_priority[560] = {",
        *dump(priority),
        "};",
        "",
        "static const char *const move_effects[560] = {",
    ]
    for effect in text:
        lines.append(f"    {c_string(effect)},")
    lines.append("};")
    lines.append("")
    OUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
