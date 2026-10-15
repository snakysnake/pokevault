#ifndef POKEVAULT_TRADE_H
#define POKEVAULT_TRADE_H

#include "save.h"

#include <stddef.h>
#include <stdint.h>

/* One Generation 5 trade evolution. item is the held item that selects this
   path. 0 is a plain trade, including Karrablast and Shelmet: the partner
   species is assumed, and nothing in this save is removed to stand in for it. */
typedef struct {
    uint16_t target;
    uint16_t item;
} TradePath;

enum {
    TRADE_OK = 0,
    TRADE_NO = 1,
    TRADE_EGG = 2,
    TRADE_MISMATCH = 3,
    TRADE_IO = 4,
    TRADE_BAD = 5
};

/* Every trade path from this species. Returns how many were written. */
int species_trade_paths(unsigned species, TradePath *out, int cap);

/* 1 when held selects one path. A plain trade ignores the held item. */
int species_trade_ready(unsigned species, unsigned held, TradePath *out);

/* Evolves the Pokémon in the active half of a Generation 5 save.
   expect_species must still be in that slot. On success the record is the
   new species, the Pokédex bit is set, and the block checksums and save
   counter are redone. The other half is left alone. ability_out receives
   the ability written onto the record. */
int trade_apply(uint8_t *file, size_t len, int party, int box, int slot,
                uint16_t expect_species, uint16_t target, uint8_t *ability_out);

/* Reads the save this Pokémon came from, writes a .bak of the previous
   bytes, then writes the evolved save. Updates mon and that save's dex
   bits. Does not re-sort the dex. */
int trade_commit(Dex *dex, MonRef *mon, uint16_t target);

#endif
