#include "types.h"

#include <stdint.h>

#include "types.inc"

static const char *const type_names[TYPE_COUNT] = {
    "Normal", "Fighting", "Flying", "Poison", "Ground", "Rock",
    "Bug", "Ghost", "Steel", "Fire", "Water", "Grass",
    "Electric", "Psychic", "Ice", "Dragon", "Dark"
};

const char *type_name(unsigned type)
{
    if (type >= TYPE_COUNT)
        return "????";
    return type_names[type];
}

unsigned species_type(unsigned species, unsigned form)
{
    int i;
    int n = (int)(sizeof form_types / sizeof form_types[0]);

    if (species >= 650)
        return TYPE_NORMAL;
    for (i = 0; i < n; i++) {
        if (form_types[i].species == species && form_types[i].form == form)
            return form_types[i].type;
    }
    return species_types[species];
}

unsigned move_type(unsigned move)
{
    if (move == 0 || move >= sizeof move_types / sizeof move_types[0])
        return TYPE_COUNT;
    return move_types[move];
}
