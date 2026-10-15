#!/usr/bin/env python3
"""Write Gen 1-5 evolution lines for the species card.

Methods are the ones used in Black, White, Black 2, and White 2.
Later additions (Ice Stone Glaceon, Alolan stones, the Linking Cord)
are left out. A line that does not fit the bottom screen fails the build.
"""

import csv
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "tools" / "cache"
OUT = ROOT / "source" / "evo.inc"
SPECIES_C = ROOT / "source" / "species.c"

BASE = "https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/"

# Items that did not evolve anything in Generation 5.
POST_GEN5_ITEMS = {
    "ice-stone",
    "linking-cord",
    "sachet",
    "whipped-dream",
    "galarica-cuff",
    "galarica-wreath",
    "black-augurite",
    "peat-block",
    "sweet-apple",
    "tart-apple",
    "cracked-pot",
    "chipped-pot",
    "syrupy-apple",
    "unremarkable-teacup",
    "masterpiece-teacup",
    "metal-alloy",
    "auspicious-armor",
    "malicious-armor",
    "leaders-crest",
    "scroll-of-darkness",
    "scroll-of-waters",
    "strawberry-sweet",
    "love-sweet",
    "berry-sweet",
    "clover-sweet",
    "flower-sweet",
    "star-sweet",
    "ribbon-sweet",
}

# Short enough to sit under a stage-2 name while the sprite is on screen.
ITEM_LABEL = {
    "water-stone": "Water Stone",
    "thunder-stone": "Thunder Stone",
    "fire-stone": "Fire Stone",
    "leaf-stone": "Leaf Stone",
    "moon-stone": "Moon Stone",
    "sun-stone": "Sun Stone",
    "shiny-stone": "Shiny Stone",
    "dusk-stone": "Dusk Stone",
    "dawn-stone": "Dawn Stone",
    "kings-rock": "King's Rock",
    "metal-coat": "Metal Coat",
    "dragon-scale": "Dragon Scale",
    "up-grade": "Up-Grade",
    "dubious-disc": "Dubious Disc",
    "protector": "Protector",
    "electirizer": "Electirizer",
    "magmarizer": "Magmarizer",
    "reaper-cloth": "Reaper Cloth",
    "deep-sea-tooth": "DeepSeaTooth",
    "deep-sea-scale": "DeepSeaScale",
    "prism-scale": "Prism Scale",
    "oval-stone": "Oval Stone",
    "razor-claw": "Razor Claw",
    "razor-fang": "Razor Fang",
}

# Leafeon, Glaceon, Magnezone, Probopass keep the field, not a later stone.
FIELD_TARGET = {
    470: "Moss Rock",
    471: "Ice Rock",
    462: "Magnetic field",
    476: "Magnetic field",
}

# (from, to) -> exact method text. Guards the lines the card is built around.
EXPECT = {
    (1, 2): "Level 16",
    (2, 3): "Level 32",
    (25, 26): "Thunder Stone",
    (61, 62): "Water Stone",
    (61, 186): "Trade King's Rock",
    (79, 80): "Level 37",
    (79, 199): "Trade King's Rock",
    (82, 462): "Magnetic field",
    (95, 208): "Trade Metal Coat",
    (108, 463): "Rollout",
    (112, 464): "Trade Protector",
    (114, 465): "Ancient Power",
    (117, 230): "Trade Dragon Scale",
    (123, 212): "Trade Metal Coat",
    (125, 466): "Trade Electirizer",
    (126, 467): "Trade Magmarizer",
    (133, 134): "Water Stone",
    (133, 135): "Thunder Stone",
    (133, 136): "Fire Stone",
    (133, 196): "Friendship, day",
    (133, 197): "Friendship, night",
    (133, 470): "Moss Rock",
    (133, 471): "Ice Rock",
    (137, 233): "Trade Up-Grade",
    (190, 424): "Double Hit",
    (193, 469): "Ancient Power",
    (207, 472): "Razor Fang, night",
    (215, 461): "Razor Claw, night",
    (221, 473): "Ancient Power",
    (233, 474): "Trade Dubious Disc",
    (236, 106): "Level 20, Atk > Def",
    (236, 107): "Level 20, Atk < Def",
    (236, 237): "Level 20, Atk = Def",
    (265, 266): "Level 7",
    (265, 268): "Level 7",
    (266, 267): "Level 10",
    (268, 269): "Level 10",
    (281, 282): "Level 30",
    (281, 475): "Dawn Stone, male",
    (290, 291): "Level 20",
    (290, 292): "Empty party slot",
    (299, 476): "Magnetic field",
    (349, 350): "Trade Prism Scale",
    (356, 477): "Trade Reaper Cloth",
    (361, 362): "Level 42",
    (361, 478): "Dawn Stone, female",
    (366, 367): "Trade DeepSeaTooth",
    (366, 368): "Trade DeepSeaScale",
    (406, 315): "Friendship, day",
    (412, 413): "Level 20, female",
    (412, 414): "Level 20, male",
    (415, 416): "Level 21, female",
    (433, 358): "Friendship, night",
    (438, 185): "Mimic",
    (439, 122): "Mimic",
    (440, 113): "Oval Stone, day",
    (447, 448): "Friendship, day",
    (458, 226): "With Remoraid",
    (525, 526): "Trade",
    (533, 534): "Trade",
    (588, 589): "Trade for Shelmet",
    (616, 617): "Trade for Karrablast",
    (64, 65): "Trade",
    (67, 68): "Trade",
    (75, 76): "Trade",
    (93, 94): "Trade",
}


def load_csv(name):
    path = CACHE / name
    if not path.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(BASE + name, path)
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def load_names():
    names = []
    started = False
    for line in SPECIES_C.read_text().splitlines():
        if line.startswith("static const char *const names["):
            started = True
            continue
        if not started:
            continue
        if line.startswith("};"):
            break
        text = line.strip().rstrip(",")
        if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
            names.append(text[1:-1])
    if len(names) != 650:
        raise SystemExit(f"expected 650 species names, found {len(names)}")
    return names


def filled(row, key):
    return row.get(key, "").strip() != ""


def flagged(row, key):
    """True when a cell carries a condition. PokeAPI stores a false flag as 0."""
    return row.get(key, "").strip().lower() not in ("", "0", "false")


def item_id(row, key):
    text = row.get(key, "").strip()
    if text == "":
        return 0
    return int(text)


def title_move(identifier):
    return " ".join(part.capitalize() for part in identifier.split("-"))


def regional_location(identifier):
    text = identifier.lower()
    return any(part in text for part in ("alola", "galar", "hisui", "paldea", "kitakami"))


def describe(row, trigger, items, moves, names):
    gender = {"1": "female", "2": "male"}.get(row["gender_id"].strip(), "")
    tod = row["time_of_day"].strip()
    level = row["minimum_level"].strip()
    held = items.get(item_id(row, "held_item_id"), "")
    stone = items.get(item_id(row, "trigger_item_id"), "")
    move = moves.get(int(row["known_move_id"]), "") if filled(row, "known_move_id") else ""
    party = int(row["party_species_id"]) if filled(row, "party_species_id") else 0
    trade_for = int(row["trade_species_id"]) if filled(row, "trade_species_id") else 0
    stats = row["relative_physical_stats"].strip()
    happy = filled(row, "minimum_happiness")
    where = filled(row, "location_id")

    if trigger == "shed":
        return "Empty party slot"
    if trigger == "trade":
        if trade_for:
            return f"Trade for {names[trade_for]}"
        if held:
            return f"Trade {ITEM_LABEL[held]}"
        return "Trade"
    if trigger == "use-item":
        label = ITEM_LABEL[stone]
        if gender:
            return f"{label}, {gender}"
        return label
    if trigger != "level-up":
        raise SystemExit(f"unhandled trigger {trigger}: {row}")

    if stats != "":
        relation = { "1": "Atk > Def", "-1": "Atk < Def", "0": "Atk = Def" }[stats]
        return f"Level {level}, {relation}"
    if move:
        return title_move(move)
    if party:
        return f"With {names[party]}"
    if held:
        label = ITEM_LABEL[held]
        if tod:
            return f"{label}, {tod}"
        return label
    if where:
        return None  # caller supplies the field name from the target
    if happy:
        if tod:
            return f"Friendship, {tod}"
        return "Friendship"
    if gender:
        return f"Level {level}, {gender}"
    if level:
        return f"Level {level}"
    raise SystemExit(f"no method for {row}")


def drop_row(row, target, group, items, locations, generations):
    """True when this row is a method Generation 5 did not use."""
    # Rows are stamped with the game where that method was introduced.
    # Anything from Generation 6 onward is a later addition.
    if generations.get(row["version_group_id"], 99) > 5:
        return True
    for key in ("trigger_item_id", "held_item_id"):
        name = items.get(item_id(row, key), "")
        if name in POST_GEN5_ITEMS:
            return True
    for key in (
        "minimum_affection",
        "known_move_type_id",
        "party_type_id",
        "needs_overworld_rain",
        "turn_upside_down",
        "needs_multiplayer",
        "used_move_id",
        "minimum_move_count",
        "minimum_damage_taken",
        "nature_bitmask",
        "minimum_steps",
        "region_id",
        "minimum_beauty",
    ):
        if flagged(row, key):
            return True
    loc = locations.get(int(row["location_id"]), "") if filled(row, "location_id") else ""
    if loc and regional_location(loc):
        return True
    # Generation 8 levels Feebas while it holds the Prism Scale. Generation 5 trades it.
    held = items.get(item_id(row, "held_item_id"), "")
    if held == "prism-scale" and row.get("_trigger") == "level-up" and any(
        other.get("_trigger") == "trade" and items.get(item_id(other, "held_item_id"), "") == "prism-scale"
        for other in group
    ):
        return True
    # A later stone does not replace the moss rock, ice rock, or magnetic field.
    stone = items.get(item_id(row, "trigger_item_id"), "")
    if target in FIELD_TARGET and stone and any(filled(other, "location_id") for other in group):
        return True
    return False


def main():
    names = load_names()
    species_rows = load_csv("pokemon_species.csv")
    evo_rows = load_csv("pokemon_evolution.csv")
    item_rows = load_csv("items.csv")
    move_rows = load_csv("moves.csv")
    trigger_rows = load_csv("evolution_triggers.csv")
    location_rows = load_csv("locations.csv")
    version_rows = load_csv("version_groups.csv")

    items = {int(row["id"]): row["identifier"] for row in item_rows}
    moves = {int(row["id"]): row["identifier"] for row in move_rows}
    triggers = {row["id"]: row["identifier"] for row in trigger_rows}
    locations = {int(row["id"]): row["identifier"] for row in location_rows}
    generations = {row["id"]: int(row["generation_id"]) for row in version_rows}

    prevo = [0] * 650
    for row in species_rows:
        species = int(row["id"])
        if not 1 <= species <= 649:
            continue
        parent = row["evolves_from_species_id"].strip()
        prevo[species] = int(parent) if parent else 0
        if prevo[species] > 649:
            raise SystemExit(f"{names[species]} evolves from a later species")

    grouped = {species: [] for species in range(650)}
    for row in evo_rows:
        target = int(row["evolved_species_id"])
        if not 1 <= target <= 649:
            continue
        copied = dict(row)
        copied["_trigger"] = triggers[row["evolution_trigger_id"]]
        grouped[target].append(copied)

    children = {species: [] for species in range(650)}
    for target in range(1, 650):
        group = grouped[target]
        if prevo[target] and not group:
            raise SystemExit(f"{names[target]} has a previous stage and no method")
        if not prevo[target] and group:
            raise SystemExit(f"{names[target]} has a method and no previous stage")
        for row in group:
            row["_trigger"] = triggers[row["evolution_trigger_id"]]
        kept = [row for row in group if not drop_row(row, target, group, items, locations, generations)]
        labels = []
        for row in kept:
            if row["_trigger"] == "level-up" and filled(row, "location_id"):
                if target not in FIELD_TARGET:
                    raise SystemExit(f"location method for {names[target]}: {row}")
                label = FIELD_TARGET[target]
            else:
                label = describe(row, row["_trigger"], items, moves, names)
            labels.append(label)
        unique = []
        for label in labels:
            if label not in unique:
                unique.append(label)
        if len(unique) > 1:
            raise SystemExit(f"{names[target]} has several Generation 5 methods: {unique}")
        if len(unique) == 1:
            children[prevo[target]].append((target, unique[0]))

    for species in range(650):
        children[species].sort()

    edges = {}
    for source, kids in children.items():
        for target, label in kids:
            edges[(source, target)] = label
            if prevo[target] != source:
                raise SystemExit(f"{names[target]} previous stage does not match {names[source]}")

    missing = [pair for pair in EXPECT if pair not in edges]
    if missing:
        raise SystemExit(f"missing evolutions: {missing}")
    for pair, label in EXPECT.items():
        if edges[pair] != label:
            raise SystemExit(f"{names[pair[0]]} -> {names[pair[1]]}: {edges[pair]!r} != {label!r}")
    if any(target > 649 or source > 649 for source, target in edges):
        raise SystemExit("evolution left Generation 5")
    if 700 in {target for _, target in edges} or any(target == 700 for _, target in edges):
        raise SystemExit("Sylveon is not a Generation 5 evolution")

    phrases = sorted(set(edges.values()))
    phrase_at = {text: index for index, text in enumerate(phrases)}
    if len(phrases) > 255:
        raise SystemExit("too many method phrases")

    into = []
    phrase_id = []
    start = [0] * 650
    count = [0] * 650
    for source in range(650):
        start[source] = len(into)
        for target, label in children[source]:
            into.append(target)
            phrase_id.append(phrase_at[label])
        count[source] = len(children[source])

    check_layout(children, names)

    lines = [
        "/* Generated by tools/build_evo.py. Generation 5 methods only. */",
        "static const uint16_t evo_prevo[650] = {",
    ]
    for index in range(0, 650, 16):
        chunk = ", ".join(f"{prevo[i]:3d}" for i in range(index, min(index + 16, 650)))
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    lines.append("static const uint16_t evo_start[650] = {")
    for index in range(0, 650, 16):
        chunk = ", ".join(f"{start[i]:3d}" for i in range(index, min(index + 16, 650)))
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    lines.append("static const uint8_t evo_count[650] = {")
    for index in range(0, 650, 16):
        chunk = ", ".join(f"{count[i]:2d}" for i in range(index, min(index + 16, 650)))
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    lines.append("static const uint16_t evo_into[] = {")
    for index in range(0, len(into), 16):
        chunk = ", ".join(f"{into[i]:3d}" for i in range(index, min(index + 16, len(into))))
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    lines.append("static const uint8_t evo_phrase[] = {")
    for index in range(0, len(phrase_id), 16):
        chunk = ", ".join(f"{phrase_id[i]:3d}" for i in range(index, min(index + 16, len(phrase_id))))
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    lines.append("static const char *const evo_phrases[] = {")
    for text in phrases:
        escaped = text.replace("\\", "\\\\").replace('"', '\\"')
        lines.append(f'    "{escaped}",')
    lines.append("};")
    lines.append("")
    OUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUT.name}: {len(edges)} evolutions, {len(phrases)} methods")


def check_layout(children, names):
    """The sprite covers the right of rows 2-9, so those lines have 22 columns."""

    def root_of(species):
        seen = set()
        while species and species not in seen:
            seen.add(species)
            parent = 0
            for source, kids in children.items():
                if any(target == species for target, _label in kids):
                    parent = source
                    break
            if not parent:
                return species
            species = parent
        raise SystemExit("evolution cycle")

    def walk(species, indent, method, lines):
        lines.append((indent, method, species))
        for target, label in children[species]:
            walk(target, indent + 2, label, lines)

    for species in range(1, 650):
        lines = []
        walk(root_of(species), 0, None, lines)
        if len(lines) > 8:
            raise SystemExit(f"{names[species]} family is too long")
        row = 3
        used = 0
        for indent, method, who in lines:
            if method:
                width = 22 if row <= 9 else 30
                if indent + len(method) > width:
                    raise SystemExit(
                        f"{names[who]} method {method!r} is {indent + len(method)} columns on row {row}"
                    )
                row += 1
                used += 1
            label = f"#{who:03d} {names[who]}"
            width = 22 if row <= 9 else 30
            if indent + len(label) > width:
                raise SystemExit(f"{label} is too wide on row {row}")
            row += 1
            used += 1
        if used > 17:
            raise SystemExit(f"{names[species]} family does not fit")


if __name__ == "__main__":
    main()
