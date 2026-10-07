#include "identity.h"

#include "identity.inc"

static const char *entry_at(const char *const *table, unsigned count, unsigned index)
{
    if (index >= count || !table[index] || !table[index][0])
        return "";
    return table[index];
}

static const char *ranged(const char *const *table, unsigned count, unsigned base, unsigned loc)
{
    if (loc < base)
        return "";
    return entry_at(table, count, loc - base);
}

const char *ability_name(unsigned ability)
{
    return entry_at(ability_names, (unsigned)(sizeof ability_names / sizeof ability_names[0]), ability);
}

const char *item_name(unsigned generation, unsigned item)
{
    if (generation <= 3)
        return entry_at(item_names3, (unsigned)(sizeof item_names3 / sizeof item_names3[0]), item);
    return entry_at(item_names45, (unsigned)(sizeof item_names45 / sizeof item_names45[0]), item);
}

const char *location_name(unsigned origin, unsigned loc)
{
    const char *name = "";
    if (origin >= 1 && origin <= 5) {
        name = entry_at(loc3_names, (unsigned)(sizeof loc3_names / sizeof loc3_names[0]), loc);
    } else if (origin == 7 || origin == 8 || (origin >= 10 && origin <= 12)) {
        name = entry_at(loc4_names, (unsigned)(sizeof loc4_names / sizeof loc4_names[0]), loc);
        if (!name[0])
            name = ranged(loc4_names_2000, (unsigned)(sizeof loc4_names_2000 / sizeof loc4_names_2000[0]), 2000, loc);
        if (!name[0])
            name = ranged(loc4_names_3000, (unsigned)(sizeof loc4_names_3000 / sizeof loc4_names_3000[0]), 3000, loc);
    } else if (origin >= 20 && origin <= 23) {
        name = entry_at(loc5_names, (unsigned)(sizeof loc5_names / sizeof loc5_names[0]), loc);
        if (!name[0])
            name = ranged(loc5_names_30000, (unsigned)(sizeof loc5_names_30000 / sizeof loc5_names_30000[0]), 30000, loc);
        if (!name[0])
            name = ranged(loc5_names_40000, (unsigned)(sizeof loc5_names_40000 / sizeof loc5_names_40000[0]), 40000, loc);
        if (!name[0])
            name = ranged(loc5_names_60000, (unsigned)(sizeof loc5_names_60000 / sizeof loc5_names_60000[0]), 60000, loc);
    }
    return name;
}

const char *origin_name(unsigned origin)
{
    switch (origin) {
    case 1: return "Sapphire";
    case 2: return "Ruby";
    case 3: return "Emerald";
    case 4: return "FireRed";
    case 5: return "LeafGreen";
    case 7: return "HeartGold";
    case 8: return "SoulSilver";
    case 10: return "Diamond";
    case 11: return "Pearl";
    case 12: return "Platinum";
    case 15: return "Colosseum";
    case 20: return "White";
    case 21: return "Black";
    case 22: return "White 2";
    case 23: return "Black 2";
    default: return "";
    }
}

uint8_t species_gender(unsigned species, uint32_t pid)
{
    uint8_t ratio;
    if (species >= sizeof gender_ratio)
        return 2;
    ratio = gender_ratio[species];
    if (ratio == 255)
        return 2;
    if (ratio == 254)
        return 1;
    if (ratio == 0)
        return 0;
    return ((pid & 0xFFu) < ratio) ? 1 : 0;
}

uint8_t species_ability(unsigned species, int second)
{
    uint8_t first;
    uint8_t other;
    if (species >= sizeof ability_slot1)
        return 0;
    first = ability_slot1[species];
    other = ability_slot2[species];
    if (second && other)
        return other;
    return first;
}
