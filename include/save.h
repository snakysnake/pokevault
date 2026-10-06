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
    MAX_MONS = 16384,
    MAX_SAVES = 48
};

typedef struct __attribute__((packed)) {
    uint16_t species;
    uint16_t order;
    uint8_t level;
    uint8_t flags;
    uint8_t save_index;
    uint8_t box;
    uint8_t slot;
} MonRef;

typedef struct {
    char name[32];
    char game[8];
    uint16_t count;
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

/* Reads one save. Returns false when the file is not a Gen 3, 4, or 5 save.
   Never writes to the buffer. */
bool save_read(Dex *dex, const char *display_name, const uint8_t *data, size_t len);

#endif
