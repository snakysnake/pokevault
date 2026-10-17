#ifndef POKEVAULT_TRANSFER_H
#define POKEVAULT_TRANSFER_H

#include "save.h"

#include <stddef.h>
#include <stdint.h>

/* A Pokémon leaves one save and arrives in another. Same generation either
   way. Generation 3 reaches Generation 4 (Pal Park). Generation 4 reaches
   Generation 5 (Poké Transfer). Nothing moves backward, and Generation 3
   does not skip ahead to Generation 5.

   Pal Park and Poké Transfer do not evolve. A move inside a generation
   does, by the trades in trade.h. Karrablast and Shelmet evolve only when
   partner is the other one; pass partner_slot < 0 for a one-way send. */

enum {
    XFER_OK = 0,
    XFER_BADGES = 1,
    XFER_GENERATION = 2,
    XFER_ROOM = 3,
    XFER_EGG = 4,
    XFER_MAIL = 5,
    XFER_LAST = 6,
    XFER_ITEM = 7,
    XFER_IO = 8,
    XFER_BAD = 9,
    XFER_NO = 10
};

typedef struct {
    uint16_t species;
    uint16_t item;
    uint16_t met_loc;
    uint8_t ability;
    uint8_t item_gen;
    uint8_t ball;
    uint8_t form;
    uint8_t met_year;
    uint8_t met_month;
    uint8_t met_day;
    uint8_t party;
    uint8_t box;
    uint8_t slot;
} TransferLand;

/* 0 when dest can take mon. Refusals are decided from the dex, before
   either file is opened. */
int transfer_gate(const Dex *dex, const MonRef *mon, int dest);

/* Short reason a save was left off the Send page. Empty when code is OK. */
const char *transfer_reason(int code);

/* 1 when this move evolves. partner 0 is a one-way send. Karrablast
   evolves only when partner is Shelmet, and the other way around. */
int transfer_evolves(uint16_t species, uint16_t item, int generation,
                     uint16_t partner, uint16_t *target);

/* Moves the record inside the two buffers. partner_slot < 0 is a one-way
   send into the first empty box, or the first empty party slot when the
   boxes are full. A Karrablast/Shelmet partner swaps the two slots and
   evolves both. land may be NULL. */
int transfer_apply(uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len,
                   int party, int box, int slot, uint16_t expect,
                   int partner_party, int partner_box, int partner_slot,
                   TransferLand *land);

/* Writes both saves. Each current file is streamed to a .bak first.
   The new bytes go to a .tmp, the destination is renamed, then the source.
   If the source rename fails, both .bak files are put back. */
int transfer_commit(Dex *dex, MonRef *mon, int dest);

/* The next transfer_commit treats the source rename as a failure. */
void transfer_fail_next_source_rename(void);

#endif
