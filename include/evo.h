#ifndef POKEVAULT_EVO_H
#define POKEVAULT_EVO_H

#include <stdint.h>

/* One Pokémon in an evolution line. method is how this stage is reached
   from the one before it, and is NULL for the basic form. indent is the
   number of spaces the line sits in from the left. */
typedef struct {
    uint16_t species;
    uint8_t indent;
    const char *method;
} EvoMember;

uint16_t species_prevo(unsigned species);
int species_evo_count(unsigned species);
uint16_t species_evo_into(unsigned species, int index);
const char *species_evo_how(unsigned species, int index);

/* The whole line, basic form first. Returns how many stages were written. */
int species_family(unsigned species, EvoMember *out, int cap);

#endif
