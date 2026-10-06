#!/usr/bin/env python3
"""Write Gen 5 primary types for species and moves.

PokeAPI's current types include the Fairy retype from Generation 6.
Fairy was Normal (or the previous primary type) in the games this
reader supports, so a Fairy primary is stored as Normal. Forme indexes
match the Gen 4/5 byte at offset 0x40.
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

NAMES = (
    "normal", "fighting", "flying", "poison", "ground", "rock", "bug", "ghost",
    "steel", "fire", "water", "grass", "electric", "psychic", "ice", "dragon",
    "dark",
)

# Separate pokemon rows whose primary type changes with the forme byte.
CASTFORM = (
    (1, "castform-sunny", "fire"),
    (2, "castform-rainy", "water"),
    (3, "castform-snowy", "ice"),
)

SPECIES_EXPECT = {
    "bulbasaur": "grass",
    "charmander": "fire",
    "squirtle": "water",
    "pikachu": "electric",
    "clefairy": "normal",
    "jigglypuff": "normal",
    "togepi": "normal",
    "snubbull": "normal",
    "marill": "water",
    "gardevoir": "psychic",
    "mawile": "steel",
    "gengar": "ghost",
    "magnemite": "electric",
    "rotom": "electric",
    "wormadam-plant": "bug",
    "castform": "normal",
    "arceus": "normal",
    "genesect": "bug",
}

MOVE_EXPECT = {
    "tackle": (33, "normal"),
    "flamethrower": (53, "fire"),
    "thunderbolt": (85, "electric"),
    "charm": (204, "normal"),
    "moonlight": (236, "normal"),
    "sweet-kiss": (186, "normal"),
}


def load_csv(name, url):
    path = CACHE / name
    if not path.exists():
        CACHE.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(url, path)
    with path.open(newline="") as handle:
        return list(csv.DictReader(handle))


def type_index(type_id):
    type_id = int(type_id)
    if type_id == 18:
        return 0
    if 1 <= type_id <= 17:
        return type_id - 1
    raise SystemExit(f"unknown type id {type_id}")


def main():
    pokemon = load_csv("pokemon.csv", POKEMON_URL)
    type_rows = load_csv("pokemon_types.csv", POKEMON_TYPES_URL)
    forms_csv = load_csv("pokemon_forms.csv", POKEMON_FORMS_URL)
    moves = load_csv("moves.csv", MOVES_URL)

    by_name = {}
    for row in pokemon:
        by_name[row["identifier"]] = int(row["id"])

    primary = {}
    for row in type_rows:
        if int(row["slot"]) != 1:
            continue
        primary[int(row["pokemon_id"])] = type_index(row["type_id"])

    species_types = [0] * 650
    for name, expect in SPECIES_EXPECT.items():
        pid = by_name[name]
        if not 1 <= pid <= 649:
            raise SystemExit(f"{name} is not a Gen 5 species")
        got = NAMES[primary[pid]]
        if got != expect:
            raise SystemExit(f"{name} primary is {got}, expected {expect}")
    for species in range(1, 650):
        if species not in primary:
            raise SystemExit(f"species {species} has no primary type")
        species_types[species] = primary[species]

    forms = []
    for form, identifier, expect in CASTFORM:
        pid = by_name[identifier]
        got = NAMES[primary[pid]]
        if got != expect:
            raise SystemExit(f"{identifier} is {got}, expected {expect}")
        forms.append((351, form, primary[pid]))

    # Arceus plates share pokemon id 493. The forme name is the type, and the
    # Gen 4/5 forme index is one less than PokeAPI's form_order. Fairy is later.
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
        forms.append((493, form, type_of[plate]))
    forms.sort()

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
        "/* Generated by tools/build_types.py. Type 17 marks an unknown move. */",
        "static const uint8_t species_types[650] = {",
    ]
    for i in range(0, 650, 20):
        chunk = ", ".join(f"{n:2d}" for n in species_types[i:i + 20])
        lines.append(f"    {chunk},")
    lines.append("};")
    lines.append("")
    lines.append("static const struct {")
    lines.append("    uint16_t species;")
    lines.append("    uint8_t form;")
    lines.append("    uint8_t type;")
    lines.append("} form_types[] = {")
    for species, form, type_id in forms:
        lines.append(f"    {{{species}, {form}, {type_id}}},")
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
