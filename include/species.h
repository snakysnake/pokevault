#ifndef POKEVAULT_SPECIES_H
#define POKEVAULT_SPECIES_H

#include <stdint.h>

const char *species_name(unsigned species);
uint8_t species_growth(unsigned species);

/* Ruby, Sapphire, Emerald, FireRed, and LeafGreen store an internal species
   index. Kanto and Johto match the National Dex. Hoenn does not: Blaziken is
   282, which is Gardevoir's national number. Returns 0 for an unused index. */
uint16_t species_from_gen3(uint16_t internal);

#endif
