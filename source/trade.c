#include "trade.h"

#include "crypto.h"
#include "species.h"
#include "stats.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef HOST_TEST
#define trade_pump() ((void)0)
#else
#include "music.h"
#define trade_pump() music_pump()
#endif

/* Abilities are the Generation 5 personal slots: first, second, hidden.
   Later generations renamed some of these, so the live ability tables are
   not a safe source. Friendship is the species' base. A round trip sets it
   on the way out, when the other trainer is not the OT, and leaves it on
   the way back. */
typedef struct {
    uint16_t from;
    uint16_t to;
    uint16_t item;
    uint8_t base_friend;
    uint8_t from_ab[3];
    uint8_t to_ab[3];
} TradeDef;

enum {
    ITEM_KINGS_ROCK = 221,
    ITEM_DEEP_SEA_TOOTH = 226,
    ITEM_DEEP_SEA_SCALE = 227,
    ITEM_METAL_COAT = 233,
    ITEM_DRAGON_SCALE = 235,
    ITEM_UPGRADE = 252,
    ITEM_PROTECTOR = 321,
    ITEM_ELECTIRIZER = 322,
    ITEM_MAGMARIZER = 323,
    ITEM_DUBIOUS_DISC = 324,
    ITEM_REAPER_CLOTH = 325,
    ITEM_PRISM_SCALE = 537
};

#define AB(a, b, c) { (uint8_t)(a), (uint8_t)(b), (uint8_t)(c) }

static const TradeDef trades[] = {
    { 64, 65, 0, 70, AB(28, 39, 98), AB(28, 39, 98) },
    { 67, 68, 0, 70, AB(62, 99, 80), AB(62, 99, 80) },
    { 75, 76, 0, 70, AB(69, 5, 8), AB(69, 5, 8) },
    { 93, 94, 0, 70, AB(26, 26, 26), AB(26, 26, 26) },
    { 525, 526, 0, 70, AB(5, 5, 159), AB(5, 5, 159) },
    { 533, 534, 0, 70, AB(62, 125, 89), AB(62, 125, 89) },
    { 588, 589, 0, 70, AB(68, 61, 99), AB(68, 75, 142) },
    { 616, 617, 0, 70, AB(93, 75, 142), AB(93, 60, 84) },
    { 61, 186, ITEM_KINGS_ROCK, 70, AB(11, 6, 33), AB(11, 6, 2) },
    { 79, 199, ITEM_KINGS_ROCK, 70, AB(12, 20, 144), AB(12, 20, 144) },
    { 95, 208, ITEM_METAL_COAT, 70, AB(69, 5, 133), AB(69, 5, 125) },
    { 112, 464, ITEM_PROTECTOR, 70, AB(31, 69, 120), AB(31, 116, 120) },
    { 117, 230, ITEM_DRAGON_SCALE, 70, AB(38, 97, 6), AB(33, 97, 6) },
    { 123, 212, ITEM_METAL_COAT, 70, AB(68, 101, 80), AB(68, 101, 135) },
    { 125, 466, ITEM_ELECTIRIZER, 70, AB(9, 9, 72), AB(78, 78, 72) },
    { 126, 467, ITEM_MAGMARIZER, 70, AB(49, 49, 72), AB(49, 49, 72) },
    { 137, 233, ITEM_UPGRADE, 70, AB(36, 88, 148), AB(36, 88, 148) },
    { 233, 474, ITEM_DUBIOUS_DISC, 70, AB(36, 88, 148), AB(91, 88, 148) },
    { 349, 350, ITEM_PRISM_SCALE, 70, AB(33, 33, 91), AB(63, 63, 56) },
    { 356, 477, ITEM_REAPER_CLOTH, 35, AB(46, 46, 46), AB(46, 46, 46) },
    { 366, 367, ITEM_DEEP_SEA_TOOTH, 70, AB(75, 75, 155), AB(33, 33, 41) },
    { 366, 368, ITEM_DEEP_SEA_SCALE, 70, AB(75, 75, 155), AB(33, 33, 93) }
};

#undef AB

static const TradeDef *find_trade(unsigned species, unsigned held, unsigned target)
{
    const TradeDef *found = NULL;
    int i;
    for (i = 0; i < (int)(sizeof trades / sizeof trades[0]); i++) {
        const TradeDef *row = &trades[i];
        if (row->from != species)
            continue;
        if (target && row->to != target)
            continue;
        if (row->item != 0 && row->item != held)
            continue;
        if (found)
            return NULL;
        found = row;
    }
    return found;
}

int species_trade_paths(unsigned species, TradePath *out, int cap)
{
    int n = 0;
    int i;
    if (!out || cap < 1)
        return 0;
    for (i = 0; i < (int)(sizeof trades / sizeof trades[0]); i++) {
        if (trades[i].from != species)
            continue;
        if (n >= cap)
            break;
        out[n].target = trades[i].to;
        out[n].item = trades[i].item;
        n++;
    }
    return n;
}

int species_trade_ready(unsigned species, unsigned held, TradePath *out)
{
    const TradeDef *row = find_trade(species, held, 0);
    if (!row || !out)
        return 0;
    out->target = row->to;
    out->item = row->item;
    return 1;
}

static uint8_t mapped_ability(uint8_t current, const TradeDef *row)
{
    int i;
    for (i = 0; i < 3; i++) {
        if (row->from_ab[i] && current == row->from_ab[i])
            return row->to_ab[i] ? row->to_ab[i] : row->to_ab[0];
    }
    return row->to_ab[0];
}

static void fill_view(const uint8_t *pk, uint16_t species, MonRef *mon)
{
    uint32_t pid = pv_read32(pk);
    int i;
    memset(mon, 0, sizeof *mon);
    mon->species = species;
    mon->ivs = pv_read32(pk + 0x38);
    mon->nature = (uint8_t)(pid % 25u);
    if (pk[0x41] < 25)
        mon->nature = pk[0x41];
    mon->form = (uint8_t)(pk[0x40] >> 3);
    mon->level = pv_level_from_exp(pv_read32(pk + 0x10), species_growth(species));
    for (i = 0; i < 6; i++)
        mon->evs[i] = pk[0x18 + i];
}

/* Party stats live in the plain tail. Current HP keeps its share of the
   old maximum, and a full or empty bar stays full or empty. */
static void write_party_stats(uint8_t *pk)
{
    MonRef view;
    uint16_t stats[6];
    int old_max;
    int cur;
    int new_max;
    int new_cur;

    old_max = (int)pv_read16(pk + 0x90);
    cur = (int)pv_read16(pk + 0x8E);
    fill_view(pk, pv_read16(pk + 8), &view);
    mon_battle_stats(&view, stats);
    new_max = stats[0];
    if (cur <= 0)
        new_cur = 0;
    else if (old_max <= 0 || cur >= old_max)
        new_cur = new_max;
    else {
        new_cur = (int)((unsigned)cur * (unsigned)new_max / (unsigned)old_max);
        if (new_cur < 1)
            new_cur = 1;
    }
    if (new_cur > new_max)
        new_cur = new_max;
    pk[0x8C] = view.level;
    pv_write16(pk + 0x8E, (uint16_t)new_cur);
    pv_write16(pk + 0x90, stats[0]);
    pv_write16(pk + 0x92, stats[1]);
    pv_write16(pk + 0x94, stats[2]);
    pv_write16(pk + 0x96, stats[5]);
    pv_write16(pk + 0x98, stats[3]);
    pv_write16(pk + 0x9A, stats[4]);
}

static void set_dex_bit(uint8_t *region, int species)
{
    int bit = species - 1;
    region[bit >> 3] |= (uint8_t)(1u << (bit & 7));
}

static int half_base(const uint8_t *data, size_t len, int main_size, int info_len)
{
    int best = -1;
    uint32_t best_count = 0;
    int i;

    for (i = 0; i < 2; i++) {
        int base = i * main_size;
        const uint8_t *footer;
        uint16_t actual;
        uint32_t count;
        if ((size_t)base + (size_t)main_size > len)
            continue;
        footer = data + base + main_size - 0x100;
        actual = pv_crc16_ccitt(footer, (size_t)info_len);
        if (actual != pv_read16(footer + info_len + 0x0E))
            continue;
        actual = pv_crc16_ccitt(data + base + 0x18E00, 0x534);
        if (actual != pv_read16(data + base + 0x19336))
            continue;
        count = pv_read32(footer + info_len);
        if (best < 0 || count > best_count) {
            best = base;
            best_count = count;
        }
    }
    return best;
}

static int active_half(const uint8_t *data, size_t len, int *main_size, int *info_len)
{
    int base = -1;
    if (len >= 0x26000)
        base = half_base(data, len, 0x26000, 0x94);
    if (base >= 0) {
        *main_size = 0x26000;
        *info_len = 0x94;
        return base;
    }
    if (len >= 0x24000)
        base = half_base(data, len, 0x24000, 0x8C);
    if (base >= 0) {
        *main_size = 0x24000;
        *info_len = 0x8C;
        return base;
    }
    return -1;
}

static void seal_span(uint8_t *sav, int start, int length, int mirror)
{
    uint16_t crc = pv_crc16_ccitt(sav + start, (size_t)length);
    pv_write16(sav + start + length + 2, crc);
    pv_write16(sav + mirror, crc);
}

static void seal_footer(uint8_t *sav, int main_size, int info_len)
{
    uint8_t *footer = sav + main_size - 0x100;
    uint32_t count = pv_read32(footer + info_len);
    uint16_t crc;
    if (count < 0xFFFFFFFFu)
        count++;
    pv_write32(footer + info_len, count);
    crc = pv_crc16_ccitt(footer, (size_t)info_len);
    pv_write16(footer + info_len + 0x0E, crc);
}

int trade_apply(uint8_t *file, size_t len, int party, int box, int slot,
                uint16_t expect_species, uint16_t target, uint8_t *ability_out)
{
    int main_size = 0;
    int info_len = 0;
    int base;
    int pk_len;
    int b2;
    int dex_at;
    int dex_len;
    int footer;
    int gender;
    int region;
    uint8_t pk[220];
    uint8_t *sav;
    uint8_t *raw;
    uint16_t species;
    uint16_t held;
    uint32_t ivs;
    const TradeDef *row;

    if (!file || expect_species == 0 || target == 0)
        return TRADE_NO;
    base = active_half(file, len, &main_size, &info_len);
    if (base < 0)
        return TRADE_BAD;
    if (party) {
        if (slot < 0 || slot > 5)
            return TRADE_MISMATCH;
        raw = file + base + 0x18E08 + slot * 220;
        pk_len = 220;
    } else {
        if (box < 0 || box > 23 || slot < 0 || slot > 29)
            return TRADE_MISMATCH;
        raw = file + base + 0x400 + box * 0x1000 + slot * 136;
        pk_len = 136;
    }
    memcpy(pk, raw, (size_t)pk_len);
    if (!pv_decrypt45(pk, pk_len))
        return TRADE_BAD;
    species = pv_read16(pk + 8);
    if (species != expect_species)
        return TRADE_MISMATCH;
    ivs = pv_read32(pk + 0x38);
    if (((ivs >> 30) & 1u) != 0)
        return TRADE_EGG;
    held = pv_read16(pk + 0x0A);
    row = find_trade(species, held, target);
    if (!row)
        return TRADE_NO;

    pv_write16(pk + 8, row->to);
    pk[0x14] = row->base_friend;
    pk[0x15] = mapped_ability(pk[0x15], row);
    if (row->item != 0)
        pv_write16(pk + 0x0A, 0);
    if (party)
        write_party_stats(pk);
    /* The seen row is male, female, shiny male, shiny female.
       A genderless Pokémon shares the male row. Read it before the
       record is shuffled back to disk. */
    gender = (pk[0x40] >> 1) & 3;
    if (gender > 1)
        gender = 0;
    region = gender;
    if (pv_is_shiny(pv_read32(pk), pv_read16(pk + 0x0C), pv_read16(pk + 0x0E)))
        region += 2;
    if (ability_out)
        *ability_out = pk[0x15];
    pv_refresh_checksum45(pk);
    pv_encrypt45(pk, pk_len);
    memcpy(raw, pk, (size_t)pk_len);

    sav = file + base;
    b2 = main_size == 0x26000;
    dex_at = b2 ? 0x21400 : 0x21600;
    dex_len = b2 ? 0x4DC : 0x4D4;
    footer = main_size - 0x100;
    set_dex_bit(sav + dex_at + 0x08, row->to);
    set_dex_bit(sav + dex_at + 0x5C + region * 0x54, row->to);
    if (party)
        seal_span(sav, 0x18E00, 0x534, footer + 0x34);
    else
        seal_span(sav, 0x400 + box * 0x1000, 0xFF0, footer + 2 + box * 2);
    seal_span(sav, dex_at, dex_len, footer + (b2 ? 0x6C : 0x6E));
    seal_footer(sav, main_size, info_len);
    return TRADE_OK;
}

enum { SAVE_CAP = 0x80000 + 0x400 };

static int write_file(const char *path, const uint8_t *data, size_t n)
{
    FILE *f;
    size_t off = 0;
    f = fopen(path, "wb");
    if (!f)
        return 0;
    while (off < n) {
        size_t chunk = n - off;
        size_t got;
        if (chunk > 16384)
            chunk = 16384;
        got = fwrite(data + off, 1, chunk, f);
        if (got != chunk) {
            fclose(f);
            remove(path);
            return 0;
        }
        off += got;
        trade_pump();
    }
    if (fclose(f) != 0) {
        remove(path);
        return 0;
    }
    return 1;
}

static int side_path(char *dst, size_t cap, const char *path, const char *suffix)
{
    int wrote = snprintf(dst, cap, "%s%s", path, suffix);
    return wrote > 0 && (size_t)wrote < cap;
}

static void mark_save_bit(uint8_t *bits, int species)
{
    int bit;
    if (species < 1 || species > NATIONAL_DEX)
        return;
    bit = species - 1;
    bits[bit >> 3] |= (uint8_t)(1u << (bit & 7));
}

int trade_commit(Dex *dex, MonRef *mon, uint16_t target)
{
    SaveInfo *info;
    TradePath path;
    struct stat st;
    uint8_t *buf = NULL;
    uint8_t *orig = NULL;
    uint8_t ability = 0;
    char bak[256];
    char tmp[256];
    FILE *f;
    size_t n = 0;
    int party;
    int rc = TRADE_IO;

    if (!dex || !mon || mon->save_index >= dex->save_count)
        return TRADE_NO;
    info = &dex->saves[mon->save_index];
    if (!info->path[0] || !species_trade_ready(mon->species, mon->item, &path)
        || path.target != target)
        return TRADE_NO;
    if ((mon->flags & MON_EGG) != 0)
        return TRADE_EGG;
    if (stat(info->path, &st) != 0 || st.st_size <= 0 || (size_t)st.st_size > SAVE_CAP)
        return TRADE_IO;
    if (!side_path(bak, sizeof bak, info->path, ".bak")
        || !side_path(tmp, sizeof tmp, info->path, ".tmp"))
        return TRADE_IO;

    buf = malloc((size_t)st.st_size);
    orig = malloc((size_t)st.st_size);
    if (!buf || !orig)
        goto done;
    f = fopen(info->path, "rb");
    if (!f)
        goto done;
    while (n < (size_t)st.st_size) {
        size_t got = fread(buf + n, 1, (size_t)st.st_size - n, f);
        if (got == 0)
            break;
        n += got;
        trade_pump();
    }
    fclose(f);
    if (n != (size_t)st.st_size)
        goto done;
    memcpy(orig, buf, n);

    party = (mon->flags & MON_PARTY) != 0;
    rc = trade_apply(buf, n, party, party ? 0 : mon->box, mon->slot,
                     mon->species, target, &ability);
    if (rc != TRADE_OK)
        goto done;

    /* The .bak is the pre-trade save. It is replaced only after the new
       bytes are already sitting in the temp file. */
    rc = TRADE_IO;
    if (!write_file(tmp, buf, n))
        goto done;
    if (!write_file(bak, orig, n)) {
        remove(tmp);
        goto done;
    }
    remove(info->path);
    if (rename(tmp, info->path) != 0) {
        /* The save name is gone. Put the pre-trade bytes back under it. */
        (void)write_file(info->path, orig, n);
        remove(tmp);
        goto done;
    }

    mon->species = target;
    mon->ability = ability;
    if (path.item != 0) {
        mon->item = 0;
        mon->item_gen = 0;
    }
    mark_save_bit(info->dex_caught, target);
    mark_save_bit(info->dex_seen, target);
    rc = TRADE_OK;

done:
    free(buf);
    free(orig);
    return rc;
}
