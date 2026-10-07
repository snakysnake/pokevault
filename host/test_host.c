#include "crypto.h"
#include "moves.h"
#include "flavor.h"
#include "save.h"
#include "species.h"
#include "stats.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            printf("fail %s:%d  %s\n", __FILE__, __LINE__, #cond);                  \
            fails++;                                                                \
        }                                                                           \
    } while (0)

static void fill_pk45_moves(uint8_t *pk, int len, uint32_t pid, uint16_t species, uint32_t exp,
                            uint16_t tid, uint16_t sid, int egg, int party_level,
                            const uint16_t moves[4])
{
    memset(pk, 0, (size_t)len);
    pv_write32(pk, pid);
    pv_write16(pk + 8, species);
    pv_write16(pk + 0x0C, tid);
    pv_write16(pk + 0x0E, sid);
    pv_write32(pk + 0x10, exp);
    if (moves) {
        pv_write16(pk + 0x28, moves[0]);
        pv_write16(pk + 0x2A, moves[1]);
        pv_write16(pk + 0x2C, moves[2]);
        pv_write16(pk + 0x2E, moves[3]);
    }
    if (egg)
        pv_write32(pk + 0x38, 1u << 30);
    if (len > 0x8C)
        pk[0x8C] = (uint8_t)party_level;
    pv_refresh_checksum45(pk);
    pv_encrypt45(pk, len);
}

static void fill_pk45(uint8_t *pk, int len, uint32_t pid, uint16_t species, uint32_t exp,
                      uint16_t tid, uint16_t sid, int egg, int party_level)
{
    fill_pk45_moves(pk, len, pid, species, exp, tid, sid, egg, party_level, NULL);
}

static void fill_pk3_moves(uint8_t *pk, int len, uint32_t pid, uint16_t species, uint32_t exp,
                           uint16_t tid, uint16_t sid, int party_level, const uint16_t moves[4])
{
    memset(pk, 0, (size_t)len);
    pv_write32(pk, pid);
    pv_write16(pk + 4, tid);
    pv_write16(pk + 6, sid);
    pv_write16(pk + 0x20, species);
    pv_write32(pk + 0x24, exp);
    if (moves) {
        pv_write16(pk + 0x2C, moves[0]);
        pv_write16(pk + 0x2E, moves[1]);
        pv_write16(pk + 0x30, moves[2]);
        pv_write16(pk + 0x32, moves[3]);
    }
    if (len > 0x54)
        pk[0x54] = (uint8_t)party_level;
    pv_refresh_checksum3(pk);
    pv_encrypt3(pk);
}

static void fill_pk3(uint8_t *pk, int len, uint32_t pid, uint16_t species, uint32_t exp,
                     uint16_t tid, uint16_t sid, int party_level)
{
    fill_pk3_moves(pk, len, pid, species, exp, tid, sid, party_level, NULL);
}

static void mark_gen4(uint8_t *data, int general_size, int storage_start, int storage_size)
{
    pv_write32(data + general_size - 0x0C, (uint32_t)general_size);
    pv_write32(data + general_size - 0x08, 0x20060623u);
    pv_write32(data + general_size - 0x14, 4);
    pv_write32(data + storage_start + storage_size - 0x14, 4);
}

static void seal_gen5(uint8_t *base, int main_size, int info_len, int mirror, uint32_t counter)
{
    uint16_t party = pv_crc16_ccitt(base + 0x18E00, 0x534);
    uint8_t *footer = base + main_size - 0x100;
    uint16_t foot;
    pv_write16(base + 0x19336, party);
    pv_write16(base + mirror, party);
    foot = pv_crc16_ccitt(footer, (size_t)info_len);
    pv_write16(footer + info_len + 0x0E, foot);
    pv_write32(footer + info_len, counter);
}

static const MonRef *find_species(const Dex *dex, uint16_t species)
{
    int i;
    for (i = 0; i < dex->mon_count; i++) {
        if (dex->mons[i].species == species)
            return &dex->mons[i];
    }
    return NULL;
}

static int save_bit(const uint8_t *bits, int species)
{
    int bit = species - 1;
    return (bits[bit >> 3] >> (bit & 7)) & 1;
}

static void set_save_bit(uint8_t *region, int species)
{
    int bit = species - 1;
    region[bit >> 3] |= (uint8_t)(1u << (bit & 7));
}

static void test_exp(void)
{
    CHECK(pv_level_from_exp(0, 0) == 1);
    CHECK(pv_level_from_exp(8, 0) == 2);
    CHECK(pv_level_from_exp(999, 0) == 9);
    CHECK(pv_level_from_exp(1000, 0) == 10);
    CHECK(pv_level_from_exp(1000000, 0) == 100);
    CHECK(pv_level_from_exp(800000, 4) == 100);
    CHECK(pv_level_from_exp(1250000, 5) == 100);
    CHECK(pv_level_from_exp(1059860, 3) == 100);
    CHECK(pv_level_from_exp(600000, 1) == 100);
    CHECK(pv_level_from_exp(1640000, 2) == 100);
    CHECK(pv_level_from_exp(15, 1) == 2);
    CHECK(pv_level_from_exp(4, 2) == 2);
    CHECK(pv_level_from_exp(9, 3) == 2);
    CHECK(pv_level_from_exp(6, 4) == 2);
    CHECK(pv_level_from_exp(10, 5) == 2);
    CHECK(pv_level_from_exp(125000, 1) == 50);
}

static void test_roundtrip(void)
{
    uint8_t pk[236];
    uint8_t g3[100];
    const uint32_t pid = 8u << 13;

    fill_pk45(pk, 236, pid, 25, 1000, 1, 0, 0, 50);
    CHECK(pv_decrypt45(pk, 236));
    CHECK(pv_read16(pk + 8) == 25);
    CHECK(pk[0x8C] == 50);
    CHECK(pv_is_shiny(pv_read32(pk), pv_read16(pk + 0x0C), pv_read16(pk + 0x0E)));

    fill_pk3(g3, 100, 0x12345678u, 1, 1000, 0x1111, 0x2222, 7);
    CHECK(pv_decrypt3(g3));
    CHECK(pv_read16(g3 + 0x20) == 1);
    CHECK(g3[0x54] == 7);
}

static void test_species(void)
{
    CHECK(strcmp(species_name(1), "Bulbasaur") == 0);
    CHECK(strcmp(species_name(25), "Pikachu") == 0);
    CHECK(strcmp(species_name(29), "Nidoran-F") == 0);
    CHECK(strcmp(species_name(32), "Nidoran-M") == 0);
    CHECK(strcmp(species_name(649), "Genesect") == 0);
    CHECK(strcmp(move_name(1), "Pound") == 0);
    CHECK(strcmp(move_name(33), "Tackle") == 0);
    CHECK(strcmp(move_name(85), "Thunderbolt") == 0);
    CHECK(strcmp(move_name(0), "????") == 0);
    CHECK(move_name(559)[0] != '?');
    CHECK(species_growth(1) == 3);
    CHECK(species_growth(25) == 0);
    CHECK(species_growth(129) == 5);
    CHECK(species_growth(493) == 5);
    CHECK(species_growth(649) == 5);
}

static void test_dp(Dex *dex)
{
    uint8_t *sav = calloc(1, 0x80000);
    uint8_t party[236];
    uint8_t box[136];
    const MonRef *mon;
    const MonRef *boxed;

    CHECK(sav != NULL);
    mark_gen4(sav, 0xC100, 0xC100, 0x121E0);
    fill_pk45(party, 236, 8u << 13, 25, 1000, 1, 0, 0, 50);
    fill_pk45(box, 136, 0xABCDu, 6, 1000, 2, 2, 1, 0);
    CHECK(pv_decrypt45(party, 236));
    party[0x5F] = 10;
    party[0x7B] = 9;
    party[0x7C] = 4;
    party[0x7D] = 22;
    party[0x83] = 4;
    pv_refresh_checksum45(party);
    pv_encrypt45(party, 236);
    sav[0x94] = 1;
    memcpy(sav + 0x98, party, sizeof party);
    memcpy(sav + 0xC100 + 4, box, sizeof box);
    /* Caught Pikachu, seen-only Mew. The egg is stored but not registered. */
    set_save_bit(sav + 0x12DC + 4, 25);
    set_save_bit(sav + 0x12DC + 4 + 0x40, 25);
    set_save_bit(sav + 0x12DC + 4 + 0x40, 151);

    dex_clear(dex);
    CHECK(save_read(dex, "Diamond", sav, 0x80000));
    CHECK(dex->save_count == 1);
    CHECK(strcmp(dex->saves[0].game, "D/P") == 0);
    CHECK(dex->mon_count == 2);
    mon = find_species(dex, 25);
    boxed = find_species(dex, 6);
    CHECK(mon && mon->level == 50 && (mon->flags & MON_PARTY) && (mon->flags & MON_SHINY));
    CHECK(mon && mon->moves[0] == 0 && mon->moves[3] == 0);
    CHECK(boxed && boxed->level == 12 && (boxed->flags & MON_EGG) && boxed->box == 0);
    CHECK(boxed && boxed->moves[0] == 0);
    CHECK(mon && mon->ball == 4 && mon->met_year == 9 && mon->met_month == 4 && mon->met_day == 22);
    CHECK(boxed && boxed->ball == 0 && boxed->met_month == 0);
    CHECK(save_bit(dex->saves[0].dex_caught, 25) && save_bit(dex->saves[0].dex_seen, 25));
    CHECK(!save_bit(dex->saves[0].dex_caught, 151) && save_bit(dex->saves[0].dex_seen, 151));
    CHECK(!save_bit(dex->saves[0].dex_seen, 6));
    free(sav);
}

static void test_pt_party_offset(Dex *dex)
{
    uint8_t *sav = calloc(1, 0x80000);
    uint8_t party[236];

    mark_gen4(sav, 0xCF2C, 0xCF2C, 0x121E4);
    fill_pk45(party, 236, 1, 133, 800000, 5, 5, 0, 36);
    sav[0x9C] = 1;
    memcpy(sav + 0xA0, party, sizeof party);
    set_save_bit(sav + 0x1328 + 4, 133);
    dex_clear(dex);
    CHECK(save_read(dex, "Platinum", sav, 0x80000));
    CHECK(dex->mon_count == 1);
    CHECK(strcmp(dex->saves[0].game, "Pt") == 0);
    CHECK(dex->mons[0].species == 133);
    CHECK(dex->mons[0].level == 36);
    CHECK(dex->mons[0].flags & MON_PARTY);
    CHECK(save_bit(dex->saves[0].dex_caught, 133) && save_bit(dex->saves[0].dex_seen, 133));
    free(sav);
}

static void test_hgss_box_stride(Dex *dex)
{
    uint8_t *sav = calloc(1, 0x80000);
    uint8_t box[136];

    mark_gen4(sav, 0xF628, 0xF700, 0x12310);
    fill_pk45(box, 136, 3, 25, 125000, 1, 1, 0, 0);
    CHECK(pv_decrypt45(box, 136));
    box[0x5F] = 7;
    box[0x83] = 4;
    box[0x86] = 17;
    box[0x7B] = 10;
    box[0x7C] = 3;
    box[0x7D] = 1;
    pv_refresh_checksum45(box);
    pv_encrypt45(box, 136);
    memcpy(sav + 0xF700 + 0x1000, box, sizeof box);
    set_save_bit(sav + 0x12B8 + 4, 25);
    dex_clear(dex);
    CHECK(save_read(dex, "HeartGold", sav, 0x80000));
    CHECK(dex->mon_count == 1);
    CHECK(strcmp(dex->saves[0].game, "HG/SS") == 0);
    CHECK(dex->mons[0].species == 25);
    CHECK(dex->mons[0].box == 1);
    CHECK(dex->mons[0].slot == 0);
    CHECK(dex->mons[0].level == 50);
    CHECK(dex->mons[0].ball == 17);
    CHECK(dex->mons[0].met_year == 10 && dex->mons[0].met_month == 3 && dex->mons[0].met_day == 1);
    CHECK(save_bit(dex->saves[0].dex_caught, 25) && save_bit(dex->saves[0].dex_seen, 25));
    free(sav);
}

static void test_bw_picks_newer_copy(Dex *dex)
{
    uint8_t *sav = calloc(1, 0x80000);
    uint8_t older[220];
    uint8_t newer[220];

    fill_pk45(older, 220, 4, 25, 1000, 1, 1, 0, 12);
    fill_pk45(newer, 220, 9, 133, 1000, 1, 1, 0, 20);
    sav[0x18E04] = 1;
    memcpy(sav + 0x18E08, older, sizeof older);
    seal_gen5(sav, 0x24000, 0x8C, 0x23F34, 1);

    sav[0x24000 + 0x18E04] = 1;
    memcpy(sav + 0x24000 + 0x18E08, newer, sizeof newer);
    set_save_bit(sav + 0x24000 + 0x21600 + 0x08, 133);
    set_save_bit(sav + 0x24000 + 0x21600 + 0x5C, 495);
    seal_gen5(sav + 0x24000, 0x24000, 0x8C, 0x23F34, 9);

    dex_clear(dex);
    CHECK(save_read(dex, "White", sav, 0x80000));
    CHECK(dex->mon_count == 1);
    CHECK(strcmp(dex->saves[0].game, "B/W") == 0);
    CHECK(dex->mons[0].species == 133);
    CHECK(dex->mons[0].level == 20);
    CHECK(dex->mons[0].flags & MON_PARTY);
    CHECK(save_bit(dex->saves[0].dex_caught, 133) && save_bit(dex->saves[0].dex_seen, 133));
    CHECK(!save_bit(dex->saves[0].dex_caught, 495) && save_bit(dex->saves[0].dex_seen, 495));
    CHECK(!save_bit(dex->saves[0].dex_seen, 25));
    free(sav);
}

static void test_gen3(Dex *dex)
{
    uint8_t *sav = calloc(1, 0x20000);
    uint8_t party[100];
    uint8_t box[80];
    uint8_t blaziken[80];
    uint8_t salamence[80];
    int i;

    fill_pk3(party, 100, 0x01020304u, 1, 1000, 0x10, 0x20, 16);
    fill_pk3(box, 80, 0x22222222u, 4, 1059860, 0x10, 0x20, 0);
    /* Internal ids: Blaziken is 282, Salamence is 397. Experience is the
       exact threshold for level 50 medium-slow and level 53 slow. */
    fill_pk3(blaziken, 80, 0x33333333u, 282, 117360, 0x10, 0x20, 0);
    fill_pk3(salamence, 80, 0x44444444u, 397, 186096, 0x10, 0x20, 0);
    CHECK(pv_decrypt3(party));
    pv_write16(party + 0x46, (uint16_t)(4u << 11));
    pv_refresh_checksum3(party);
    pv_encrypt3(party);
    memcpy(sav + 0x1000 + 0x238, party, sizeof party);
    sav[0x1000 + 0x234] = 1;
    /* Section 5 is the first storage section. Box data starts 4 bytes in. */
    memcpy(sav + 5 * 0x1000 + 4, box, sizeof box);
    memcpy(sav + 5 * 0x1000 + 4 + 80, blaziken, sizeof blaziken);
    memcpy(sav + 5 * 0x1000 + 4 + 160, salamence, sizeof salamence);
    set_save_bit(sav + 0x28, 1);
    set_save_bit(sav + 0x5C, 7);

    for (i = 0; i < 14; i++) {
        pv_write16(sav + i * 0x1000 + 0xFF4, (uint16_t)i);
        pv_write32(sav + i * 0x1000 + 0xFFC, 3);
        pv_write16(sav + i * 0x1000 + 0xFF6, pv_checksum32(sav + i * 0x1000, 0xF80));
    }
    for (i = 0; i < 14; i++) {
        int ofs = 0xE000 + i * 0x1000;
        pv_write16(sav + ofs + 0xFF4, (uint16_t)i);
        pv_write32(sav + ofs + 0xFFC, 1);
        pv_write16(sav + ofs + 0xFF6, pv_checksum32(sav + ofs, 0xF80));
    }

    dex_clear(dex);
    CHECK(save_read(dex, "Emerald", sav, 0x20000));
    CHECK(dex->mon_count == 4);
    CHECK(strcmp(dex->saves[0].game, "R/S") == 0);
    CHECK(find_species(dex, 1) && find_species(dex, 1)->level == 16);
    CHECK(find_species(dex, 1)->flags & MON_PARTY);
    CHECK(find_species(dex, 4) && find_species(dex, 4)->level == 100);
    CHECK(find_species(dex, 4)->box == 0);
    CHECK(find_species(dex, 257) && find_species(dex, 257)->level == 50);
    CHECK(find_species(dex, 373) && find_species(dex, 373)->level == 53);
    CHECK(find_species(dex, 282) == NULL);
    CHECK(find_species(dex, 1)->ball == 4 && find_species(dex, 1)->met_month == 0);
    CHECK(save_bit(dex->saves[0].dex_caught, 1) && save_bit(dex->saves[0].dex_seen, 1));
    CHECK(!save_bit(dex->saves[0].dex_caught, 7) && save_bit(dex->saves[0].dex_seen, 7));
    CHECK(!save_bit(dex->saves[0].dex_seen, 4));
    free(sav);
}

static void test_b2w2(Dex *dex)
{
    uint8_t *sav = calloc(1, 0x80000);
    uint8_t party[220];

    fill_pk45(party, 220, 11, 494, 1250000, 3, 3, 0, 70);
    sav[0x18E04] = 1;
    memcpy(sav + 0x18E08, party, sizeof party);
    set_save_bit(sav + 0x21400 + 0x08, 494);
    seal_gen5(sav, 0x26000, 0x94, 0x25F34, 2);

    dex_clear(dex);
    CHECK(save_read(dex, "Black 2", sav, 0x80000));
    CHECK(dex->mon_count == 1);
    CHECK(strcmp(dex->saves[0].game, "B2/W2") == 0);
    CHECK(dex->mons[0].species == 494);
    CHECK(dex->mons[0].level == 70);
    CHECK(save_bit(dex->saves[0].dex_caught, 494) && save_bit(dex->saves[0].dex_seen, 494));
    free(sav);
}

static void test_frlg(Dex *dex)
{
    uint8_t *sav = calloc(1, 0x20000);
    uint8_t party[100];
    int i;

    fill_pk3(party, 100, 0x33333333u, 150, 1000, 1, 1, 22);
    pv_write32(sav + 0xAC, 1);
    sav[0x1000 + 0x034] = 1;
    memcpy(sav + 0x1000 + 0x038, party, sizeof party);
    for (i = 0; i < 14; i++) {
        pv_write16(sav + i * 0x1000 + 0xFF4, (uint16_t)i);
        pv_write32(sav + i * 0x1000 + 0xFFC, 2);
        pv_write16(sav + i * 0x1000 + 0xFF6, pv_checksum32(sav + i * 0x1000, 0xF80));
    }

    dex_clear(dex);
    CHECK(save_read(dex, "LeafGreen", sav, 0x20000));
    CHECK(dex->mon_count == 1);
    CHECK(strcmp(dex->saves[0].game, "FR/LG") == 0);
    CHECK(dex->mons[0].species == 150);
    CHECK(dex->mons[0].level == 22);
    CHECK(dex->mons[0].flags & MON_PARTY);
    free(sav);
}

static void test_moves(Dex *dex)
{
    uint8_t *sav = calloc(1, 0x80000);
    uint8_t party[236];
    uint8_t g3[100];
    /* Personality selects a block order that moves the attack block off slot 1. */
    const uint32_t shuffled = 2u << 13;
    const uint16_t m45[4] = {85, 33, 98, 0};
    const uint16_t m3[4] = {22, 75, 0, 0};
    const MonRef *mon;
    int i;

    CHECK(sav != NULL);
    mark_gen4(sav, 0xC100, 0xC100, 0x121E0);
    fill_pk45_moves(party, 236, shuffled, 25, 1000, 1, 1, 0, 40, m45);
    sav[0x94] = 1;
    memcpy(sav + 0x98, party, sizeof party);
    dex_clear(dex);
    CHECK(save_read(dex, "Diamond", sav, 0x80000));
    mon = find_species(dex, 25);
    CHECK(mon != NULL);
    CHECK(mon && mon->moves[0] == 85);
    CHECK(mon && mon->moves[1] == 33);
    CHECK(mon && mon->moves[2] == 98);
    CHECK(mon && mon->moves[3] == 0);

    memset(sav, 0, 0x80000);
    fill_pk3_moves(g3, 100, 2, 1, 1000, 0x10, 0x20, 16, m3);
    memcpy(sav + 0x1000 + 0x238, g3, sizeof g3);
    sav[0x1000 + 0x234] = 1;
    for (i = 0; i < 14; i++) {
        pv_write16(sav + i * 0x1000 + 0xFF4, (uint16_t)i);
        pv_write32(sav + i * 0x1000 + 0xFFC, 4);
        pv_write16(sav + i * 0x1000 + 0xFF6, pv_checksum32(sav + i * 0x1000, 0xF80));
    }
    dex_clear(dex);
    CHECK(save_read(dex, "Emerald", sav, 0x20000));
    mon = find_species(dex, 1);
    CHECK(mon && mon->level == 16);
    CHECK(mon && mon->moves[0] == 22);
    CHECK(mon && mon->moves[1] == 75);
    CHECK(mon && mon->moves[2] == 0);
    free(sav);
}

static void push_mon(Dex *dex, uint16_t species, uint8_t level)
{
    MonRef *mon = &dex->mons[dex->mon_count];
    memset(mon, 0, sizeof *mon);
    mon->species = species;
    mon->level = level;
    mon->order = (uint16_t)dex->mon_count;
    dex->mon_count++;
}

static void seal_gen3(uint8_t *sav)
{
    int i;
    for (i = 0; i < 14; i++) {
        pv_write16(sav + i * 0x1000 + 0xFF4, (uint16_t)i);
        pv_write32(sav + i * 0x1000 + 0xFFC, 9);
        pv_write16(sav + i * 0x1000 + 0xFF6, pv_checksum32(sav + i * 0x1000, 0xF80));
    }
}

static void test_rows(Dex *dex)
{
    SpeciesRow rows[8];
    int n;

    dex_clear(dex);
    push_mon(dex, 25, 10);
    push_mon(dex, 1, 50);
    push_mon(dex, 25, 40);
    push_mon(dex, 6, 5);
    dex_sort(dex, 1);
    n = dex_species_rows(dex, rows, 8);
    CHECK(n == 3);
    CHECK(rows[0].species == 1 && rows[0].count == 1 && rows[0].first == 0);
    CHECK(rows[1].species == 6 && rows[1].count == 1);
    CHECK(rows[2].species == 25 && rows[2].count == 2 && rows[2].first == 2);
    CHECK(dex->mons[rows[2].first].level == 40);
    CHECK(dex->mons[rows[2].first + 1].level == 10);
}

static void test_battle_stats(Dex *dex)
{
    MonRef mon;
    uint16_t st[6];
    uint8_t *sav;
    uint8_t party[236];
    uint8_t g3[100];
    const MonRef *found;

    memset(&mon, 0, sizeof mon);
    mon.species = 25;
    mon.level = 50;
    mon_battle_stats(&mon, st);
    CHECK(st[0] == 95 && st[1] == 60 && st[2] == 45);
    CHECK(st[3] == 55 && st[4] == 55 && st[5] == 95);

    mon.species = 292;
    mon.level = 100;
    mon.ivs = 0x3FFFFFFFu;
    mon_battle_stats(&mon, st);
    CHECK(st[0] == 1);
    CHECK(st[1] == 216);

    mon.species = 386;
    mon.level = 100;
    mon.ivs = 0;
    mon.form = 0;
    mon_battle_stats(&mon, st);
    CHECK(st[1] == 305);
    mon.form = 1;
    mon_battle_stats(&mon, st);
    CHECK(st[1] == 365);
    CHECK(strcmp(nature_name(3), "Adamant") == 0);
    CHECK(strcmp(nature_name(13), "Jolly") == 0);
    CHECK(nature_name(99)[0] == '?');

    sav = calloc(1, 0x80000);
    CHECK(sav != NULL);
    memset(party, 0, sizeof party);
    pv_write32(party, 3);
    pv_write16(party + 8, 25);
    pv_write16(party + 0x0C, 7);
    pv_write16(party + 0x0E, 7);
    pv_write32(party + 0x10, 1000);
    pv_write32(party + 0x38, 0x3FFFFFFFu);
    party[0x19] = 252;
    party[0x8C] = 100;
    pv_refresh_checksum45(party);
    pv_encrypt45(party, 236);
    mark_gen4(sav, 0xC100, 0xC100, 0x121E0);
    sav[0x94] = 1;
    memcpy(sav + 0x98, party, sizeof party);
    dex_clear(dex);
    CHECK(save_read(dex, "Diamond", sav, 0x80000));
    found = find_species(dex, 25);
    CHECK(found != NULL);
    CHECK(found && found->nature == 3 && found->level == 100);
    CHECK(found && found->evs[1] == 252 && (found->ivs & 31u) == 31u);
    if (found)
        mon_battle_stats(found, st);
    CHECK(st[0] == 211 && st[1] == 229 && st[2] == 116);
    CHECK(st[3] == 122 && st[4] == 136 && st[5] == 216);

    memset(sav, 0, 0x80000);
    memset(party, 0, sizeof party);
    pv_write16(party + 8, 386);
    pv_write32(party + 0x10, 1000);
    party[0x40] = (uint8_t)(1u << 3);
    party[0x8C] = 100;
    pv_refresh_checksum45(party);
    pv_encrypt45(party, 236);
    mark_gen4(sav, 0xC100, 0xC100, 0x121E0);
    sav[0x94] = 1;
    memcpy(sav + 0x98, party, sizeof party);
    dex_clear(dex);
    CHECK(save_read(dex, "Diamond", sav, 0x80000));
    found = find_species(dex, 386);
    CHECK(found && found->form == 1 && found->nature == 0 && found->level == 100);
    if (found)
        mon_battle_stats(found, st);
    CHECK(st[1] == 365);

    memset(sav, 0, 0x20000);
    memset(g3, 0, sizeof g3);
    pv_write16(g3 + 4, 1);
    pv_write16(g3 + 6, 1);
    pv_write16(g3 + 0x20, 1);
    pv_write32(g3 + 0x24, 1000);
    pv_write32(g3 + 0x48, 31u << 5);
    g3[0x39] = 252;
    g3[0x54] = 50;
    pv_refresh_checksum3(g3);
    pv_encrypt3(g3);
    sav[0x1000 + 0x234] = 1;
    memcpy(sav + 0x1000 + 0x238, g3, sizeof g3);
    seal_gen3(sav);
    dex_clear(dex);
    CHECK(save_read(dex, "Emerald", sav, 0x20000));
    found = find_species(dex, 1);
    CHECK(found && found->level == 50 && found->nature == 0 && found->form == 0);
    CHECK(found && found->evs[1] == 252 && ((found->ivs >> 5) & 31u) == 31u);
    if (found)
        mon_battle_stats(found, st);
    /* Bulbasaur, Hardy, attack IV 31 and 252 attack EVs. */
    CHECK(st[0] == 105);
    CHECK(st[1] == 101);
    CHECK(st[2] == 54 && st[3] == 70 && st[4] == 70 && st[5] == 50);
    free(sav);
}

static void test_move_info(void)
{
    MoveInfo info;
    CHECK(!move_info(0, &info));
    CHECK(!move_info(9999, &info));
    CHECK(strcmp(move_effect(0), "") == 0);
    CHECK(move_info(33, &info));
    CHECK(info.category == MOVE_PHYSICAL);
    CHECK(info.power == 50);
    CHECK(info.accuracy == 100);
    CHECK(info.pp == 35);
    CHECK(info.priority == 0);
    CHECK(move_info(85, &info));
    CHECK(info.category == MOVE_SPECIAL);
    CHECK(info.power == 95);
    CHECK(info.pp == 15);
    CHECK(strstr(move_effect(85), "10%"));
    CHECK(move_info(53, &info) && info.power == 95 && info.category == MOVE_SPECIAL);
    CHECK(move_info(282, &info) && info.power == 20);
    CHECK(move_info(129, &info) && info.power == 60 && info.accuracy == 0);
    CHECK(move_info(204, &info) && info.category == MOVE_STATUS && info.power == 0);
    CHECK(move_info(237, &info) && info.power == 0);
    CHECK(strstr(move_effect(237), "30"));
    CHECK(move_info(22, &info) && info.power == 35 && info.pp == 15);
    CHECK(strcmp(move_name(98), "Quick Attack") == 0);
    CHECK(move_info(98, &info) && info.priority == 1);
    CHECK(move_info(46, &info) && info.priority == -6);
    CHECK(move_info(245, &info) && info.priority == 2 && info.power == 80);
}

static void test_types(void)
{
    CHECK(strcmp(type_name(TYPE_ELECTRIC), "Electric") == 0);
    CHECK(strcmp(type_name(TYPE_COUNT), "????") == 0);
    CHECK(species_type(1, 0) == TYPE_GRASS);
    CHECK(species_type(4, 0) == TYPE_FIRE);
    CHECK(species_type(25, 0) == TYPE_ELECTRIC);
    CHECK(species_type(35, 0) == TYPE_NORMAL);
    CHECK(species_type(94, 0) == TYPE_GHOST);
    CHECK(species_type(282, 0) == TYPE_PSYCHIC);
    CHECK(species_type(351, 0) == TYPE_NORMAL);
    CHECK(species_type(351, 1) == TYPE_FIRE);
    CHECK(species_type(351, 2) == TYPE_WATER);
    CHECK(species_type(351, 3) == TYPE_ICE);
    CHECK(species_type(493, 0) == TYPE_NORMAL);
    CHECK(species_type(493, 9) == TYPE_FIRE);
    CHECK(species_type(493, 16) == TYPE_DARK);
    CHECK(species_type2(1, 0) == TYPE_POISON);
    CHECK(species_type2(4, 0) == TYPE_COUNT);
    CHECK(species_type2(6, 0) == TYPE_FLYING);
    CHECK(species_type2(35, 0) == TYPE_COUNT);
    CHECK(species_type2(183, 0) == TYPE_COUNT);
    CHECK(species_type2(468, 0) == TYPE_FLYING);
    CHECK(species_type(479, 0) == TYPE_ELECTRIC);
    CHECK(species_type2(479, 0) == TYPE_GHOST);
    CHECK(species_type2(479, 1) == TYPE_FIRE);
    CHECK(species_type2(351, 1) == TYPE_COUNT);
    CHECK(species_type2(492, 0) == TYPE_COUNT);
    CHECK(species_type2(492, 1) == TYPE_FLYING);
    CHECK(species_type2(648, 0) == TYPE_PSYCHIC);
    CHECK(species_type2(648, 1) == TYPE_FIGHTING);
    CHECK(type_effect(TYPE_FIRE, TYPE_GRASS, TYPE_POISON) == 8);
    CHECK(type_effect(TYPE_ROCK, TYPE_FIRE, TYPE_FLYING) == 16);
    CHECK(type_effect(TYPE_ELECTRIC, TYPE_GROUND, TYPE_ROCK) == 0);
    CHECK(type_effect(TYPE_PSYCHIC, TYPE_POISON, TYPE_FIGHTING) == 16);
    CHECK(type_effect(TYPE_GHOST, TYPE_STEEL, TYPE_COUNT) == 2);
    CHECK(type_effect(TYPE_DARK, TYPE_STEEL, TYPE_COUNT) == 2);
    CHECK(type_effect(TYPE_FIGHTING, TYPE_GHOST, TYPE_DARK) == 0);
    {
        const char *entry = species_flavor(1);
        CHECK(entry && entry[0]);
        CHECK(strstr(entry, "seed") != NULL);
        CHECK(species_flavor(649)[0] != 0);
        CHECK(species_flavor(0)[0] == 0);
        CHECK(species_flavor(999)[0] == 0);
    }
    CHECK(move_type(33) == TYPE_NORMAL);
    CHECK(move_type(53) == TYPE_FIRE);
    CHECK(move_type(85) == TYPE_ELECTRIC);
    CHECK(move_type(204) == TYPE_NORMAL);
    CHECK(move_type(0) == TYPE_COUNT);
    CHECK(move_type(9999) == TYPE_COUNT);
}

static void test_tidy_name(void)
{
    char name[80];

    strcpy(name, "Pokemon - Silberne Edition");
    dex_tidy_name(name);
    CHECK(strcmp(name, "Silberne Edition") == 0);

    strcpy(name, "Pok\xC3\xA9mon \xE2\x80\x93 HeartGold");
    dex_tidy_name(name);
    CHECK(strcmp(name, "HeartGold") == 0);

    strcpy(name, "POKEMON_-_Perl-Edition");
    dex_tidy_name(name);
    CHECK(strcmp(name, "Perl-Edition") == 0);

    strcpy(name, "[Pokemon] - Blattgrune Edition");
    dex_tidy_name(name);
    CHECK(strcmp(name, "Blattgrune Edition") == 0);

    strcpy(name, "Pokemon(TM) - Platin-Edition");
    dex_tidy_name(name);
    CHECK(strcmp(name, "Platin-Edition") == 0);

    strcpy(name, "Diamond");
    dex_tidy_name(name);
    CHECK(strcmp(name, "Diamond") == 0);

    strcpy(name, "Pokemon");
    dex_tidy_name(name);
    CHECK(strcmp(name, "Pokemon") == 0);

    strcpy(name, "Pok\xE9mon - Kristall-Edition");
    dex_tidy_name(name);
    CHECK(strcmp(name, "Kristall-Edition") == 0);

    strcpy(name, "Pokemon - Silberne Edition Patch");
    dex_tidy_name(name);
    CHECK(strcmp(name, "Silberne Edition") == 0);

    strcpy(name, "Pokemon - Silberne Edition v1");
    dex_tidy_name(name);
    CHECK(strcmp(name, "Silberne Edition") == 0);

    strcpy(name, "Pokemon - HeartGold v0.4.7");
    dex_tidy_name(name);
    CHECK(strcmp(name, "HeartGold") == 0);

    strcpy(name, "Diamond (Patch) [v1]");
    dex_tidy_name(name);
    CHECK(strcmp(name, "Diamond") == 0);

    strcpy(name, "POKEMON_-_Perl-Edition_v0.4.7_PATCHED");
    dex_tidy_name(name);
    CHECK(strcmp(name, "Perl-Edition") == 0);

    strcpy(name, "Platin-Edition-patch v1.2");
    dex_tidy_name(name);
    CHECK(strcmp(name, "Platin-Edition") == 0);

    strcpy(name, "Emerald [PATCHED] V1.2.3");
    dex_tidy_name(name);
    CHECK(strcmp(name, "Emerald") == 0);

    strcpy(name, "Feuerrote Edition(v0.4.7)");
    dex_tidy_name(name);
    CHECK(strcmp(name, "Feuerrote Edition") == 0);

    strcpy(name, "Pokemon - Version Saphir");
    dex_tidy_name(name);
    CHECK(strcmp(name, "Version Saphir") == 0);
}

static void test_rejects_garbage(Dex *dex)
{
    uint8_t junk[128];
    memset(junk, 0xAB, sizeof junk);
    dex_clear(dex);
    CHECK(!save_read(dex, "nope", junk, sizeof junk));
    CHECK(dex->mon_count == 0);
}

int main(void)
{
    static Dex dex;
    test_exp();
    test_roundtrip();
    test_species();
    test_dp(&dex);
    test_pt_party_offset(&dex);
    test_hgss_box_stride(&dex);
    test_bw_picks_newer_copy(&dex);
    test_b2w2(&dex);
    test_gen3(&dex);
    test_frlg(&dex);
    test_moves(&dex);
    test_rows(&dex);
    test_battle_stats(&dex);
    test_move_info();
    test_types();
    test_tidy_name();
    test_rejects_garbage(&dex);
    if (fails) {
        printf("%d checks failed\n", fails);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
