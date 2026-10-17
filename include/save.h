#ifndef POKEVAULT_SAVE_H
#define POKEVAULT_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    MON_SHINY = 1,
    MON_EGG = 2,
    MON_PARTY = 4
};

enum {
    GENDER_MALE = 0,
    GENDER_FEMALE = 1,
    GENDER_NONE = 2
};

enum {
    MAX_MONS = 16384,
    MAX_SAVES = 48,
    NATIONAL_DEX = 649,
    /* One bit per species, species 1 in bit 0. */
    DEX_BYTES = (NATIONAL_DEX + 7) / 8,
    /* Path of the .sav on the card, including the drive prefix. */
    SAVE_PATH_MAX = 192
};

/* ivs uses the save's bit layout: 5 bits each of HP, Attack, Defense, Speed,
   Sp. Atk, Sp. Def. evs[] is that same order. Battle stats are calculated
   from these, the nature (personality % 25), and the species base stats.
   form is the Gen 4/5 forme index. Gen 3 leaves it at 0. */
typedef struct {
    uint32_t ivs;
    uint16_t species;
    uint16_t order;
    uint16_t moves[4];
    uint8_t level;
    uint8_t flags;
    uint8_t save_index;
    uint8_t box;
    uint8_t slot;
    uint8_t nature;
    uint8_t form;
    uint8_t evs[6];
    /* ball 0 means the save did not record one. met_month 0 means no date
       (Generation 3 never stores one). met_year is years since 2000. */
    uint8_t ball;
    uint8_t met_year;
    uint8_t met_month;
    uint8_t met_day;
    /* item_gen is 3, or 4 for the shared Generation 4 and 5 index. 0 is none.
       origin is the version id. gender is GENDER_MALE, GENDER_FEMALE, or
       GENDER_NONE. nick is empty when the Pokémon still has its species name. */
    uint16_t item;
    uint16_t met_loc;
    uint8_t ability;
    uint8_t gender;
    uint8_t origin;
    uint8_t item_gen;
    char nick[12];
} MonRef;

typedef struct {
    uint16_t species;
    uint16_t count;
    uint16_t first;
    uint8_t dex_caught;
    uint8_t dex_seen;
} SpeciesRow;

typedef struct {
    char name[32];
    /* Card path this save was read from. Empty for a save built in a test. */
    char path[SAVE_PATH_MAX];
    /* In-game trainer name. Empty when the save did not have one. */
    char trainer[16];
    char game[16];
    uint32_t money;
    uint16_t hours;
    uint16_t count;
    uint8_t minutes;
    uint8_t seconds;
    /* Gym badges currently held. Always the 8 of that game. HeartGold and
       SoulSilver keep Johto here; their Kanto badges are kanto_badges and
       do not count toward the 8. stars is the trainer card level.
       star_slots is 4 in Generation 3 and 5 in Generations 4 and 5.
       title is 1..14 when a sibling ROM named the cartridge, otherwise 0
       and game stays the pair label (R/S, HG/SS, and the rest). */
    uint8_t badges;
    uint8_t badge_slots;
    uint8_t kanto_badges;
    uint8_t title;
    uint8_t stars;
    uint8_t star_slots;
    /* Pokédex registration for this save, separate from Pokémon still stored. */
    uint8_t dex_caught[DEX_BYTES];
    uint8_t dex_seen[DEX_BYTES];
} SaveInfo;

typedef struct {
    MonRef mons[MAX_MONS];
    int mon_count;
    SaveInfo saves[MAX_SAVES];
    int save_count;
    int files_seen;
    bool truncated;
} Dex;

void dex_clear(Dex *dex);
void dex_sort(Dex *dex, int by_species);

/* Drops a leading "Pokemon - " (or Pokémon, with a dash or similar separator).
   "Pokemon - Silberne Edition" becomes "Silberne Edition". Also drops words
   that contain "patch", and version tags such as "v1" or "v0.4.7". */
void dex_tidy_name(char *name);

/* Groups a species-sorted dex. out[i].first indexes the first copy in dex->mons. */
int dex_species_rows(const Dex *dex, SpeciesRow *out, int cap);

/* Reads one save. Returns false when the file is not a Gen 3, 4, or 5 save.
   Never writes to the buffer. */
bool save_read(Dex *dex, const char *display_name, const uint8_t *data, size_t len);

/* The 14 cartridges that connect before the 3DS, in Pokédex order. */
enum {
    TITLE_NONE = 0,
    TITLE_RUBY = 1,
    TITLE_SAPPHIRE = 2,
    TITLE_EMERALD = 3,
    TITLE_FIRERED = 4,
    TITLE_LEAFGREEN = 5,
    TITLE_DIAMOND = 6,
    TITLE_PEARL = 7,
    TITLE_PLATINUM = 8,
    TITLE_HEARTGOLD = 9,
    TITLE_SOULSILVER = 10,
    TITLE_BLACK = 11,
    TITLE_WHITE = 12,
    TITLE_BLACK2 = 13,
    TITLE_WHITE2 = 14,
    TITLE_COUNT = 14,
    DESIGN_SPECIAL = 15
};

/* 3, 4, or 5. Pair labels and specific titles both count. 0 is unknown. */
int game_generation(const char *game);

/* 1..14, or 0 when the name is a pair label or something else. */
int game_title(const char *game);

const char *title_name(int title);
int title_generation(int title);

/* Four-byte ROM code, region letter included. 0 when it is not one of the 14. */
int title_from_code(const char *code);

/* Writes the title's name into game and stores the id. A paired save stays
   as it was when title is 0. */
void save_set_title(SaveInfo *info, int title);

/* 1 when some save of this title has its 8 gym badges. A pair label unlocks
   neither side. */
int title_unlocked(const Dex *dex, int title);
int titles_unlocked(const Dex *dex);
int special_unlocked(const Dex *dex);

#endif
