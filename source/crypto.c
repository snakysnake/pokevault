#include "crypto.h"

#include <string.h>

uint16_t pv_read16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

uint32_t pv_read32(const uint8_t *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

void pv_write16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

void pv_write32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

uint16_t pv_add16(const uint8_t *p, size_t len)
{
    uint32_t sum = 0;
    size_t i;
    for (i = 0; i + 1 < len; i += 2)
        sum += pv_read16(p + i);
    return (uint16_t)sum;
}

uint16_t pv_crc16_ccitt(const uint8_t *p, size_t len)
{
    uint8_t top = 0xFF;
    uint8_t bot = 0xFF;
    size_t i;
    for (i = 0; i < len; i++) {
        uint8_t x = (uint8_t)(p[i] ^ top);
        x ^= (uint8_t)(x >> 4);
        top = (uint8_t)(bot ^ (x >> 3) ^ (uint8_t)(x << 4));
        bot = (uint8_t)(x ^ (uint8_t)(x << 5));
    }
    return (uint16_t)((top << 8) | bot);
}

uint16_t pv_checksum32(const uint8_t *p, size_t len)
{
    uint32_t sum = 0;
    size_t i;
    for (i = 0; i + 3 < len; i += 4)
        sum += pv_read32(p + i);
    return (uint16_t)(sum + (sum >> 16));
}

/* Block identity at each physical slot. Index by (PID >> 13) & 31.
   Entries 24..31 repeat 0..7 so the shift can skip a modulo. */
static const uint8_t block_order[32 * 4] = {
    0, 1, 2, 3,  0, 1, 3, 2,  0, 2, 1, 3,  0, 3, 1, 2,
    0, 2, 3, 1,  0, 3, 2, 1,  1, 0, 2, 3,  1, 0, 3, 2,
    2, 0, 1, 3,  3, 0, 1, 2,  2, 0, 3, 1,  3, 0, 2, 1,
    1, 2, 0, 3,  1, 3, 0, 2,  2, 1, 0, 3,  3, 1, 0, 2,
    2, 3, 0, 1,  3, 2, 0, 1,  1, 2, 3, 0,  1, 3, 2, 0,
    2, 1, 3, 0,  3, 1, 2, 0,  2, 3, 1, 0,  3, 2, 1, 0,
    0, 1, 2, 3,  0, 1, 3, 2,  0, 2, 1, 3,  0, 3, 1, 2,
    0, 2, 3, 1,  0, 3, 2, 1,  1, 0, 2, 3,  1, 0, 3, 2
};

static void unshuffle(uint8_t *blocks, unsigned sv, int block_size)
{
    uint8_t tmp[128];
    const uint8_t *order = &block_order[(sv & 31) * 4];
    int i;
    for (i = 0; i < 4; i++)
        memcpy(tmp + i * block_size, blocks + order[i] * block_size, (size_t)block_size);
    memcpy(blocks, tmp, (size_t)block_size * 4);
}

static uint32_t lcrng(uint32_t seed)
{
    return seed * 0x41C64E6Du + 0x6073u;
}

static void crypt_array(uint8_t *data, int len, uint32_t seed)
{
    int i;
    for (i = 0; i + 1 < len; i += 2) {
        uint16_t x;
        seed = lcrng(seed);
        x = (uint16_t)(seed >> 16);
        data[i] ^= (uint8_t)x;
        data[i + 1] ^= (uint8_t)(x >> 8);
    }
}

static uint16_t checksum45(const uint8_t *data)
{
    return pv_add16(data + 8, 128);
}

bool pv_decrypt45(uint8_t *data, int len)
{
    uint16_t chk;
    uint32_t pv;
    uint32_t sv;

    if (len != 136 && len != 220 && len != 236)
        return false;

    chk = pv_read16(data + 6);
    if (checksum45(data) == chk)
        return true;

    pv = pv_read32(data);
    sv = (pv >> 13) & 31u;
    crypt_array(data + 8, 128, chk);
    if (len > 136)
        crypt_array(data + 136, len - 136, pv);
    unshuffle(data + 8, sv, 32);
    return checksum45(data) == chk;
}

bool pv_decrypt3(uint8_t *data)
{
    uint32_t pid;
    uint32_t oid;
    uint32_t key;
    uint16_t expect;
    int i;

    expect = pv_read16(data + 0x1C);
    if (pv_add16(data + 0x20, 48) == expect)
        return true;

    pid = pv_read32(data);
    oid = pv_read32(data + 4);
    key = pid ^ oid;
    for (i = 0; i < 48; i += 4)
        pv_write32(data + 0x20 + i, pv_read32(data + 0x20 + i) ^ key);
    unshuffle(data + 0x20, pid % 24u, 12);
    return pv_add16(data + 0x20, 48) == expect;
}

bool pv_is_shiny(uint32_t pid, uint16_t tid, uint16_t sid)
{
    uint16_t x = (uint16_t)((pid & 0xFFFFu) ^ (pid >> 16) ^ tid ^ sid);
    return x < 8;
}

static uint32_t exp_erratic(int n)
{
    uint64_t n3 = (uint64_t)n * (uint64_t)n * (uint64_t)n;
    if (n <= 50)
        return (uint32_t)(n3 * (uint64_t)(100 - n) / 50u);
    if (n <= 68)
        return (uint32_t)(n3 * (uint64_t)(150 - n) / 100u);
    if (n <= 98)
        return (uint32_t)(n3 * (uint64_t)((1911 - 10 * n) / 3) / 500u);
    return (uint32_t)(n3 * (uint64_t)(160 - n) / 100u);
}

static uint32_t exp_fluctuating(int n)
{
    uint64_t n3 = (uint64_t)n * (uint64_t)n * (uint64_t)n;
    if (n <= 15)
        return (uint32_t)(n3 * (uint64_t)(((n + 1) / 3) + 24) / 50u);
    if (n <= 36)
        return (uint32_t)(n3 * (uint64_t)(n + 14) / 50u);
    return (uint32_t)(n3 * (uint64_t)((n / 2) + 32) / 50u);
}

static uint32_t exp_at(uint8_t growth, int level)
{
    uint64_t n;
    uint64_t n3;
    int64_t slow;

    if (level <= 1)
        return 0;
    n = (uint64_t)level;
    n3 = n * n * n;
    switch (growth) {
    case 0: /* medium fast */
        return (uint32_t)n3;
    case 1:
        return exp_erratic(level);
    case 2:
        return exp_fluctuating(level);
    case 3: /* medium slow */
        slow = (int64_t)(6u * n3 / 5u) - (int64_t)(15u * n * n) + (int64_t)(100u * n) - 140;
        return slow > 0 ? (uint32_t)slow : 0;
    case 4: /* fast */
        return (uint32_t)(4u * n3 / 5u);
    case 5: /* slow */
        return (uint32_t)(5u * n3 / 4u);
    default:
        return (uint32_t)n3;
    }
}

uint8_t pv_level_from_exp(uint32_t exp, uint8_t growth)
{
    uint8_t level = 1;
    if (growth > 5)
        growth = 0;
    while (level < 100 && exp >= exp_at(growth, level + 1))
        level++;
    return level;
}

#ifdef HOST_TEST
void pv_refresh_checksum45(uint8_t *data)
{
    pv_write16(data + 6, checksum45(data));
}

void pv_refresh_checksum3(uint8_t *data)
{
    pv_write16(data + 0x1C, pv_add16(data + 0x20, 48));
}

static void shuffle_to_disk(uint8_t *blocks, unsigned sv, int block_size)
{
    uint8_t tmp[128];
    const uint8_t *order = &block_order[(sv & 31) * 4];
    int i;
    memset(tmp, 0, sizeof tmp);
    for (i = 0; i < 4; i++)
        memcpy(tmp + order[i] * block_size, blocks + i * block_size, (size_t)block_size);
    memcpy(blocks, tmp, (size_t)block_size * 4);
}

void pv_encrypt45(uint8_t *data, int len)
{
    uint32_t pv = pv_read32(data);
    uint16_t chk = pv_read16(data + 6);
    uint32_t sv = (pv >> 13) & 31u;
    shuffle_to_disk(data + 8, sv, 32);
    crypt_array(data + 8, 128, chk);
    if (len > 136)
        crypt_array(data + 136, len - 136, pv);
}

void pv_encrypt3(uint8_t *data)
{
    uint32_t pid = pv_read32(data);
    uint32_t key = pid ^ pv_read32(data + 4);
    int i;
    shuffle_to_disk(data + 0x20, pid % 24u, 12);
    for (i = 0; i < 48; i += 4)
        pv_write32(data + 0x20 + i, pv_read32(data + 0x20 + i) ^ key);
}
#endif
