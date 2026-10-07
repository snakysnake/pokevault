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
    DEX_BYTES = (NATIONAL_DEX + 7) / 8
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
    /* In-game trainer name. Empty when the save did not have one. */
    char trainer[16];
    char game[8];
    uint32_t money;
    uint16_t hours;
    uint16_t count;
    uint8_t minutes;
    uint8_t seconds;
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

#endif
