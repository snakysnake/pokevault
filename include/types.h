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
   Castform weather and Arceus plates change the primary type. */
unsigned species_type(unsigned species, unsigned form);

/* TYPE_COUNT when the move is empty or outside Gen 5. */
unsigned move_type(unsigned move);

#endif
