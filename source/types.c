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

static int form_slot(unsigned species, unsigned form)
{
    int i;
    int n = (int)(sizeof form_types / sizeof form_types[0]);

    for (i = 0; i < n; i++) {
        if (form_types[i].species == species && form_types[i].form == form)
            return i;
    }
    return -1;
}

unsigned species_type(unsigned species, unsigned form)
{
    int slot;

    if (species >= 650)
        return TYPE_NORMAL;
    slot = form_slot(species, form);
    if (slot >= 0)
        return form_types[slot].type1;
    return species_types[species];
}

unsigned species_type2(unsigned species, unsigned form)
{
    int slot;

    if (species >= 650)
        return TYPE_COUNT;
    slot = form_slot(species, form);
    if (slot >= 0)
        return form_types[slot].type2;
    return species_second[species];
}

int type_effect(unsigned attack, unsigned defend1, unsigned defend2)
{
    int a;
    int b;

    if (attack >= TYPE_COUNT || defend1 >= TYPE_COUNT)
        return 4;
    a = type_chart[attack][defend1];
    b = defend2 >= TYPE_COUNT ? 2 : type_chart[attack][defend2];
    if (a == 0 || b == 0)
        return 0;
    return a * b;
}

unsigned move_type(unsigned move)
{
    if (move == 0 || move >= sizeof move_types / sizeof move_types[0])
        return TYPE_COUNT;
    return move_types[move];
}
