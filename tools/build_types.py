#!/usr/bin/env python3
"""Write Gen 5 types, forme overrides, and the damage chart.

PokeAPI's current types include the Fairy retype from Generation 6.
A Fairy primary is stored as Normal, and a Fairy secondary is dropped,
which restores the Gen 5 typing (Togekiss is Normal/Flying, Marill is
Water). Forme indexes match the Gen 4/5 byte at offset 0x40.

The chart is the Generation 2-5 table. Steel still resists Ghost and
Dark; Fairy does not exist.
"""

import csv
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "tools" / "cache"
OUT = ROOT / "source" / "types.inc"

POKEMON_URL = "https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/pokemon.csv"
POKEMON_TYPES_URL = "https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/pokemon_types.csv"
POKEMON_FORMS_URL = "https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/pokemon_forms.csv"
MOVES_URL = "https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/moves.csv"
EFFICACY_URL = "https://raw.githubusercontent.com/PokeAPI/pokeapi/master/data/v2/csv/type_efficacy.csv"

NAMES = (
    "normal", "fighting", "flying", "poison", "ground", "rock", "bug", "ghost",
    "steel", "fire", "water", "grass", "electric", "psychic", "ice", "dragon",
    "dark",
)
NONE = 17

# (identifier, species, form, primary, secondary or None)
FORM_EXPECT = (
    ("castform-sunny", 351, 1, "fire", None),
    ("castform-rainy", 351, 2, "water", None),
    ("castform-snowy", 351, 3, "ice", None),
    ("wormadam-sandy", 413, 1, "bug", "ground"),
    ("wormadam-trash", 413, 2, "bug", "steel"),
    ("rotom-heat", 479, 1, "electric", "fire"),
    ("rotom-wash", 479, 2, "electric", "water"),
    ("rotom-frost", 479, 3, "electric", "ice"),
    ("rotom-fan", 479, 4, "electric", "flying"),
    ("rotom-mow", 479, 5, "electric", "grass"),
    ("shaymin-sky", 492, 1, "grass", "flying"),
    ("darmanitan-zen", 555, 1, "fire", "psychic"),
    ("meloetta-pirouette", 648, 1, "normal", "fighting"),
)

SPECIES_EXPECT = {
    "bulbasaur": ("grass", "poison"),
    "charmander": ("fire", None),
    "charizard": ("fire", "flying"),
    "pikachu": ("electric", None),
    "clefairy": ("normal", None),
    "jigglypuff": ("normal", None),
    "magnemite": ("electric", "steel"),
    "gastly": ("ghost", "poison"),
    "gengar": ("ghost", "poison"),
    "gyarados": ("water", "flying"),
    "togepi": ("normal", None),
    "togekiss": ("normal", "flying"),
    "snubbull": ("normal", None),
    "marill": ("water", None),
    "azumarill": ("water", None),
    "gardevoir": ("psychic", None),
    "mawile": ("steel", None),
    "skarmory": ("steel", "flying"),
    "swampert": ("water", "ground"),
    "castform": ("normal", None),
    "wormadam-plant": ("bug", "grass"),
    "spiritomb": ("ghost", "dark"),
    "rotom": ("electric", "ghost"),
    "shaymin-land": ("grass", None),
    "arceus": ("normal", None),
    "darmanitan-standard": ("fire", None),
    "meloetta-aria": ("normal", "psychic"),
    "genesect": ("bug", "steel"),
}

MOVE_EXPECT = {
    "tackle": (33, "normal"),
    "flamethrower": (53, "fire"),
    "thunderbolt": (85, "electric"),
    "charm": (204, "normal"),
    "moonlight": (236, "normal"),
    "sweet-kiss": (186, "normal"),
}

# Attack, defend, factor. 0 immune, 1 half, 4 double. Everything else is 2.
CHART_OFF = (
    ("normal", "rock", 1), ("normal", "ghost", 0), ("normal", "steel", 1),
    ("fighting", "normal", 4), ("fighting", "flying", 1), ("fighting", "poison", 1),
    ("fighting", "rock", 4), ("fighting", "bug", 1), ("fighting", "ghost", 0),
    ("fighting", "steel", 4), ("fighting", "psychic", 1), ("fighting", "ice", 4),
    ("fighting", "dark", 4),
    ("flying", "fighting", 4), ("flying", "rock", 1), ("flying", "bug", 4),
    ("flying", "steel", 1), ("flying", "grass", 4), ("flying", "electric", 1),
    ("poison", "poison", 1), ("poison", "ground", 1), ("poison", "rock", 1),
    ("poison", "ghost", 1), ("poison", "steel", 0), ("poison", "grass", 4),
    ("ground", "flying", 0), ("ground", "poison", 4), ("ground", "rock", 4),
    ("ground", "bug", 1), ("ground", "steel", 4), ("ground", "fire", 4),
    ("ground", "grass", 1), ("ground", "electric", 4),
    ("rock", "fighting", 1), ("rock", "flying", 4), ("rock", "ground", 1),
    ("rock", "bug", 4), ("rock", "steel", 1), ("rock", "fire", 4), ("rock", "ice", 4),
    ("bug", "fighting", 1), ("bug", "flying", 1), ("bug", "poison", 1),
    ("bug", "ghost", 1), ("bug", "steel", 1), ("bug", "fire", 1),
    ("bug", "grass", 4), ("bug", "psychic", 4), ("bug", "dark", 4),
    ("ghost", "normal", 0), ("ghost", "ghost", 4), ("ghost", "steel", 1),
    ("ghost", "psychic", 4), ("ghost", "dark", 1),
    ("steel", "rock", 4), ("steel", "steel", 1), ("steel", "fire", 1),
    ("steel", "water", 1), ("steel", "electric", 1), ("steel", "ice", 4),
    ("fire", "rock", 1), ("fire", "bug", 4), ("fire", "steel", 4),
    ("fire", "fire", 1), ("fire", "water", 1), ("fire", "grass", 4),
    ("fire", "ice", 4), ("fire", "dragon", 1),
    ("water", "ground", 4), ("water", "rock", 4), ("water", "fire", 4),
    ("water", "water", 1), ("water", "grass", 1), ("water", "dragon", 1),
    ("grass", "flying", 1), ("grass", "poison", 1), ("grass", "ground", 4),
    ("grass", "rock", 4), ("grass", "bug", 1), ("grass", "steel", 1),
    ("grass", "fire", 1), ("grass", "water", 4), ("grass", "grass", 1),
    ("grass", "dragon", 1),
    ("electric", "flying", 4), ("electric", "ground", 0), ("electric", "water", 4),
    ("electric", "grass", 1), ("electric", "electric", 1), ("electric", "dragon", 1),
    ("psychic", "fighting", 4), ("psychic", "poison", 4), ("psychic", "steel", 1),
    ("psychic", "psychic", 1), ("psychic", "dark", 0),
    ("ice", "flying", 4), ("ice", "ground", 4), ("ice", "steel", 1),
    ("ice", "fire", 1), ("ice", "water", 1), ("ice", "grass", 4),
    ("ice", "ice", 1), ("ice", "dragon", 4),
    ("dragon", "steel", 1), ("dragon", "dragon", 4),
    ("dark", "fighting", 1), ("dark", "ghost", 4), ("dark", "steel", 1),
    ("dark", "psychic", 4), ("dark", "dark", 1),
)


def load_csv(name, url):
    path = CACHE / name
    if not path.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(url, path)
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def type_index(type_id):
    type_id = int(type_id)
    # Fairy moves were Normal in the generations this reader supports.
    if type_id == 18:
        return 0
    if 1 <= type_id <= 17:
        return type_id - 1
    raise SystemExit(f"unknown type id {type_id}")


def pair_of(slots, pid):
    got = slots.get(pid, {})
    return got.get(1, 0), got.get(2, NONE)


def effect(chart, attack, defend1, defend2):
    a = chart[attack][defend1]
    b = 2 if defend2 == NONE else chart[attack][defend2]
    if a == 0 or b == 0:
        return 0
    return a * b


def main():
    pokemon = load_csv("pokemon.csv", POKEMON_URL)
    type_rows = load_csv("pokemon_types.csv", POKEMON_TYPES_URL)
    forms_csv = load_csv("pokemon_forms.csv", POKEMON_FORMS_URL)
    moves = load_csv("moves.csv", MOVES_URL)
    by_name = {}
    for row in pokemon:
        by_name[row["identifier"]] = int(row["id"])

    slots = {}
    for row in type_rows:
        pid = int(row["pokemon_id"])
        slot = int(row["slot"])
        type_id = int(row["type_id"])
        if slot not in (1, 2):
            continue
        if type_id == 18:
            if slot == 1:
                slots.setdefault(pid, {})[1] = 0
            continue
        slots.setdefault(pid, {})[slot] = type_index(type_id)

    species_types = [0] * 650
    species_second = [NONE] * 650
    for name, expect in SPECIES_EXPECT.items():
        pid = by_name[name]
        if not 1 <= pid <= 649:
            raise SystemExit(f"{name} is not a Gen 5 species")
        got = pair_of(slots, pid)
        want = tuple(NAMES.index(part) if part else NONE for part in expect)
        if got != want:
            raise SystemExit(f"{name} is {got}, expected {want}")
    for species in range(1, 650):
        if species not in slots or 1 not in slots[species]:
            raise SystemExit(f"species {species} has no primary type")
        species_types[species], species_second[species] = pair_of(slots, species)

    forms = []
    for identifier, species, form, primary, second in FORM_EXPECT:
        pid = by_name[identifier]
        got = pair_of(slots, pid)
        want = (NAMES.index(primary), NAMES.index(second) if second else NONE)
        if got != want:
            raise SystemExit(f"{identifier} is {got}, expected {want}")
        forms.append((species, form, got[0], got[1]))

    type_of = {name: i for i, name in enumerate(NAMES)}
    for row in forms_csv:
        if not row["identifier"].startswith("arceus-"):
            continue
        plate = row["form_identifier"]
        if plate in ("normal", "unknown", "fairy"):
            continue
        if plate not in type_of:
            raise SystemExit(f"unknown Arceus plate {plate}")
        form = int(row["form_order"]) - 1
        if form < 1 or form > 16:
            raise SystemExit(f"Arceus {plate} form {form} is outside Gen 5")
        forms.append((493, form, type_of[plate], NONE))
    forms.sort()

    chart = [[2 for _ in range(17)] for _ in range(17)]
    for attack, defend, factor in CHART_OFF:
        chart[type_of[attack]][type_of[defend]] = factor

    def species_effect(attack, species_name):
        pid = by_name[species_name]
        primary, second = pair_of(slots, pid)
        return effect(chart, type_of[attack], primary, second)

    if species_effect("fire", "bulbasaur") != 8:
        raise SystemExit("Bulbasaur should be 2x weak to Fire")
    if species_effect("rock", "charizard") != 16:
        raise SystemExit("Charizard should be 4x weak to Rock")
    if species_effect("psychic", "gengar") != 8:
        raise SystemExit("Gengar should be 2x weak to Psychic")
    if species_effect("psychic", "toxicroak") != 16:
        raise SystemExit("Toxicroak should be 4x weak to Psychic")
    if species_effect("ground", "magnemite") != 16:
        raise SystemExit("Magnemite should be 4x weak to Ground")
    if species_effect("electric", "geodude") != 0:
        raise SystemExit("Geodude should be immune to Electric")
    if species_effect("fighting", "spiritomb") != 0:
        raise SystemExit("Spiritomb should have no Fighting weakness")
    if chart[type_of["ghost"]][type_of["steel"]] != 1:
        raise SystemExit("Gen 5 Steel should resist Ghost")
    if chart[type_of["dark"]][type_of["steel"]] != 1:
        raise SystemExit("Gen 5 Steel should resist Dark")

    factor_of = {0: 0, 50: 1, 100: 2, 200: 4}
    for row in load_csv("type_efficacy.csv", EFFICACY_URL):
        attack = int(row["damage_type_id"])
        defend = int(row["target_type_id"])
        if attack == 18 or defend == 18:
            continue
        if not 1 <= attack <= 17 or not 1 <= defend <= 17:
            raise SystemExit(f"unexpected efficacy types {attack} {defend}")
        got = chart[attack - 1][defend - 1]
        published = factor_of[int(row["damage_factor"])]
        # Generation 6 stopped Steel resisting Ghost and Dark.
        if (attack, defend) in ((8, 9), (17, 9)):
            if published != 2 or got != 1:
                raise SystemExit("Steel resist for Ghost/Dark drifted")
            continue
        if got != published:
            raise SystemExit(
                f"{NAMES[attack - 1]} vs {NAMES[defend - 1]} is {got}, chart says {published}"
            )

    move_types = [17] * 560
    by_move = {row["identifier"]: row for row in moves}
    for name, (expect_id, expect_type) in MOVE_EXPECT.items():
        row = by_move[name]
        if int(row["id"]) != expect_id:
            raise SystemExit(f"{name} id is {row['id']}, expected {expect_id}")
        got = NAMES[type_index(row["type_id"])]
        if got != expect_type:
            raise SystemExit(f"{name} is {got}, expected {expect_type}")
    for row in moves:
        mid = int(row["id"])
        if 1 <= mid <= 559:
            move_types[mid] = type_index(row["type_id"])

    lines = [
        "/* Generated by tools/build_types.py.",
        "   species_second uses 17 for a single-typed species.",
        "   type_chart values are 0, 1, 2, 4 for 0x, 1/2x, 1x, 2x.",
        "   Ghost and Dark versus Steel stay at 1/2, the Gen 5 chart. */",
        "static const uint8_t species_types[650] = {",
    ]
    for i in range(0, 650, 20):
        chunk = ", ".join(f"{n:2d}" for n in species_types[i:i + 20])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    lines.append("static const uint8_t species_second[650] = {")
    for i in range(0, 650, 20):
        chunk = ", ".join(f"{n:2d}" for n in species_second[i:i + 20])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    lines.append("static const struct {")
    lines.append("    uint16_t species;")
    lines.append("    uint8_t form;")
    lines.append("    uint8_t type1;")
    lines.append("    uint8_t type2;")
    lines.append("} form_types[] = {")
    for species, form, type1, type2 in forms:
        lines.append(f"    {{{species}, {form}, {type1}, {type2}}},")
    lines.append("};")
    lines.append("")
    lines.append("static const uint8_t type_chart[17][17] = {")
    for row in chart:
        chunk = ", ".join(str(n) for n in row)
        lines.append(f"    {{{chunk}}},")
    lines.append("};")
    lines.append("")
    lines.append("static const uint8_t move_types[560] = {")
    for i in range(0, 560, 20):
        chunk = ", ".join(f"{n:2d}" for n in move_types[i:i + 20])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    OUT.write_text("\n".join(lines) + "\n")
    print(f"wrote {OUT} ({len(forms)} forme rows)")


if __name__ == "__main__":
    main()
