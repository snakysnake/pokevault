#ifndef POKEVAULT_IDENTITY_H
#define POKEVAULT_IDENTITY_H

#include <stdint.h>

const char *ability_name(unsigned ability);
/* generation is 3, or 4 for the shared Generation 4 and 5 item index. */
const char *item_name(unsigned generation, unsigned item);
/* origin is the save's version id. An unknown place returns an empty string. */
const char *location_name(unsigned origin, unsigned loc);
const char *origin_name(unsigned origin);

/* 0 male, 1 female, 2 genderless. Generation 3 stores this in the personality. */
uint8_t species_gender(unsigned species, uint32_t pid);
/* second selects the other ability. A species with one ability returns that one. */
uint8_t species_ability(unsigned species, int second);

#endif
