#include "stats.h"

#include "base_stats.inc"

static const char *const natures[25] = {
    "Hardy", "Lonely", "Brave", "Adamant", "Naughty",
    "Bold", "Docile", "Relaxed", "Impish", "Lax",
    "Timid", "Hasty", "Serious", "Jolly", "Naive",
    "Modest", "Mild", "Quiet", "Bashful", "Rash",
    "Calm", "Gentle", "Sassy", "Careful", "Quirky"
};

const char *nature_name(unsigned nature)
{
    if (nature >= 25)
        return "????";
    return natures[nature];
}

static void species_base(unsigned species, unsigned form, uint8_t out[6])
{
    int i;
    int n = (int)(sizeof form_stats / sizeof form_stats[0]);

    if (species >= 650) {
        for (i = 0; i < 6; i++)
            out[i] = 0;
        return;
    }
    for (i = 0; i < 6; i++)
        out[i] = base_stats[species][i];
    for (i = 0; i < n; i++) {
        if (form_stats[i].species == species && form_stats[i].form == form) {
            int s;
            for (s = 0; s < 6; s++)
                out[s] = form_stats[i].stats[s];
            return;
        }
    }
}

/* Same integer formula the Gen 3-5 games use. Shedinja's HP is always 1. */
static int battle_stat(int base, int iv, int ev, int level, int hp, int nature_mod, int shedinja)
{
    int core;
    if (hp && shedinja)
        return 1;
    core = (2 * base + iv + ev / 4) * level / 100;
    if (hp)
        return core + level + 10;
    core += 5;
    if (nature_mod > 0)
        return core * 110 / 100;
    if (nature_mod < 0)
        return core * 90 / 100;
    return core;
}

void mon_battle_stats(const MonRef *mon, uint16_t out[6])
{
    uint8_t base[6];
    int level;
    int up;
    int down;
    int i;
    /* Save order is HP, Attack, Defense, Speed, Sp. Atk, Sp. Def.
       The summary screen shows Speed last. */
    static const int save_index[6] = {0, 1, 2, 4, 5, 3};
    static const int nature_slot[6] = {-1, 0, 1, 3, 4, 2};

    if (!mon || !out)
        return;
    species_base(mon->species, mon->form, base);
    level = mon->level;
    if (level < 1)
        level = 1;
    if (level > 100)
        level = 100;
    if (mon->nature < 25) {
        up = (int)mon->nature / 5;
        down = (int)mon->nature % 5;
    } else {
        up = 0;
        down = 0;
    }
    for (i = 0; i < 6; i++) {
        int from = save_index[i];
        int iv = (int)((mon->ivs >> (from * 5)) & 31u);
        int ev = mon->evs[from];
        int mod = 0;
        int value;
        int slot = nature_slot[i];
        if (slot >= 0 && up != down) {
            if (slot == up)
                mod = 1;
            else if (slot == down)
                mod = -1;
        }
        value = battle_stat(base[i], iv, ev, level, i == 0, mod, mon->species == 292);
        if (value < 0)
            value = 0;
        out[i] = (uint16_t)value;
    }
}
