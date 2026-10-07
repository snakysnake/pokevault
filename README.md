# PokeVault

PokeVault is a Nintendo DS homebrew that reads Pokémon saves off the SD card and shows them as one National Dex. It is read only. It never writes to a save.

It is an unofficial fan tool, not affiliated with Nintendo, Game Freak, or The Pokémon Company.

## What it does

Put your `.sav` files on the card, boot `pokevault.nds`, and it walks the card looking for them. Generation 3, 4, and 5 saves are mixed into one list, species #001 through #649.

Each species shows whether you have a copy stored, whether a save has it marked caught or seen, and whether you have a shiny. Open a species and every copy is listed with its level, nature, and which box or party slot it sits in.

A single Pokémon shows:

- Level, nature, and calculated battle stats
- Its four moves
- The ball it was caught in, and the date, when the save has one
- Which file it came from, and its box or party slot
- The Pokédex entry
- Type weaknesses
- A sprite, including the shiny sprite

The home menu also has a Games page and a Progress page. Games lists each save with the trainer name, money, play time, and Pokédex count. Progress is one bar for the current list: shiny, stored, caught in a Pokédex, seen, and not seen.

## Saves it reads

- Ruby and Sapphire
- Emerald
- FireRed and LeafGreen
- Diamond and Pearl
- Platinum
- HeartGold and SoulSilver
- Black and White
- Black 2 and White 2

Files need a `.sav` extension. It looks through the whole card, and skips `_nds`, `hiya`, `gm9`, `dcim`, and `private`. If it finds nothing, the usual places are `roms/nds/saves` and `roms/gba`.

It keeps up to 48 saves and 16,384 Pokémon. Past that, the list says it is full.

## Controls

**Home**

- Up and Down move between Pokédex, Games, and Progress
- A opens the one you are on

**Pokédex**

- Up and Down scroll, Left and Right move one line, L and R turn the page
- A opens that species
- X opens the filter
- B returns to the menu

**A species**

- Up and Down move between copies
- L and R switch the bottom screen between Stats, Entry, and Weak
- B goes back to the list

**Filter**

- Caught: stored in a box or the party
- Seen: marked seen in a Pokédex
- In Dex: marked caught in a Pokédex
- Shiny: a shiny copy is stored
- Games: turn individual saves on or off
- Pokédex: the full National Dex, or only Kanto, Johto, Hoenn, Sinnoh, or Unova

A toggles a row. On Games or Pokédex, A opens that list. B goes back, and from the top of the filter it closes it.

**Games and Progress**

- Up and Down scroll the save list
- B returns to the menu

## Running it

It runs on a DS that already boots homebrew. That includes TWiLight Menu++ on a DSi or 3DS, and a DS or DS Lite with a flashcart running TWiLight Menu or its own kernel. Copy `pokevault.nds` onto the same SD card the menu uses, usually into `roms/nds`, and launch it from the menu like any other homebrew.

The saves can stay where TWiLight already put them. DS saves sit next to the ROM or in `roms/nds/saves`. GBA saves, from GBARunner, sit in `roms/gba`. PokeVault walks the card and reads those `.sav` files in place. It skips TWiLight's own `_nds` folder, and also skips `hiya`, `gm9`, `dcim`, and `private`, so the menu's files are left alone. The card is only read.

Sprites and music are packed inside `pokevault.nds`. The Pokémon data comes from the SD card. TWiLight Menu tells the homebrew where that `.nds` file is, which is what those packed files need. The dex still opens if a loader omits that path. The sprites and music are just blank.

Building it needs [devkitARM](https://devkitpro.org/wiki/Getting_Started). From this folder, `make` writes `pokevault.nds`. `make -f Makefile.host test` runs the save-parsing checks on a computer, with no DS compiler.
