#ifndef POKEVAULT_SPECIES_H
#define POKEVAULT_SPECIES_H

#include <stdint.h>

const char *species_name(unsigned species);
uint8_t species_growth(unsigned species);

/* The friendship a hatched Pokémon of this species starts with. */
uint8_t species_base_friendship(unsigned species);

/* Ruby, Sapphire, Emerald, FireRed, and LeafGreen store an internal species
   index. Kanto and Johto match the National Dex. Hoenn does not: Blaziken is
   282, which is Gardevoir's national number. Returns 0 for an unused index. */
uint16_t species_from_gen3(uint16_t internal);

/* National Dex number to the Generation 3 index. Returns 0 when the species
   is not in those games. */
uint16_t species_to_gen3(uint16_t national);

#endif
