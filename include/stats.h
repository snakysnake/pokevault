#ifndef POKEVAULT_STATS_H
#define POKEVAULT_STATS_H

#include "save.h"

const char *nature_name(unsigned nature);

/* out is HP, Attack, Defense, Sp. Atk, Sp. Def, Speed. */
void mon_battle_stats(const MonRef *mon, uint16_t out[6]);

#endif
