#ifndef POKEVAULT_TRADE_H
#define POKEVAULT_TRADE_H

#include "save.h"

#include <stddef.h>
#include <stdint.h>

/* One trade evolution. item is the held item that selects this path, in the
   index of the generation that was asked about. 0 is a plain trade.
   Karrablast and Shelmet are not listed: they evolve only when a transfer
   sends them for each other. Generation 3 has the Hoenn trades and not the
   ones added later. Generation 4 adds those, except Feebas. */
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

/* 1 when held selects one path in Generation 5. A plain trade ignores the item. */
int species_trade_ready(unsigned species, unsigned held, TradePath *out);

/* generation is 3, 4, or 5. held is that generation's item index.
   Generation 3 stops at the Hoenn dex. Generation 4 has no Feebas trade
   and no Unova trades. */
int species_trade_ready_gen(unsigned species, unsigned held, int generation, TradePath *out);

/* item is the Generation 4 index from species_trade_paths. 0 is a plain trade.
   On success, out->item is the index that generation stores. */
int species_trade_in_gen(unsigned species, unsigned item, int generation, TradePath *out);

/* The ability this trade writes, given the ability on the record now.
   0 when this generation has no such trade. */
uint8_t trade_mapped_ability(unsigned species, unsigned held, int generation, uint8_t current);

/* Evolves the Pokémon in the active half of a Generation 3, 4, or 5 save.
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
