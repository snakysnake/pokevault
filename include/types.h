#ifndef POKEVAULT_TYPES_H
#define POKEVAULT_TYPES_H

enum {
    TYPE_NORMAL = 0,
    TYPE_FIGHTING,
    TYPE_FLYING,
    TYPE_POISON,
    TYPE_GROUND,
    TYPE_ROCK,
    TYPE_BUG,
    TYPE_GHOST,
    TYPE_STEEL,
    TYPE_FIRE,
    TYPE_WATER,
    TYPE_GRASS,
    TYPE_ELECTRIC,
    TYPE_PSYCHIC,
    TYPE_ICE,
    TYPE_DRAGON,
    TYPE_DARK,
    TYPE_COUNT
};

const char *type_name(unsigned type);

/* Primary type. form is the Gen 4/5 forme index; Gen 3 passes 0.
   Castform weather, Rotom appliances, and Arceus plates change it. */
unsigned species_type(unsigned species, unsigned form);

/* Second type, or TYPE_COUNT when the Pokemon has only one.
   form uses the same Gen 4/5 index as species_type. */
unsigned species_type2(unsigned species, unsigned form);

/* Gen 5 damage factor of an attacking type against one or two defenders.
   0, 1, 2, 4, 8, and 16 mean 0x, 1/4x, 1/2x, 1x, 2x, and 4x.
   defend2 is TYPE_COUNT for a single-typed Pokemon. */
int type_effect(unsigned attack, unsigned defend1, unsigned defend2);

/* TYPE_COUNT when the move is empty or outside Gen 5. */
unsigned move_type(unsigned move);

#endif
