#include "transfer.h"

#include "crypto.h"
#include "identity.h"
#include "species.h"
#include "stats.h"
#include "trade.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef HOST_TEST
#define xfer_pump() ((void)0)
#else
#include "music.h"
#define xfer_pump() music_pump()
#endif

enum {
    SAVE_CAP = 0x80000 + 0x400,
    G3_SECTOR = 0x1000,
    G3_USED = 0xF80,
    G3_MAIN = 14 * G3_SECTOR,
    PK_CAP = 236
};

static int fail_source_rename;

void transfer_fail_next_source_rename(void)
{
    fail_source_rename = 1;
}

const char *transfer_reason(int code)
{
    switch (code) {
    case XFER_BADGES: return "badges";
    case XFER_GENERATION: return "the generation";
    case XFER_ROOM: return "no room";
    case XFER_EGG: return "an egg";
    case XFER_MAIL: return "mail";
    case XFER_LAST: return "the last one";
    case XFER_ITEM: return "its item";
    default: return "";
    }
}

int transfer_evolves(uint16_t species, uint16_t item, int generation,
                     uint16_t partner, uint16_t *target)
{
    TradePath path;
    if (generation == 5 && species == 588 && partner == 616) {
        if (target)
            *target = 589;
        return 1;
    }
    if (generation == 5 && species == 616 && partner == 588) {
        if (target)
            *target = 617;
        return 1;
    }
    if (species == 588 || species == 616)
        return 0;
    if (!species_trade_ready_gen(species, item, generation, &path))
        return 0;
    if (target)
        *target = path.target;
    return 1;
}

static int name_has(const char *name, const char *needle)
{
    size_t n;
    size_t m;
    size_t i;
    if (!name || !needle)
        return 0;
    n = strlen(name);
    m = strlen(needle);
    if (m == 0 || n < m)
        return 0;
    for (i = 0; i + m <= n; i++) {
        if (memcmp(name + i, needle, m) == 0)
            return 1;
    }
    return 0;
}

static int item_is_mail(int generation, unsigned item)
{
    if (item == 0)
        return 0;
    return name_has(item_name((unsigned)generation, item), "Mail");
}

/* -1 when Generation 4 has no item by this name. 0 stays 0. */
static int item_to_gen4(unsigned item)
{
    const char *name;
    unsigned i;
    if (item == 0)
        return 0;
    name = item_name(3, item);
    if (!name[0])
        return -1;
    for (i = 1; i < 639; i++) {
        const char *other = item_name(4, i);
        if (other[0] && strcmp(other, name) == 0)
            return (int)i;
    }
    return -1;
}

static int gen_max(int gen)
{
    if (gen == 3)
        return 386;
    if (gen == 4)
        return 493;
    if (gen == 5)
        return 649;
    return 0;
}

static int gen_slots(int gen)
{
    if (gen == 3)
        return 14 * 30 + 6;
    if (gen == 4)
        return 18 * 30 + 6;
    if (gen == 5)
        return 24 * 30 + 6;
    return 0;
}

static int arrow_ok(int from, int to)
{
    if (from < 3 || from > 5 || to < 3 || to > 5)
        return 0;
    if (from == to)
        return 1;
    if (from == 3 && to == 4)
        return 1;
    if (from == 4 && to == 5)
        return 1;
    return 0;
}

static void today(uint8_t *year, uint8_t *month, uint8_t *day)
{
    time_t now = time(NULL);
    struct tm *tm;
    *year = 0;
    *month = 0;
    *day = 0;
    if (now == (time_t)-1)
        return;
    tm = localtime(&now);
    if (!tm)
        return;
    if (tm->tm_year >= 100 && tm->tm_year - 100 <= 255)
        *year = (uint8_t)(tm->tm_year - 100);
    *month = (uint8_t)(tm->tm_mon + 1);
    *day = (uint8_t)tm->tm_mday;
}

static int fold_same(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
    }
    return *a == 0 && *b == 0;
}

static char g3_latin(unsigned char c)
{
    if (c == 0x00)
        return ' ';
    if (c >= 0xA1 && c <= 0xAA)
        return (char)('0' + (c - 0xA1));
    if (c >= 0xBB && c <= 0xD4)
        return (char)('A' + (c - 0xBB));
    if (c >= 0xD5 && c <= 0xEE)
        return (char)('a' + (c - 0xD5));
    return 0;
}

static void g3_ascii(const uint8_t *src, int n, char *out, int cap)
{
    int i;
    int w = 0;
    if (cap <= 0)
        return;
    for (i = 0; i < n && w + 1 < cap; i++) {
        char c;
        if (src[i] == 0xFF)
            break;
        c = g3_latin(src[i]);
        if (c == 0 || c == ' ')
            continue;
        out[w++] = c;
    }
    out[w] = 0;
}

static char g4_letter(unsigned u)
{
    if (u == 0x001 || u == 0x1DE)
        return ' ';
    if (u >= 0x121 && u <= 0x12A)
        return (char)('0' + (u - 0x121));
    if (u >= 0x12B && u <= 0x144)
        return (char)('A' + (u - 0x12B));
    if (u >= 0x145 && u <= 0x15E)
        return (char)('a' + (u - 0x145));
    return 0;
}

static void put_g4(uint8_t *dst, int chars, const char *ascii)
{
    int i = 0;
    int o = 0;
    memset(dst, 0xFF, (size_t)chars * 2);
    if (!ascii)
        return;
    while (ascii[i] && o < chars) {
        unsigned code = 0;
        char c = ascii[i++];
        if (c == ' ')
            code = 0x1DE;
        else if (c >= '0' && c <= '9')
            code = 0x121u + (unsigned)(c - '0');
        else if (c >= 'A' && c <= 'Z')
            code = 0x12Bu + (unsigned)(c - 'A');
        else if (c >= 'a' && c <= 'z')
            code = 0x145u + (unsigned)(c - 'a');
        else
            continue;
        dst[o * 2] = (uint8_t)code;
        dst[o * 2 + 1] = (uint8_t)(code >> 8);
        o++;
    }
    if (o < chars) {
        dst[o * 2] = 0xFF;
        dst[o * 2 + 1] = 0xFF;
    }
}

static void g4_to_utf16(const uint8_t *src, int bytes, uint8_t *dst, int dst_bytes)
{
    int i = 0;
    int o = 0;
    memset(dst, 0, (size_t)dst_bytes);
    while (i + 1 < bytes && o + 1 < dst_bytes) {
        unsigned u = (unsigned)src[i] | ((unsigned)src[i + 1] << 8);
        char c;
        i += 2;
        if (u == 0 || u == 0xFFFF)
            break;
        c = g4_letter(u);
        if (c == 0 || c == ' ')
            continue;
        dst[o] = (uint8_t)c;
        dst[o + 1] = 0;
        o += 2;
    }
    if (o + 1 < dst_bytes) {
        dst[o] = 0xFF;
        dst[o + 1] = 0xFF;
    }
}

static int slot_len(int gen, int party)
{
    if (gen == 3)
        return party ? 100 : 80;
    if (gen == 4)
        return party ? 236 : 136;
    if (gen == 5)
        return party ? 220 : 136;
    return 0;
}

static int box_count(int gen)
{
    if (gen == 3)
        return 14;
    if (gen == 4)
        return 18;
    if (gen == 5)
        return 24;
    return 0;
}

typedef struct {
    int general_size;
    int storage_start;
    int storage_size;
    int party;
    int box_start;
    int box_stride;
    int dex_ofs;
    int footer;
} Gen4Map;

static const Gen4Map gen4_maps[] = {
    { 0xF628, 0xF700, 0x12310, 0x98, 0, 0x1000, 0x12B8, 0x10 },
    { 0xCF2C, 0xCF2C, 0x121E4, 0xA0, 4, 30 * 136, 0x1328, 0x14 },
    { 0xC100, 0xC100, 0x121E0, 0x98, 4, 30 * 136, 0x12DC, 0x14 }
};

typedef struct {
    uint8_t *file;
    size_t len;
    int gen;
    int base;
    int main_size;
    int info_len;
    int b2;
    int g3_slot;
    int g4_which;
    int general_half;
    int storage_half;
    int party_touch;
    int storage_touch;
    int dex_touch;
    int box_touch[24];
    int g3_touch[8];
    int g3_n;
} Loc;

static int newer_count(uint32_t a, uint32_t b)
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

static int gen4_marked(const uint8_t *block, int length)
{
    uint32_t magic;
    if (pv_read32(block + length - 0x0C) != (uint32_t)length)
        return 0;
    magic = pv_read32(block + length - 0x08);
    return magic == 0x20060623u || magic == 0x20070903u;
}

static int gen4_which(const uint8_t *data, size_t len)
{
    int i;
    for (i = 0; i < (int)(sizeof gen4_maps / sizeof gen4_maps[0]); i++) {
        int half;
        int size = gen4_maps[i].general_size;
        for (half = 0; half < 2; half++) {
            size_t base = (size_t)half * 0x40000u;
            if (base + (size_t)size > len)
                continue;
            if (gen4_marked(data + base, size))
                return i;
        }
    }
    return -1;
}

static int gen4_half(const uint8_t *data, size_t len, int begin, int length)
{
    size_t off;
    int ok0;
    int ok1;
    ok0 = (size_t)begin + (size_t)length <= len;
    ok1 = (size_t)begin + 0x40000u + (size_t)length <= len;
    if (!ok0 && !ok1)
        return -1;
    if (!ok1)
        return 0;
    if (!ok0)
        return 1;
    off = (size_t)begin + (size_t)length - 0x14u;
    if (pv_read32(data + off) != pv_read32(data + off + 0x40000u))
        return newer_count(pv_read32(data + off), pv_read32(data + off + 0x40000u));
    return newer_count(pv_read32(data + off + 4), pv_read32(data + off + 4 + 0x40000u));
}

static int g3_slot_score(const uint8_t *data, size_t len, int slot)
{
    int start = slot * G3_MAIN;
    int seen = 0;
    int good = 0;
    int ofs;
    if ((size_t)start + G3_MAIN > len)
        return -1;
    for (ofs = start; ofs < start + G3_MAIN; ofs += G3_SECTOR) {
        int id = (int)(int16_t)pv_read16(data + ofs + 0xFF4);
        if ((unsigned)id >= 14)
            return -1;
        if (seen & (1 << id))
            return -1;
        seen |= 1 << id;
        if (pv_checksum32(data + ofs, G3_USED) == pv_read16(data + ofs + 0xFF6))
            good++;
    }
    if (seen != 0x3FFF)
        return -1;
    return good;
}

static uint32_t g3_counter(const uint8_t *data, int slot)
{
    int start = slot * G3_MAIN;
    int ofs;
    for (ofs = start; ofs < start + G3_MAIN; ofs += G3_SECTOR) {
        if (pv_read16(data + ofs + 0xFF4) == 0)
            return pv_read32(data + ofs + 0xFFC);
    }
    return 0;
}

static int g3_active(const uint8_t *data, size_t len)
{
    int score0 = g3_slot_score(data, len, 0);
    int score1 = g3_slot_score(data, len, 1);
    if (score0 < 0 && score1 < 0)
        return -1;
    if (score1 < 0 || (score0 >= 0 && score0 > score1))
        return 0;
    if (score0 < 0 || score1 > score0)
        return 1;
    return newer_count(g3_counter(data, 0), g3_counter(data, 1));
}

static int loc_open(Loc *loc, uint8_t *file, size_t len)
{
    int base;
    int which;
    memset(loc, 0, sizeof *loc);
    loc->file = file;
    loc->len = len;
    if (!file)
        return 0;
    base = -1;
    if (len >= 0x26000)
        base = half_base(file, len, 0x26000, 0x94);
    if (base >= 0) {
        loc->gen = 5;
        loc->base = base;
        loc->main_size = 0x26000;
        loc->info_len = 0x94;
        loc->b2 = 1;
        return 1;
    }
    if (len >= 0x24000)
        base = half_base(file, len, 0x24000, 0x8C);
    if (base >= 0) {
        loc->gen = 5;
        loc->base = base;
        loc->main_size = 0x24000;
        loc->info_len = 0x8C;
        return 1;
    }
    which = gen4_which(file, len);
    if (which >= 0) {
        const Gen4Map *map = &gen4_maps[which];
        int general = gen4_half(file, len, 0, map->general_size);
        int storage = gen4_half(file, len, map->storage_start, map->storage_size);
        if (general < 0 || storage < 0)
            return 0;
        loc->gen = 4;
        loc->g4_which = which;
        loc->general_half = general;
        loc->storage_half = storage;
        return 1;
    }
    base = g3_active(file, len);
    if (base < 0)
        return 0;
    loc->gen = 3;
    loc->g3_slot = base;
    return 1;
}

static uint8_t *g3_sector(Loc *loc, int id)
{
    size_t start = (size_t)loc->g3_slot * (size_t)G3_MAIN;
    int i;
    if (start + (size_t)G3_MAIN > loc->len)
        return NULL;
    for (i = 0; i < 14; i++) {
        uint8_t *sec = loc->file + start + (size_t)i * G3_SECTOR;
        if ((int)(int16_t)pv_read16(sec + 0xFF4) == id)
            return sec;
    }
    return NULL;
}

static void g3_remember(Loc *loc, int id)
{
    int i;
    if (!loc || loc->gen != 3)
        return;
    for (i = 0; i < loc->g3_n; i++) {
        if (loc->g3_touch[i] == id)
            return;
    }
    if (loc->g3_n < 8)
        loc->g3_touch[loc->g3_n++] = id;
}

static int g3_span(Loc *loc, int first, int logical, uint8_t *buf, int n, int store)
{
    int left = n;
    int at = logical;
    while (left > 0) {
        int id = first + at / G3_USED;
        int off = at % G3_USED;
        int chunk = G3_USED - off;
        uint8_t *sec;
        if (id < 0 || id > 13)
            return 0;
        if (chunk > left)
            chunk = left;
        sec = g3_sector(loc, id);
        if (!sec)
            return 0;
        if (store)
            memcpy(sec + off, buf + (n - left), (size_t)chunk);
        else
            memcpy(buf + (n - left), sec + off, (size_t)chunk);
        g3_remember(loc, id);
        at += chunk;
        left -= chunk;
    }
    return 1;
}

static uint8_t *g4_general(Loc *loc)
{
    return loc->file + (size_t)loc->general_half * 0x40000u;
}

static uint8_t *g4_storage(Loc *loc)
{
    const Gen4Map *map = &gen4_maps[loc->g4_which];
    return loc->file + (size_t)loc->storage_half * 0x40000u + (size_t)map->storage_start;
}

static int g3_frlg(Loc *loc)
{
    uint8_t *small = g3_sector(loc, 0);
    if (!small)
        return 0;
    return pv_read32(small + 0xAC) == 1;
}

static int g3_party_at(Loc *loc)
{
    return g3_frlg(loc) ? 0x034 : 0x234;
}

static int bit_count8(unsigned value)
{
    int n = 0;
    int i;
    for (i = 0; i < 8; i++)
        n += (int)((value >> i) & 1u);
    return n;
}

static int g3_flag(Loc *loc, int base, int flag)
{
    int ofs = base + (flag >> 3);
    uint8_t byte = 0;
    if (!g3_span(loc, 1, ofs, &byte, 1, 0))
        return 0;
    return (byte >> (flag & 7)) & 1;
}

static int loc_badges(Loc *loc)
{
    if (loc->gen == 5) {
        int money = loc->b2 ? 0x21100 : 0x21200;
        uint8_t *sav = loc->file + loc->base;
        if (money + 5 > loc->main_size)
            return 0;
        return bit_count8(sav[money + 4]);
    }
    if (loc->gen == 4) {
        const Gen4Map *map = &gen4_maps[loc->g4_which];
        int trainer = loc->g4_which == 1 ? 0x68 : 0x64;
        uint8_t *general = g4_general(loc);
        if (trainer + 0x1A >= map->general_size)
            return 0;
        return bit_count8(general[trainer + 0x1A]);
    }
    if (loc->gen == 3) {
        int base;
        int first;
        int n = 0;
        int i;
        uint8_t *small = g3_sector(loc, 0);
        if (!small)
            return 0;
        if (pv_read32(small + 0xAC) == 1) {
            base = 0xEE0;
            first = 0x820;
        } else {
            int emerald = 0;
            for (i = 0x890; i < 0xF2C; i++) {
                if (small[i]) {
                    emerald = 1;
                    break;
                }
            }
            if (emerald) {
                base = 0x1270;
                first = 0x867;
            } else {
                base = 0x1220;
                first = 0x807;
            }
        }
        for (i = 0; i < 8; i++)
            n += g3_flag(loc, base, first + i);
        return n;
    }
    return 0;
}

static int range_ok(int gen, int party, int box, int slot)
{
    if (party)
        return slot >= 0 && slot <= 5;
    if (box < 0 || slot < 0 || slot > 29)
        return 0;
    return box < box_count(gen);
}

static uint8_t *slot_ptr(Loc *loc, int party, int box, int slot)
{
    if (loc->gen == 5) {
        uint8_t *sav = loc->file + loc->base;
        if (party)
            return sav + 0x18E08 + slot * 220;
        return sav + 0x400 + box * 0x1000 + slot * 136;
    }
    if (loc->gen == 4) {
        const Gen4Map *map = &gen4_maps[loc->g4_which];
        if (party)
            return g4_general(loc) + map->party + slot * 236;
        return g4_storage(loc) + map->box_start + box * map->box_stride + slot * 136;
    }
    return NULL;
}

static int read_slot(Loc *loc, int party, int box, int slot, uint8_t *out)
{
    int n = slot_len(loc->gen, party);
    if (!range_ok(loc->gen, party, box, slot) || n <= 0 || n > PK_CAP)
        return 0;
    if (loc->gen == 3) {
        int logical;
        if (party)
            logical = g3_party_at(loc) + 4 + slot * 100;
        else
            logical = 4 + (box * 30 + slot) * 80;
        return g3_span(loc, party ? 1 : 5, logical, out, n, 0);
    }
    memcpy(out, slot_ptr(loc, party, box, slot), (size_t)n);
    return 1;
}

static int write_slot(Loc *loc, int party, int box, int slot, const uint8_t *in)
{
    int n = slot_len(loc->gen, party);
    if (!range_ok(loc->gen, party, box, slot) || n <= 0)
        return 0;
    if (loc->gen == 3) {
        int logical;
        if (party)
            logical = g3_party_at(loc) + 4 + slot * 100;
        else
            logical = 4 + (box * 30 + slot) * 80;
        if (!g3_span(loc, party ? 1 : 5, logical, (uint8_t *)in, n, 1))
            return 0;
    } else {
        memcpy(slot_ptr(loc, party, box, slot), in, (size_t)n);
    }
    if (party)
        loc->party_touch = 1;
    else if (loc->gen == 5 && box >= 0 && box < 24)
        loc->box_touch[box] = 1;
    else
        loc->storage_touch = 1;
    return 1;
}

static int party_count(Loc *loc)
{
    uint8_t n = 0;
    if (loc->gen == 5)
        return loc->file[loc->base + 0x18E04];
    if (loc->gen == 4) {
        const Gen4Map *map = &gen4_maps[loc->g4_which];
        return g4_general(loc)[map->party - 4];
    }
    if (!g3_span(loc, 1, g3_party_at(loc), &n, 1, 0))
        return 0;
    return n;
}

static void set_party_count(Loc *loc, int count)
{
    uint8_t n = (uint8_t)count;
    if (count < 0)
        count = 0;
    if (count > 6)
        count = 6;
    n = (uint8_t)count;
    if (loc->gen == 5)
        loc->file[loc->base + 0x18E04] = n;
    else if (loc->gen == 4) {
        const Gen4Map *map = &gen4_maps[loc->g4_which];
        g4_general(loc)[map->party - 4] = n;
    } else {
        g3_span(loc, 1, g3_party_at(loc), &n, 1, 1);
    }
    loc->party_touch = 1;
}

static int decrypt_slot(Loc *loc, const uint8_t *raw, int n)
{
    if (loc->gen == 3)
        return pv_decrypt3((uint8_t *)raw) ? 1 : 0;
    return pv_decrypt45((uint8_t *)raw, n) ? 1 : 0;
}

static uint16_t species_of(Loc *loc, const uint8_t *pk)
{
    if (loc->gen == 3)
        return species_from_gen3(pv_read16(pk + 0x20));
    return pv_read16(pk + 8);
}

static int egg_of(Loc *loc, const uint8_t *pk)
{
    uint32_t ivs = loc->gen == 3 ? pv_read32(pk + 0x48) : pv_read32(pk + 0x38);
    return ((ivs >> 30) & 1u) != 0;
}

static uint16_t item_of(Loc *loc, const uint8_t *pk)
{
    if (loc->gen == 3)
        return pv_read16(pk + 0x22);
    return pv_read16(pk + 0x0A);
}

static int occupied(Loc *loc, int party, int box, int slot)
{
    uint8_t raw[PK_CAP];
    uint8_t tmp[PK_CAP];
    int n = slot_len(loc->gen, party);
    uint16_t species;
    if (!read_slot(loc, party, box, slot, raw))
        return 0;
    memcpy(tmp, raw, (size_t)n);
    if (!decrypt_slot(loc, tmp, n))
        return 0;
    species = species_of(loc, tmp);
    return species != 0;
}

static int count_mons(Loc *loc)
{
    int n = 0;
    int count;
    int i;
    int box;
    int slot;
    count = party_count(loc);
    if (count < 0)
        count = 0;
    if (count > 6)
        count = 6;
    for (i = 0; i < count; i++)
        n += occupied(loc, 1, 0, i);
    for (box = 0; box < box_count(loc->gen); box++) {
        for (slot = 0; slot < 30; slot++)
            n += occupied(loc, 0, box, slot);
    }
    return n;
}

static int find_room(Loc *loc, int *party, int *box, int *slot)
{
    int b;
    int s;
    int count;
    for (b = 0; b < box_count(loc->gen); b++) {
        for (s = 0; s < 30; s++) {
            if (!occupied(loc, 0, b, s)) {
                *party = 0;
                *box = b;
                *slot = s;
                return 1;
            }
        }
    }
    count = party_count(loc);
    if (count < 0)
        count = 0;
    if (count > 6)
        count = 6;
    if (count < 6) {
        *party = 1;
        *box = 0;
        *slot = count;
        return 1;
    }
    return 0;
}

static void compact_party(Loc *loc, int removed)
{
    uint8_t kept[6][PK_CAP];
    int count = party_count(loc);
    int n = 0;
    int i;
    int size;
    if (count < 0)
        count = 0;
    if (count > 6)
        count = 6;
    size = slot_len(loc->gen, 1);
    for (i = 0; i < count; i++) {
        if (i == removed)
            continue;
        if (!read_slot(loc, 1, 0, i, kept[n]))
            memset(kept[n], 0, (size_t)size);
        n++;
    }
    for (i = 0; i < n; i++)
        write_slot(loc, 1, 0, i, kept[i]);
    memset(kept[0], 0, (size_t)size);
    for (i = n; i < count; i++)
        write_slot(loc, 1, 0, i, kept[0]);
    set_party_count(loc, n);
}

static void clear_slot(Loc *loc, int party, int box, int slot)
{
    uint8_t zeros[PK_CAP];
    memset(zeros, 0, sizeof zeros);
    if (party)
        compact_party(loc, slot);
    else
        write_slot(loc, 0, box, slot, zeros);
}

static void set_dex_bit(uint8_t *region, int species)
{
    int bit = species - 1;
    if (species < 1 || species > NATIONAL_DEX)
        return;
    region[bit >> 3] |= (uint8_t)(1u << (bit & 7));
}

static int dex_bit(const uint8_t *region, int species)
{
    int bit = species - 1;
    return (region[bit >> 3] >> (bit & 7)) & 1;
}

static int gen5_on_display(const uint8_t *dex, int species)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (dex_bit(dex + 0x1AC + i * 0x54, species))
            return 1;
    }
    return 0;
}

static void set_gen5_language(uint8_t *dex, int species, int language, int b2)
{
    int index;
    int bit;
    int lang_ofs;
    if (species < 1 || species > 493)
        return;
    if (language >= 1 && language <= 5)
        index = language - 1;
    else if (language == 7)
        index = 5;
    else if (language == 8)
        index = 6;
    else
        index = 1;
    lang_ofs = b2 ? 0x328 : 0x320;
    bit = (species - 1) * 7 + index;
    dex[lang_ofs + (bit >> 3)] |= (uint8_t)(1u << (bit & 7));
}

static void mark_dex(Loc *loc, const uint8_t *pk, uint16_t species)
{
    if (species < 1 || species > gen_max(loc->gen))
        return;
    if (loc->gen == 3) {
        uint8_t *small = g3_sector(loc, 0);
        if (!small)
            return;
        set_dex_bit(small + 0x28, species);
        set_dex_bit(small + 0x5C, species);
        g3_remember(loc, 0);
        loc->dex_touch = 1;
        return;
    }
    if (loc->gen == 4) {
        const Gen4Map *map = &gen4_maps[loc->g4_which];
        uint8_t *general = g4_general(loc);
        set_dex_bit(general + map->dex_ofs + 4, species);
        set_dex_bit(general + map->dex_ofs + 4 + 0x40, species);
        loc->dex_touch = 1;
        return;
    }
    {
        uint8_t *sav = loc->file + loc->base;
        int dex_at = loc->b2 ? 0x21400 : 0x21600;
        int gender = (pk[0x40] >> 1) & 3;
        int region;
        if (gender > 1)
            gender = 0;
        region = gender;
        if (pv_is_shiny(pv_read32(pk), pv_read16(pk + 0x0C), pv_read16(pk + 0x0E)))
            region += 2;
        set_dex_bit(sav + dex_at + 0x08, species);
        set_dex_bit(sav + dex_at + 0x5C + region * 0x54, species);
        if (!gen5_on_display(sav + dex_at, species))
            set_dex_bit(sav + dex_at + 0x1AC + region * 0x54, species);
        set_gen5_language(sav + dex_at, species, pk[0x17], loc->b2);
        loc->dex_touch = 1;
    }
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

static void seal_gen4(uint8_t *block, int size, int footer)
{
    uint32_t count = pv_read32(block + size - 0x14);
    uint16_t crc;
    if (count < 0xFFFFFFFFu)
        pv_write32(block + size - 0x14, count + 1);
    crc = pv_crc16_ccitt(block, (size_t)(size - footer));
    pv_write16(block + size - 2, crc);
}

static void seal_g3(uint8_t *sector)
{
    pv_write16(sector + 0xFF6, pv_checksum32(sector, G3_USED));
}

static void bump_g3(Loc *loc)
{
    uint8_t *sec0 = g3_sector(loc, 0);
    uint32_t count;
    int i;
    uint8_t *first;
    if (!sec0)
        return;
    count = pv_read32(sec0 + 0xFFC);
    if (count < 0xFFFFFFFFu)
        count++;
    first = loc->file + (size_t)loc->g3_slot * (size_t)G3_MAIN;
    for (i = 0; i < 14; i++)
        pv_write32(first + (size_t)i * G3_SECTOR + 0xFFC, count);
}

static void seal_loc(Loc *loc)
{
    int i;
    if (loc->gen == 5) {
        uint8_t *sav = loc->file + loc->base;
        int footer = loc->main_size - 0x100;
        int dex_at = loc->b2 ? 0x21400 : 0x21600;
        int dex_len = loc->b2 ? 0x4DC : 0x4D4;
        int any = loc->party_touch || loc->dex_touch;
        for (i = 0; i < 24; i++) {
            if (!loc->box_touch[i])
                continue;
            seal_span(sav, 0x400 + i * 0x1000, 0xFF0, footer + 2 + i * 2);
            any = 1;
        }
        if (loc->party_touch)
            seal_span(sav, 0x18E00, 0x534, footer + 0x34);
        if (loc->dex_touch)
            seal_span(sav, dex_at, dex_len, footer + (loc->b2 ? 0x6C : 0x6E));
        if (any)
            seal_footer(sav, loc->main_size, loc->info_len);
        return;
    }
    if (loc->gen == 4) {
        const Gen4Map *map = &gen4_maps[loc->g4_which];
        if (loc->storage_touch || loc->box_touch[0])
            seal_gen4(g4_storage(loc), map->storage_size, map->footer);
        if (loc->party_touch || loc->dex_touch)
            seal_gen4(g4_general(loc), map->general_size, map->footer);
        return;
    }
    if (loc->g3_n > 0) {
        for (i = 0; i < loc->g3_n; i++) {
            uint8_t *sec = g3_sector(loc, loc->g3_touch[i]);
            if (sec)
                seal_g3(sec);
        }
        bump_g3(loc);
    }
}

static void write_party45(uint8_t *pk, int gen5)
{
    MonRef view;
    uint16_t stats[6];
    uint32_t pid = pv_read32(pk);
    int old_max = (int)pv_read16(pk + 0x90);
    int cur = (int)pv_read16(pk + 0x8E);
    int new_max;
    int new_cur;
    int i;
    memset(&view, 0, sizeof view);
    view.species = pv_read16(pk + 8);
    view.ivs = pv_read32(pk + 0x38);
    view.nature = (uint8_t)(pid % 25u);
    if (gen5 && pk[0x41] < 25)
        view.nature = pk[0x41];
    view.form = (uint8_t)(pk[0x40] >> 3);
    view.level = pv_level_from_exp(pv_read32(pk + 0x10), species_growth(view.species));
    for (i = 0; i < 6; i++)
        view.evs[i] = pk[0x18 + i];
    mon_battle_stats(&view, stats);
    new_max = stats[0];
    if (old_max <= 0 && cur <= 0)
        new_cur = new_max;
    else if (cur <= 0)
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

static void write_party3(uint8_t *pk)
{
    MonRef view;
    uint16_t stats[6];
    uint16_t species = species_from_gen3(pv_read16(pk + 0x20));
    int old_max = (int)pv_read16(pk + 0x58);
    int cur = (int)pv_read16(pk + 0x56);
    int new_max;
    int new_cur;
    int i;
    memset(&view, 0, sizeof view);
    view.species = species;
    view.ivs = pv_read32(pk + 0x48);
    view.nature = (uint8_t)(pv_read32(pk) % 25u);
    view.level = pv_level_from_exp(pv_read32(pk + 0x24), species_growth(species));
    for (i = 0; i < 6; i++)
        view.evs[i] = pk[0x38 + i];
    mon_battle_stats(&view, stats);
    new_max = stats[0];
    if (old_max <= 0 && cur <= 0)
        new_cur = new_max;
    else if (cur <= 0)
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
    pk[0x54] = view.level;
    pv_write16(pk + 0x56, (uint16_t)new_cur);
    pv_write16(pk + 0x58, stats[0]);
    pv_write16(pk + 0x5A, stats[1]);
    pv_write16(pk + 0x5C, stats[2]);
    pv_write16(pk + 0x5E, stats[5]);
    pv_write16(pk + 0x60, stats[3]);
    pv_write16(pk + 0x62, stats[4]);
}

static uint8_t link_ability(uint16_t species, uint8_t current)
{
    if (species == 588) {
        if (current == 61)
            return 75;
        if (current == 99)
            return 142;
        return 68;
    }
    if (species == 616) {
        if (current == 75)
            return 60;
        if (current == 142)
            return 84;
        return 93;
    }
    return current;
}

static void apply_evolution(uint8_t *pk, int gen, uint16_t from, uint16_t held,
                            uint16_t target, int link)
{
    if (gen == 3) {
        uint16_t stored = species_to_gen3(target);
        TradePath path;
        if (stored == 0)
            return;
        pv_write16(pk + 0x20, stored);
        pk[0x29] = species_base_friendship(target);
        if (!link && species_trade_ready_gen(from, held, 3, &path) && path.item != 0)
            pv_write16(pk + 0x22, 0);
        return;
    }
    {
        uint8_t next;
        TradePath path;
        if (link)
            next = link_ability(from, pk[0x15]);
        else
            next = trade_mapped_ability(from, held, gen, pk[0x15]);
        pv_write16(pk + 8, target);
        pk[0x14] = species_base_friendship(target);
        if (next)
            pk[0x15] = next;
        if (!link && species_trade_ready_gen(from, held, gen, &path) && path.item != 0)
            pv_write16(pk + 0x0A, 0);
    }
}

static void finish_record(uint8_t *pk, int gen, int n, int party)
{
    if (party && gen == 3 && n >= 100)
        write_party3(pk);
    else if (party && gen != 3 && n > 136)
        write_party45(pk, gen == 5);
    if (gen == 3) {
        pv_refresh_checksum3(pk);
        pv_encrypt3(pk);
    } else {
        pv_refresh_checksum45(pk);
        pv_encrypt45(pk, n);
    }
}

static int hgss_loc(Loc *loc)
{
    return loc->gen == 4 && loc->g4_which == 0;
}

static void pal_park(uint8_t *dst, int dst_n, const uint8_t *src, Loc *dest)
{
    uint32_t pid = pv_read32(src);
    uint32_t ivs = pv_read32(src + 0x48);
    uint16_t origins = pv_read16(src + 0x46);
    uint16_t species = species_from_gen3(pv_read16(src + 0x20));
    uint16_t item = pv_read16(src + 0x22);
    int mapped = item_to_gen4(item);
    uint8_t level = pv_level_from_exp(pv_read32(src + 0x24), species_growth(species));
    uint8_t ball = (uint8_t)((origins >> 11) & 0xF);
    uint8_t gender = species_gender(species, pid);
    uint8_t form = 0;
    uint8_t year, month, day;
    char nick[12];
    char ot[12];
    int nicknamed;
    memset(dst, 0, (size_t)dst_n);
    if (species == 201)
        form = (uint8_t)(pid % 28u);
    if (ball < 1 || ball > 12)
        ball = 4;
    if (mapped < 0)
        mapped = 0;
    g3_ascii(src + 0x08, 10, nick, (int)sizeof nick);
    g3_ascii(src + 0x14, 7, ot, (int)sizeof ot);
    nicknamed = nick[0] && !fold_same(nick, species_name(species));
    if (nicknamed)
        ivs |= 1u << 31;
    else
        ivs &= ~(1u << 31);
    ivs &= ~(1u << 30);
    today(&year, &month, &day);
    pv_write32(dst, pid);
    pv_write16(dst + 8, species);
    pv_write16(dst + 0x0A, (uint16_t)mapped);
    pv_write16(dst + 0x0C, pv_read16(src + 4));
    pv_write16(dst + 0x0E, pv_read16(src + 6));
    pv_write32(dst + 0x10, pv_read32(src + 0x24));
    dst[0x14] = species_base_friendship(species);
    dst[0x15] = species_ability(species, (src[0x13] & 1) != 0);
    dst[0x16] = src[0x1B];
    dst[0x17] = src[0x12];
    memcpy(dst + 0x18, src + 0x38, 6);
    memcpy(dst + 0x1E, src + 0x3E, 6);
    memcpy(dst + 0x28, src + 0x2C, 8);
    memcpy(dst + 0x30, src + 0x34, 4);
    dst[0x34] = (uint8_t)(src[0x28] & 3);
    dst[0x35] = (uint8_t)((src[0x28] >> 2) & 3);
    dst[0x36] = (uint8_t)((src[0x28] >> 4) & 3);
    dst[0x37] = (uint8_t)((src[0x28] >> 6) & 3);
    pv_write32(dst + 0x38, ivs);
    pv_write32(dst + 0x3C, pv_read32(src + 0x4C));
    dst[0x40] = (uint8_t)((gender << 1) | (form << 3));
    put_g4(dst + 0x48, 11, nicknamed ? nick : species_name(species));
    dst[0x5F] = (uint8_t)((origins >> 7) & 0xF);
    put_g4(dst + 0x68, 8, ot);
    dst[0x7B] = year;
    dst[0x7C] = month;
    dst[0x7D] = day;
    if (hgss_loc(dest))
        pv_write16(dst + 0x46, 436);
    else
        pv_write16(dst + 0x80, 55);
    dst[0x83] = ball;
    if (hgss_loc(dest))
        dst[0x86] = ball;
    dst[0x84] = (uint8_t)((level & 0x7F) | ((origins & 0x8000) ? 0x80 : 0));
}

static void poke_transfer(uint8_t *dst, int dst_n, const uint8_t *src)
{
    uint32_t pid = pv_read32(src);
    uint16_t species = pv_read16(src + 8);
    uint8_t level = pv_level_from_exp(pv_read32(src + 0x10), species_growth(species));
    uint8_t year, month, day;
    uint8_t nick[22];
    uint8_t ot[16];
    int n = dst_n < 136 ? dst_n : 136;
    memcpy(nick, src + 0x48, 22);
    memcpy(ot, src + 0x68, 16);
    memset(dst, 0, (size_t)dst_n);
    memcpy(dst, src, (size_t)n);
    pv_write16(dst + 0x0A, 0);
    dst[0x14] = species_base_friendship(species);
    dst[0x41] = (uint8_t)(pid % 25u);
    pv_write16(dst + 0x46, 0);
    g4_to_utf16(nick, 22, dst + 0x48, 22);
    g4_to_utf16(ot, 16, dst + 0x68, 16);
    today(&year, &month, &day);
    dst[0x7B] = year;
    dst[0x7C] = month;
    dst[0x7D] = day;
    pv_write16(dst + 0x80, 30001);
    dst[0x84] = (uint8_t)((level & 0x7F) | (dst[0x84] & 0x80));
}

static uint16_t met_of(const uint8_t *pk, int gen)
{
    uint8_t version;
    uint16_t extended;
    uint16_t plain;
    if (gen == 3)
        return pk[0x45];
    version = pk[0x5F];
    extended = pv_read16(pk + 0x46);
    plain = pv_read16(pk + 0x80);
    if (version >= 20 && version <= 23)
        return plain;
    if (extended)
        return extended;
    return plain;
}

static uint8_t ball_of(const uint8_t *pk, int gen)
{
    if (gen == 3)
        return (uint8_t)((pv_read16(pk + 0x46) >> 11) & 0xF);
    if ((pk[0x5F] == 7 || pk[0x5F] == 8) && pk[0x86] != 0)
        return pk[0x86];
    return pk[0x83];
}

static void fill_land(TransferLand *land, const uint8_t *pk, int gen, uint16_t species,
                      int party, int box, int slot)
{
    if (!land)
        return;
    memset(land, 0, sizeof *land);
    land->species = species;
    if (gen == 3) {
        uint16_t item = pv_read16(pk + 0x22);
        land->item = item;
        land->item_gen = item ? 3 : 0;
        land->ability = species_ability(species, (pk[0x13] & 1) != 0);
        land->form = 0;
    } else {
        uint16_t item = pv_read16(pk + 0x0A);
        land->item = item;
        land->item_gen = item ? 4 : 0;
        land->ability = pk[0x15];
        land->form = (uint8_t)(pk[0x40] >> 3);
        land->met_year = pk[0x7B];
        land->met_month = pk[0x7C];
        land->met_day = pk[0x7D];
    }
    land->met_loc = met_of(pk, gen);
    land->ball = ball_of(pk, gen);
    land->party = party ? 1 : 0;
    land->box = party ? 0xFF : (uint8_t)box;
    land->slot = (uint8_t)slot;
}

static int prepare(Loc *loc, int party, int box, int slot, uint8_t *plain, int *n_out,
                   uint16_t *species, int *egg, uint16_t *item)
{
    uint8_t raw[PK_CAP];
    int n = slot_len(loc->gen, party);
    if (!read_slot(loc, party, box, slot, raw))
        return 0;
    memcpy(plain, raw, (size_t)n);
    if (!decrypt_slot(loc, plain, n))
        return 0;
    *n_out = n;
    *species = species_of(loc, plain);
    *egg = egg_of(loc, plain);
    *item = item_of(loc, plain);
    return *species != 0;
}

static int screen(Loc *src, Loc *dst, uint16_t species, int egg, uint16_t item, int swapping)
{
    int from = src->gen;
    int to = dst->gen;
    if (loc_badges(src) < 8 || loc_badges(dst) < 8)
        return XFER_BADGES;
    if (!arrow_ok(from, to))
        return XFER_GENERATION;
    if (species < 1 || species > gen_max(to))
        return XFER_GENERATION;
    if (egg && from != to)
        return XFER_EGG;
    if (item && item_is_mail(from == 3 ? 3 : 4, item))
        return XFER_MAIL;
    if (!swapping && count_mons(src) <= 1)
        return XFER_LAST;
    if (from == 3 && to == 4 && item && item_to_gen4(item) < 0)
        return XFER_ITEM;
    return XFER_OK;
}

int transfer_apply(uint8_t *src_file, size_t src_len, uint8_t *dst_file, size_t dst_len,
                   int party, int box, int slot, uint16_t expect,
                   int partner_party, int partner_box, int partner_slot,
                   TransferLand *land)
{
    Loc src;
    Loc dst;
    uint8_t plain[PK_CAP];
    uint8_t other[PK_CAP];
    uint8_t built[PK_CAP];
    int n = 0;
    int other_n = 0;
    uint16_t species = 0;
    uint16_t other_species = 0;
    uint16_t item = 0;
    uint16_t other_item = 0;
    int egg = 0;
    int other_egg = 0;
    int swapping;
    int rc;
    int dest_party = 0;
    int dest_box = 0;
    int dest_slot = 0;
    uint16_t target = 0;

    if (!loc_open(&src, src_file, src_len) || !loc_open(&dst, dst_file, dst_len))
        return XFER_BAD;
    if (!range_ok(src.gen, party, box, slot))
        return XFER_BAD;
    swapping = partner_slot >= 0;
    if (!prepare(&src, party, box, slot, plain, &n, &species, &egg, &item))
        return XFER_BAD;
    if (species != expect)
        return XFER_NO;
    if (swapping) {
        if (!range_ok(dst.gen, partner_party, partner_box, partner_slot))
            return XFER_NO;
        if (!prepare(&dst, partner_party, partner_box, partner_slot, other, &other_n,
                     &other_species, &other_egg, &other_item))
            return XFER_NO;
        if (other_egg)
            return XFER_EGG;
        if (!((species == 588 && other_species == 616) || (species == 616 && other_species == 588)))
            return XFER_NO;
        if (src.gen != dst.gen || src.gen != 5)
            return XFER_GENERATION;
    }
    rc = screen(&src, &dst, species, egg, item, swapping);
    if (rc != XFER_OK)
        return rc;
    if (swapping && other_item && item_is_mail(4, other_item))
        return XFER_MAIL;
    if (!swapping && !find_room(&dst, &dest_party, &dest_box, &dest_slot))
        return XFER_ROOM;

    if (swapping) {
        uint16_t to_a = 0;
        uint16_t to_b = 0;
        transfer_evolves(species, item, 5, other_species, &to_a);
        transfer_evolves(other_species, other_item, 5, species, &to_b);
        apply_evolution(plain, 5, species, item, to_a, 1);
        apply_evolution(other, 5, other_species, other_item, to_b, 1);
        /* Each takes the other's slot, so the stored size follows that slot. */
        if (partner_party && other_n < slot_len(5, 1)) {
            memset(plain + other_n, 0, (size_t)(slot_len(5, 1) - other_n));
            other_n = slot_len(5, 1);
        }
        if (party && n < slot_len(5, 1)) {
            memset(other + n, 0, (size_t)(slot_len(5, 1) - n));
            n = slot_len(5, 1);
        }
        finish_record(plain, 5, slot_len(5, partner_party), partner_party);
        finish_record(other, 5, slot_len(5, party), party);
        if (!write_slot(&dst, partner_party, partner_box, partner_slot, plain))
            return XFER_BAD;
        if (!write_slot(&src, party, box, slot, other))
            return XFER_BAD;
        /* Dex marks need the decrypted record. Decrypt the copies we kept
           by rebuilding from the pre-encrypt buffers is already done; the
           buffers are encrypted now, so mark from the species alone and a
           stand-in gender byte. Re-read after write. */
        {
            uint8_t back[PK_CAP];
            int bn = 0;
            uint16_t sp = 0;
            int eg = 0;
            uint16_t it = 0;
            if (!prepare(&dst, partner_party, partner_box, partner_slot, back, &bn, &sp, &eg, &it))
                return XFER_BAD;
            mark_dex(&dst, back, sp);
            fill_land(land, back, 5, sp, partner_party, partner_box, partner_slot);
            if (!prepare(&src, party, box, slot, back, &bn, &sp, &eg, &it))
                return XFER_BAD;
            mark_dex(&src, back, sp);
        }
        seal_loc(&src);
        seal_loc(&dst);
        return XFER_OK;
    }

    memset(built, 0, sizeof built);
    if (src.gen == dst.gen) {
        int dest_n = slot_len(dst.gen, dest_party);
        memcpy(built, plain, (size_t)n);
        if (n < dest_n)
            memset(built + n, 0, (size_t)(dest_n - n));
        if (!egg && transfer_evolves(species, item, src.gen, 0, &target))
            apply_evolution(built, src.gen, species, item, target, 0);
        else
            target = species;
        finish_record(built, dst.gen, dest_n, dest_party);
        if (!write_slot(&dst, dest_party, dest_box, dest_slot, built))
            return XFER_BAD;
    } else if (src.gen == 3 && dst.gen == 4) {
        int dest_n = slot_len(4, dest_party);
        pal_park(built, dest_n, plain, &dst);
        target = species;
        finish_record(built, 4, dest_n, dest_party);
        if (!write_slot(&dst, dest_party, dest_box, dest_slot, built))
            return XFER_BAD;
    } else if (src.gen == 4 && dst.gen == 5) {
        int dest_n = slot_len(5, dest_party);
        poke_transfer(built, dest_n, plain);
        target = species;
        finish_record(built, 5, dest_n, dest_party);
        if (!write_slot(&dst, dest_party, dest_box, dest_slot, built))
            return XFER_BAD;
    } else {
        return XFER_GENERATION;
    }

    {
        uint8_t back[PK_CAP];
        int bn = 0;
        uint16_t sp = 0;
        int eg = 0;
        uint16_t it = 0;
        if (!prepare(&dst, dest_party, dest_box, dest_slot, back, &bn, &sp, &eg, &it))
            return XFER_BAD;
        mark_dex(&dst, back, sp);
        fill_land(land, back, dst.gen, sp, dest_party, dest_box, dest_slot);
    }
    clear_slot(&src, party, box, slot);
    if (dest_party)
        set_party_count(&dst, party_count(&dst) + 1);
    seal_loc(&src);
    seal_loc(&dst);
    (void)target;
    return XFER_OK;
}

int transfer_gate(const Dex *dex, const MonRef *mon, int dest)
{
    const SaveInfo *src;
    const SaveInfo *dst;
    int from;
    int to;
    int item_gen;
    if (!dex || !mon || mon->save_index >= dex->save_count || dest < 0 || dest >= dex->save_count)
        return XFER_NO;
    if (dest == mon->save_index)
        return XFER_NO;
    src = &dex->saves[mon->save_index];
    dst = &dex->saves[dest];
    from = game_generation(src->game);
    to = game_generation(dst->game);
    if (src->badges < 8 || dst->badges < 8)
        return XFER_BADGES;
    if (!arrow_ok(from, to) || mon->species < 1 || mon->species > gen_max(to))
        return XFER_GENERATION;
    if ((mon->flags & MON_EGG) && from != to)
        return XFER_EGG;
    item_gen = mon->item_gen == 3 ? 3 : 4;
    if (mon->item && item_is_mail(item_gen, mon->item))
        return XFER_MAIL;
    if (src->count <= 1)
        return XFER_LAST;
    if (dst->count >= gen_slots(to))
        return XFER_ROOM;
    if (from == 3 && to == 4 && mon->item && item_to_gen4(mon->item) < 0)
        return XFER_ITEM;
    return XFER_OK;
}

static int stream_copy(const char *from, const char *to)
{
    FILE *in;
    FILE *out;
    uint8_t buf[4096];
    size_t n;
    in = fopen(from, "rb");
    if (!in)
        return 0;
    out = fopen(to, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            remove(to);
            return 0;
        }
        xfer_pump();
    }
    fclose(in);
    if (fclose(out) != 0) {
        remove(to);
        return 0;
    }
    return 1;
}

static int write_file(const char *path, const uint8_t *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    size_t off = 0;
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
        xfer_pump();
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

static int read_all(const char *path, uint8_t *buf, size_t n)
{
    FILE *f = fopen(path, "rb");
    size_t off = 0;
    if (!f)
        return 0;
    while (off < n) {
        size_t got = fread(buf + off, 1, n - off, f);
        if (got == 0)
            break;
        off += got;
        xfer_pump();
    }
    fclose(f);
    return off == n;
}

static void mark_save(uint8_t *bits, int species)
{
    int bit;
    if (species < 1 || species > NATIONAL_DEX)
        return;
    bit = species - 1;
    bits[bit >> 3] |= (uint8_t)(1u << (bit & 7));
}

int transfer_commit(Dex *dex, MonRef *mon, int dest)
{
    SaveInfo *src;
    SaveInfo *dst;
    TransferLand land;
    struct stat st_src;
    struct stat st_dst;
    uint8_t *src_buf = NULL;
    uint8_t *dst_buf = NULL;
    char src_bak[256];
    char dst_bak[256];
    char src_tmp[256];
    char dst_tmp[256];
    int party;
    int rc;
    int hooked;

    if (!dex || !mon)
        return XFER_NO;
    rc = transfer_gate(dex, mon, dest);
    if (rc != XFER_OK)
        return rc;
    src = &dex->saves[mon->save_index];
    dst = &dex->saves[dest];
    if (!src->path[0] || !dst->path[0])
        return XFER_IO;
    if (stat(src->path, &st_src) != 0 || stat(dst->path, &st_dst) != 0)
        return XFER_IO;
    if (st_src.st_size <= 0 || st_dst.st_size <= 0)
        return XFER_IO;
    if ((size_t)st_src.st_size > SAVE_CAP || (size_t)st_dst.st_size > SAVE_CAP)
        return XFER_IO;
    if (!side_path(src_bak, sizeof src_bak, src->path, ".bak")
        || !side_path(dst_bak, sizeof dst_bak, dst->path, ".bak")
        || !side_path(src_tmp, sizeof src_tmp, src->path, ".tmp")
        || !side_path(dst_tmp, sizeof dst_tmp, dst->path, ".tmp"))
        return XFER_IO;

    /* One working buffer per save. The previous bytes live in the .bak. */
    if (!stream_copy(src->path, src_bak) || !stream_copy(dst->path, dst_bak))
        return XFER_IO;
    src_buf = malloc((size_t)st_src.st_size);
    dst_buf = malloc((size_t)st_dst.st_size);
    if (!src_buf || !dst_buf || !read_all(src->path, src_buf, (size_t)st_src.st_size)
        || !read_all(dst->path, dst_buf, (size_t)st_dst.st_size)) {
        free(src_buf);
        free(dst_buf);
        return XFER_IO;
    }
    party = (mon->flags & MON_PARTY) != 0;
    rc = transfer_apply(src_buf, (size_t)st_src.st_size, dst_buf, (size_t)st_dst.st_size,
                        party, party ? 0 : mon->box, mon->slot, mon->species,
                        0, 0, -1, &land);
    if (rc != XFER_OK) {
        free(src_buf);
        free(dst_buf);
        return rc;
    }
    if (!write_file(dst_tmp, dst_buf, (size_t)st_dst.st_size)
        || !write_file(src_tmp, src_buf, (size_t)st_src.st_size)) {
        remove(dst_tmp);
        remove(src_tmp);
        free(src_buf);
        free(dst_buf);
        return XFER_IO;
    }
    free(src_buf);
    free(dst_buf);

    remove(dst->path);
    if (rename(dst_tmp, dst->path) != 0) {
        stream_copy(dst_bak, dst->path);
        remove(dst_tmp);
        remove(src_tmp);
        return XFER_IO;
    }
    hooked = fail_source_rename;
    fail_source_rename = 0;
    remove(src->path);
    if (hooked || rename(src_tmp, src->path) != 0) {
        stream_copy(dst_bak, dst->path);
        stream_copy(src_bak, src->path);
        remove(src_tmp);
        remove(dst_tmp);
        return XFER_IO;
    }

    if (src->count > 0)
        src->count--;
    if (dst->count < 65535)
        dst->count++;
    mon->species = land.species;
    mon->item = land.item;
    mon->item_gen = land.item_gen;
    mon->ability = land.ability;
    mon->form = land.form;
    mon->ball = land.ball;
    mon->met_loc = land.met_loc;
    mon->met_year = land.met_year;
    mon->met_month = land.met_month;
    mon->met_day = land.met_day;
    mon->save_index = (uint8_t)dest;
    mon->box = land.box;
    mon->slot = land.slot;
    if (land.party)
        mon->flags = (uint8_t)(mon->flags | MON_PARTY);
    else
        mon->flags = (uint8_t)(mon->flags & (uint8_t)~MON_PARTY);
    mark_save(dst->dex_caught, land.species);
    mark_save(dst->dex_seen, land.species);
    return XFER_OK;
}
