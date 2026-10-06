#include "save.h"

#include "crypto.h"
#include "species.h"

#include <stdlib.h>
#include <string.h>

void dex_clear(Dex *dex)
{
    dex->mon_count = 0;
    dex->save_count = 0;
    dex->files_seen = 0;
    dex->truncated = false;
}

static int cmp_species(const void *va, const void *vb)
{
    const MonRef *a = va;
    const MonRef *b = vb;
    if (a->species != b->species)
        return a->species < b->species ? -1 : 1;
    if (a->level != b->level)
        return a->level > b->level ? -1 : 1;
    if (a->save_index != b->save_index)
        return a->save_index < b->save_index ? -1 : 1;
    if (a->order != b->order)
        return a->order < b->order ? -1 : 1;
    return 0;
}

static int cmp_file(const void *va, const void *vb)
{
    const MonRef *a = va;
    const MonRef *b = vb;
    int ap = (a->flags & MON_PARTY) ? 0 : 1;
    int bp = (b->flags & MON_PARTY) ? 0 : 1;
    if (a->save_index != b->save_index)
        return a->save_index < b->save_index ? -1 : 1;
    if (ap != bp)
        return ap - bp;
    if (a->box != b->box)
        return a->box < b->box ? -1 : 1;
    if (a->slot != b->slot)
        return a->slot < b->slot ? -1 : 1;
    if (a->order != b->order)
        return a->order < b->order ? -1 : 1;
    return 0;
}

void dex_sort(Dex *dex, int by_species)
{
    if (dex->mon_count > 1) {
        qsort(dex->mons, (size_t)dex->mon_count, sizeof(MonRef),
              by_species ? cmp_species : cmp_file);
    }
}

static int begin_save(Dex *dex, const char *name, const char *game)
{
    SaveInfo *info;
    size_t i;
    if (dex->save_count >= MAX_SAVES)
        return -1;
    info = &dex->saves[dex->save_count];
    memset(info, 0, sizeof *info);
    for (i = 0; i < sizeof info->name - 1 && name && name[i]; i++)
        info->name[i] = name[i];
    for (i = 0; i < sizeof info->game - 1 && game && game[i]; i++)
        info->game[i] = game[i];
    return dex->save_count++;
}

static void add_mon(Dex *dex, int save_index, uint16_t species, uint8_t level,
                    uint8_t flags, uint8_t box, uint8_t slot, const uint16_t moves[4])
{
    MonRef *mon;
    int i;
    if (save_index < 0)
        return;
    if (dex->mon_count >= MAX_MONS) {
        dex->truncated = true;
        return;
    }
    mon = &dex->mons[dex->mon_count];
    mon->species = species;
    mon->order = (uint16_t)dex->mon_count;
    for (i = 0; i < 4; i++)
        mon->moves[i] = moves ? moves[i] : 0;
    mon->level = level;
    mon->flags = flags;
    mon->save_index = (uint8_t)save_index;
    mon->box = box;
    mon->slot = slot;
    dex->mon_count++;
    dex->saves[save_index].count++;
}

static uint8_t level_of(uint32_t exp, uint16_t species, int party_level)
{
    if (party_level >= 1 && party_level <= 100)
        return (uint8_t)party_level;
    return pv_level_from_exp(exp, species_growth(species));
}

static void consider45(Dex *dex, int save_index, const uint8_t *raw, int len,
                       int party, uint16_t max_species, uint8_t box, uint8_t slot)
{
    uint8_t tmp[236];
    uint16_t species;
    uint32_t exp;
    uint32_t pid;
    uint8_t flags = 0;
    uint16_t moves[4];
    int party_level = 0;

    if (len > (int)sizeof tmp)
        return;
    memcpy(tmp, raw, (size_t)len);
    if (!pv_decrypt45(tmp, len))
        return;

    species = pv_read16(tmp + 0x08);
    if (species == 0 || species > max_species)
        return;

    exp = pv_read32(tmp + 0x10);
    pid = pv_read32(tmp);
    if (pv_is_shiny(pid, pv_read16(tmp + 0x0C), pv_read16(tmp + 0x0E)))
        flags |= MON_SHINY;
    if (((pv_read32(tmp + 0x38) >> 30) & 1u) != 0)
        flags |= MON_EGG;
    if (party) {
        flags |= MON_PARTY;
        party_level = tmp[0x8C];
    }
    /* Attack block sits after growth once pv_decrypt45 has unshuffled. */
    moves[0] = pv_read16(tmp + 0x28);
    moves[1] = pv_read16(tmp + 0x2A);
    moves[2] = pv_read16(tmp + 0x2C);
    moves[3] = pv_read16(tmp + 0x2E);
    add_mon(dex, save_index, species, level_of(exp, species, party_level), flags, box, slot, moves);
}

static void consider3(Dex *dex, int save_index, const uint8_t *raw, int len,
                      int party, uint8_t box, uint8_t slot)
{
    uint8_t tmp[100];
    uint16_t species;
    uint32_t exp;
    uint32_t pid;
    uint8_t flags = 0;
    uint16_t moves[4];
    int party_level = 0;

    if (len < 80 || len > (int)sizeof tmp)
        return;
    memcpy(tmp, raw, (size_t)len);
    if (!pv_decrypt3(tmp))
        return;

    species = pv_read16(tmp + 0x20);
    if (species == 0 || species > 386)
        return;

    exp = pv_read32(tmp + 0x24);
    pid = pv_read32(tmp);
    if (pv_is_shiny(pid, pv_read16(tmp + 4), pv_read16(tmp + 6)))
        flags |= MON_SHINY;
    if (((pv_read32(tmp + 0x48) >> 30) & 1u) != 0)
        flags |= MON_EGG;
    if (party && len >= 0x55) {
        flags |= MON_PARTY;
        party_level = tmp[0x54];
    }
    /* Attack substructure follows growth once pv_decrypt3 has unshuffled. */
    moves[0] = pv_read16(tmp + 0x2C);
    moves[1] = pv_read16(tmp + 0x2E);
    moves[2] = pv_read16(tmp + 0x30);
    moves[3] = pv_read16(tmp + 0x32);
    add_mon(dex, save_index, species, level_of(exp, species, party_level), flags, box, slot, moves);
}

static int newer_counter(uint32_t a, uint32_t b)
{
    if (a == 0xFFFFFFFFu && b != 0xFFFFFFFEu)
        return 1;
    if (b == 0xFFFFFFFFu && a != 0xFFFFFFFEu)
        return 0;
    if (a > b)
        return 0;
    if (a < b)
        return 1;
    return 0;
}

static int active_half(const uint8_t *data, size_t len, int begin, int length)
{
    size_t off;
    int ok0;
    int ok1;
    uint32_t major0;
    uint32_t major1;

    ok0 = (size_t)begin + (size_t)length <= len;
    ok1 = (size_t)begin + 0x40000u + (size_t)length <= len;
    if (!ok0 && !ok1)
        return -1;
    if (!ok1)
        return 0;
    if (!ok0)
        return 1;

    off = (size_t)begin + (size_t)length - 0x14u;
    major0 = pv_read32(data + off);
    major1 = pv_read32(data + off + 0x40000u);
    if (major0 != major1)
        return newer_counter(major0, major1);
    return newer_counter(pv_read32(data + off + 4), pv_read32(data + off + 4 + 0x40000u));
}

static bool gen4_block_marked(const uint8_t *block, int length)
{
    uint32_t size = pv_read32(block + length - 0x0C);
    uint32_t magic = pv_read32(block + length - 0x08);
    if (size != (uint32_t)length)
        return false;
    return magic == 0x20060623u || magic == 0x20070903u;
}

static bool gen4_is(const uint8_t *data, size_t len, int general_size)
{
    int half;
    for (half = 0; half < 2; half++) {
        size_t base = (size_t)half * 0x40000u;
        if (base + (size_t)general_size > len)
            continue;
        if (gen4_block_marked(data + base, general_size))
            return true;
    }
    return false;
}

static void read_gen4(Dex *dex, const char *name, const uint8_t *data, size_t len,
                      int general_size, int storage_start, int storage_size,
                      int party, int box_start, int box_stride, int box_count,
                      uint16_t max_species, const char *game)
{
    int save_index;
    int general_half;
    int storage_half;
    const uint8_t *general;
    const uint8_t *storage;
    int count;
    int slot;
    int box;

    general_half = active_half(data, len, 0, general_size);
    storage_half = active_half(data, len, storage_start, storage_size);
    if (general_half < 0 || storage_half < 0)
        return;

    general = data + (size_t)general_half * 0x40000u;
    storage = data + (size_t)storage_half * 0x40000u + (size_t)storage_start;
    save_index = begin_save(dex, name, game);
    if (save_index < 0)
        return;

    count = general[party - 4];
    if (count < 0)
        count = 0;
    if (count > 6)
        count = 6;
    for (slot = 0; slot < count; slot++) {
        consider45(dex, save_index, general + party + slot * 236, 236, 1,
                   max_species, 0xFF, (uint8_t)slot);
    }

    for (box = 0; box < box_count; box++) {
        for (slot = 0; slot < 30; slot++) {
            const uint8_t *pk = storage + box_start + box * box_stride + slot * 136;
            consider45(dex, save_index, pk, 136, 0, max_species, (uint8_t)box, (uint8_t)slot);
        }
    }
}

static bool gen5_footer_ok(const uint8_t *base, int main_size, int info_len)
{
    const uint8_t *footer = base + main_size - 0x100;
    uint16_t actual = pv_crc16_ccitt(footer, (size_t)info_len);
    uint16_t stored = pv_read16(footer + info_len + 0x0E);
    return actual == stored;
}

static bool gen5_party_ok(const uint8_t *base)
{
    uint16_t actual = pv_crc16_ccitt(base + 0x18E00, 0x534);
    return actual == pv_read16(base + 0x19336);
}

static int gen5_base(const uint8_t *data, size_t len, int main_size, int info_len)
{
    int best = -1;
    uint32_t best_count = 0;
    int i;

    for (i = 0; i < 2; i++) {
        int base = i * main_size;
        uint32_t count;
        if ((size_t)base + (size_t)main_size > len)
            continue;
        if (!gen5_footer_ok(data + base, main_size, info_len))
            continue;
        if (!gen5_party_ok(data + base))
            continue;
        count = pv_read32(data + base + main_size - 0x100 + info_len);
        if (best < 0 || count > best_count) {
            best = base;
            best_count = count;
        }
    }
    return best;
}

static void read_gen5(Dex *dex, const char *name, const uint8_t *data, int base,
                      uint16_t max_species, const char *game)
{
    const uint8_t *sav = data + base;
    int save_index = begin_save(dex, name, game);
    int count;
    int slot;
    int box;

    if (save_index < 0)
        return;

    count = sav[0x18E00 + 4];
    if (count < 0)
        count = 0;
    if (count > 6)
        count = 6;
    for (slot = 0; slot < count; slot++) {
        const uint8_t *pk = sav + 0x18E08 + slot * 220;
        consider45(dex, save_index, pk, 220, 1, max_species, 0xFF, (uint8_t)slot);
    }

    for (box = 0; box < 24; box++) {
        for (slot = 0; slot < 30; slot++) {
            const uint8_t *pk = sav + 0x400 + box * 0x1000 + slot * 136;
            consider45(dex, save_index, pk, 136, 0, max_species, (uint8_t)box, (uint8_t)slot);
        }
    }
}

enum {
    G3_SECTOR = 0x1000,
    G3_USED = 0xF80,
    G3_MAIN = 14 * G3_SECTOR
};

static int gen3_slot_score(const uint8_t *data, size_t len, int slot)
{
    int start = slot * G3_MAIN;
    int seen = 0;
    int good = 0;
    int ofs;

    if ((size_t)start + G3_MAIN > len)
        return -1;

    for (ofs = start; ofs < start + G3_MAIN; ofs += G3_SECTOR) {
        int id = (int)(int16_t)pv_read16(data + ofs + 0xFF4);
        uint16_t expect;
        uint16_t actual;
        if ((unsigned)id >= 14)
            return -1;
        if (seen & (1 << id))
            return -1;
        seen |= 1 << id;
        expect = pv_checksum32(data + ofs, G3_USED);
        actual = pv_read16(data + ofs + 0xFF6);
        if (expect == actual)
            good++;
    }
    if (seen != 0x3FFF)
        return -1;
    return good;
}

static uint32_t gen3_counter(const uint8_t *data, int slot)
{
    int start = slot * G3_MAIN;
    int ofs;
    for (ofs = start; ofs < start + G3_MAIN; ofs += G3_SECTOR) {
        if (pv_read16(data + ofs + 0xFF4) == 0)
            return pv_read32(data + ofs + 0xFFC);
    }
    return 0;
}

static void gen3_assemble(const uint8_t *data, int slot, uint8_t *small, uint8_t *large, uint8_t *storage)
{
    int start = slot * G3_MAIN;
    int ofs;
    memset(small, 0, G3_USED);
    memset(large, 0, 4 * G3_USED);
    memset(storage, 0, 9 * G3_USED);
    for (ofs = start; ofs < start + G3_MAIN; ofs += G3_SECTOR) {
        int id = (int)pv_read16(data + ofs + 0xFF4);
        uint8_t *dest;
        if (id >= 5)
            dest = storage + (id - 5) * G3_USED;
        else if (id >= 1)
            dest = large + (id - 1) * G3_USED;
        else
            dest = small;
        memcpy(dest, data + ofs, G3_USED);
    }
}

static const char *gen3_game(const uint8_t *small)
{
    uint32_t marker = pv_read32(small + 0xAC);
    int i;
    if (marker == 1)
        return "FR/LG";
    if (marker == 0)
        return "R/S";
    for (i = 0x890; i < 0xF2C; i++) {
        if (small[i] != 0)
            return "E";
    }
    return "R/S";
}

/* Main RAM, not the stack. The ARM9 user stack lives in 16 KB of DTCM,
   and these three blocks are about 55 KB together. */
static uint8_t g3_small[G3_USED];
static uint8_t g3_large[4 * G3_USED];
static uint8_t g3_storage[9 * G3_USED];

static void read_gen3(Dex *dex, const char *name, const uint8_t *data, size_t len)
{
    int score0 = gen3_slot_score(data, len, 0);
    int score1 = gen3_slot_score(data, len, 1);
    int slot;
    uint8_t *small = g3_small;
    uint8_t *large = g3_large;
    uint8_t *storage = g3_storage;
    int save_index;
    int party_count_ofs;
    int party_ofs;
    int count;
    int slot_i;
    int box;
    const char *game;

    if (score0 < 0 && score1 < 0)
        return;
    if (score1 < 0 || (score0 >= 0 && score0 > score1))
        slot = 0;
    else if (score0 < 0 || score1 > score0)
        slot = 1;
    else
        slot = newer_counter(gen3_counter(data, 0), gen3_counter(data, 1));

    gen3_assemble(data, slot, small, large, storage);
    game = gen3_game(small);
    save_index = begin_save(dex, name, game);
    if (save_index < 0)
        return;

    if (strcmp(game, "FR/LG") == 0) {
        party_count_ofs = 0x034;
        party_ofs = 0x038;
    } else {
        party_count_ofs = 0x234;
        party_ofs = 0x238;
    }

    count = large[party_count_ofs];
    if (count > 6)
        count = 6;
    for (slot_i = 0; slot_i < count; slot_i++) {
        consider3(dex, save_index, large + party_ofs + slot_i * 100, 100, 1,
                  0xFF, (uint8_t)slot_i);
    }

    for (box = 0; box < 14; box++) {
        for (slot_i = 0; slot_i < 30; slot_i++) {
            const uint8_t *pk = storage + 4 + (box * 30 + slot_i) * 80;
            consider3(dex, save_index, pk, 80, 0, (uint8_t)box, (uint8_t)slot_i);
        }
    }
}

static bool try_gen5(Dex *dex, const char *name, const uint8_t *data, size_t len)
{
    int b2 = -1;
    int bw = -1;
    if (len >= 0x26000)
        b2 = gen5_base(data, len, 0x26000, 0x94);
    if (b2 >= 0) {
        read_gen5(dex, name, data, b2, 649, "B2/W2");
        return true;
    }
    if (len >= 0x24000)
        bw = gen5_base(data, len, 0x24000, 0x8C);
    if (bw >= 0) {
        read_gen5(dex, name, data, bw, 649, "B/W");
        return true;
    }
    return false;
}

static bool try_gen4(Dex *dex, const char *name, const uint8_t *data, size_t len)
{
    if (len < 0x40000)
        return false;
    if (gen4_is(data, len, 0xF628)) {
        read_gen4(dex, name, data, len, 0xF628, 0xF700, 0x12310,
                  0x98, 0, 0x1000, 18, 493, "HG/SS");
        return true;
    }
    if (gen4_is(data, len, 0xCF2C)) {
        read_gen4(dex, name, data, len, 0xCF2C, 0xCF2C, 0x121E4,
                  0xA0, 4, 30 * 136, 18, 493, "Pt");
        return true;
    }
    if (gen4_is(data, len, 0xC100)) {
        read_gen4(dex, name, data, len, 0xC100, 0xC100, 0x121E0,
                  0x98, 4, 30 * 136, 18, 493, "D/P");
        return true;
    }
    return false;
}

bool save_read(Dex *dex, const char *display_name, const uint8_t *data, size_t len)
{
    size_t n = len;
    int before = dex->save_count;

    if (n > 0x80000 && n <= 0x80000 + 0x400)
        n = 0x80000;
    else if (n > 0x20000 && n <= 0x20000 + 0x100)
        n = 0x20000;

    if (n >= 0x24000 && n <= 0x80000) {
        if (try_gen5(dex, display_name, data, n))
            return dex->save_count > before;
        if (try_gen4(dex, display_name, data, n))
            return dex->save_count > before;
        return false;
    }

    if (n == 0x10000 || n == 0x20000 || (n >= 0xE000 && n < 0x40000)) {
        read_gen3(dex, display_name, data, n);
        return dex->save_count > before;
    }
    return false;
}
