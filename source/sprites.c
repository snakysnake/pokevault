#include "sprites.h"

#include "music.h"

#include <nds.h>
#include <stdio.h>
#include <string.h>

/* nitro:/sprites.bin, written by tools/build_assets.py.
   Header: magic, version, count, width, height, image_bytes, data_off.
   Then one flag byte per species (index 0 is the egg). Bit 0 = normal, bit 1 = shiny.
   Images follow at data_off: egg, then normal/shiny pairs for species 1..649.
   Each image is 256 RGB555 colors and a 64x64 8bpp sprite in 8x8 tile order.
   Palette index 0 is transparent.

   The card and the idle screen use one hardware sprite. A box holds 30,
   and the main engine only has 16 sprite palettes of 256 colors, so the
   grid is drawn into a direct-color background instead. The hand is still
   a hardware sprite.

   The grid remembers the decoded party and one box (32x32, about 72KB).
   That is enough for L/R. The national dex stays on the card. */

enum {
    MON_SPRITES = 649,
    MON_SPRITE_BYTES = 512 + 64 * 64,
    /* Sits in the right-hand pocket, under the two header rows and
       clear of the one-tile frame. */
    MON_SPRITE_X = 184,
    MON_SPRITE_Y = 24,
    /* Centered on the top screen for the idle screensaver. */
    MON_TOP_X = (256 - 64) / 2,
    MON_TOP_Y = (192 - 64) / 2,
    /* 32x32 is the grid size. Party plus one box, not the whole dex. */
    BOX_SPRITE = 32,
    CACHE_PARTY = 6,
    CACHE_BOX = 30
};

static u16 *gfx;
static u16 *gfx_top;
static u16 *hand_gfx;
static FILE *bank;
static int ready;
static int failed;
static int shown = -2;
static int shown_top = -2;
static int hand_on;
static int hand_x;
static int hand_y;
static uint32_t data_off;
static uint8_t flags[MON_SPRITES + 1];
static uint8_t image[MON_SPRITE_BYTES] __attribute__((aligned(32)));
/* Image index last read into `image`. -1 is none. Skips a second card read
   when the bottom card asks for the sprite the grid just loaded. */
static int resident = -1;

/* Decoded grid sprite. stamp is the party or box generation that still wants it. */
typedef struct {
    int index;
    int stamp;
    unsigned short pix[BOX_SPRITE * BOX_SPRITE];
} CachedSprite;

static CachedSprite party_cache[CACHE_PARTY];
static CachedSprite box_cache[CACHE_BOX];
static int party_n;
static int box_n;
static int party_stamp;
static int box_stamp;
static int party_key = -1;
static int box_key = -1;
static int use_party;

/* Right-pointing hand. 1 outline, 2 fill, 3 fingertip.
   Outline plus fill so it reads on black and on white. */
static const char hand_rows[16][17] = {
    "................",
    "..........11....",
    ".........1221...",
    "........122221..",
    ".......12222221.",
    ".......122222211",
    "......1122222213",
    "......1122122213",
    ".......12221221.",
    "........1122221.",
    ".........11111..",
    "................",
    "................",
    "................",
    "................",
    "................"
};

static uint16_t ru16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t ru32(const uint8_t *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static void place(int hide)
{
    oamSet(&oamSub, 0, MON_SPRITE_X, MON_SPRITE_Y, 0, 0,
           SpriteSize_64x64, SpriteColorFormat_256Color,
           gfx, -1, false, hide, false, false, false);
}

static void hand_store(void);
void sprites_hand_hide(void);

static void place_top(int dy, int hide)
{
    int y = MON_TOP_Y + dy;
    if (y < 0)
        y = 0;
    oamSet(&oamMain, 0, MON_TOP_X, y, 0, 0,
           SpriteSize_64x64, SpriteColorFormat_256Color,
           gfx_top, -1, false, hide, false, false, false);
}

void sprites_init(void)
{
    vramSetBankD(VRAM_D_SUB_SPRITE);
    oamInit(&oamSub, SpriteMapping_1D_128, false);
    gfx = oamAllocateGfx(&oamSub, SpriteSize_64x64, SpriteColorFormat_256Color);
    if (gfx)
        place(true);

    vramSetBankE(VRAM_E_MAIN_SPRITE);
    oamInit(&oamMain, SpriteMapping_1D_128, false);
    gfx_top = oamAllocateGfx(&oamMain, SpriteSize_64x64, SpriteColorFormat_256Color);
    if (gfx_top)
        place_top(0, true);
    hand_gfx = oamAllocateGfx(&oamMain, SpriteSize_16x16, SpriteColorFormat_16Color);
    if (hand_gfx) {
        hand_store();
        sprites_hand_hide();
    }
}

void sprites_flush(void)
{
    if (gfx)
        oamUpdate(&oamSub);
    if (gfx_top)
        oamUpdate(&oamMain);
}

static int hand_pixel(int x, int y)
{
    char c;
    if (x < 0 || y < 0 || x > 15 || y > 15)
        return 0;
    c = hand_rows[y][x];
    if (c == '1')
        return 1;
    if (c == '2')
        return 2;
    if (c == '3')
        return 3;
    return 0;
}

/* 16x16 4bpp, two tiles across. VRAM is written a halfword at a time. */
static void hand_store(void)
{
    uint8_t bytes[128];
    int y;
    int x;
    int i;
    memset(bytes, 0, sizeof bytes);
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            int pix = hand_pixel(x, y);
            int tile = (y >> 3) * 2 + (x >> 3);
            int col = x & 7;
            int off = tile * 32 + (y & 7) * 4 + (col >> 1);
            if (col & 1)
                bytes[off] = (uint8_t)((bytes[off] & 0x0F) | (pix << 4));
            else
                bytes[off] = (uint8_t)((bytes[off] & 0xF0) | pix);
        }
    }
    for (i = 0; i < 64; i++)
        hand_gfx[i] = (u16)(bytes[i * 2] | (bytes[i * 2 + 1] << 8));
    SPRITE_PALETTE[0] = 0;
    SPRITE_PALETTE[1] = RGB15(0, 0, 0);
    SPRITE_PALETTE[2] = RGB15(31, 31, 31);
    SPRITE_PALETTE[3] = RGB15(31, 4, 4);
}

static void place_hand(int x, int y, int hide)
{
    if (!hand_gfx)
        return;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x > 256 - 16)
        x = 256 - 16;
    if (y > 192 - 16)
        y = 192 - 16;
    oamSet(&oamMain, 1, x, y, 0, 0,
           SpriteSize_16x16, SpriteColorFormat_16Color,
           hand_gfx, -1, false, hide, false, false, false);
}

void sprites_hand_hide(void)
{
    hand_on = 0;
    place_hand(0, 0, true);
}

void sprites_hand(int x, int y)
{
    hand_x = x;
    hand_y = y;
    hand_on = hand_gfx != NULL;
    if (!hand_gfx)
        return;
    SPRITE_PALETTE[0] = 0;
    SPRITE_PALETTE[1] = RGB15(0, 0, 0);
    SPRITE_PALETTE[2] = RGB15(31, 31, 31);
    SPRITE_PALETTE[3] = RGB15(31, 4, 4);
    place_hand(x, y, false);
}

void sprites_hand_nudge(int dy)
{
    if (!hand_on)
        return;
    place_hand(hand_x, hand_y + dy, false);
}

void sprites_hide_top(void)
{
    if (gfx_top)
        place_top(0, true);
    sprites_hand_hide();
}

void sprites_hide(void)
{
    shown = -1;
    if (gfx)
        place(true);
    sprites_hide_top();
}

static int ensure(void)
{
    uint8_t hdr[20];
    if (ready)
        return 1;
    if (failed || !gfx)
        return 0;
    bank = fopen("nitro:/sprites.bin", "rb");
    if (!bank)
        goto bad;
    if (fread(hdr, 1, 20, bank) != 20 || memcmp(hdr, "PVSP", 4) != 0)
        goto bad;
    if (ru16(hdr + 4) != 1 || ru16(hdr + 6) != MON_SPRITES)
        goto bad;
    if (ru16(hdr + 8) != 64 || ru16(hdr + 10) != 64 || ru32(hdr + 12) != MON_SPRITE_BYTES)
        goto bad;
    data_off = ru32(hdr + 16);
    if (data_off < 20u + sizeof flags || data_off > 1024u)
        goto bad;
    if (fseek(bank, 20, SEEK_SET) != 0 || fread(flags, 1, sizeof flags, bank) != sizeof flags)
        goto bad;
    ready = 1;
    music_pump();
    return 1;
bad:
    if (bank) {
        fclose(bank);
        bank = NULL;
    }
    failed = 1;
    music_pump();
    return 0;
}

static int load_image(int index)
{
    long off;
    int ok;

    /* The stream holds about 46 ms. A box of these reads, back to back,
       runs it dry. Refill after each one; a repeat of the same picture
       is already in `image` and does not touch the card. */
    if (index == resident)
        return 1;
    off = (long)data_off + (long)index * MON_SPRITE_BYTES;
    ok = fseek(bank, off, SEEK_SET) == 0
        && fread(image, 1, MON_SPRITE_BYTES, bank) == MON_SPRITE_BYTES;
    resident = ok ? index : -1;
    music_pump();
    return ok;
}

static int image_index(unsigned species, int shiny, int egg)
{
    int use_shiny;

    if (egg) {
        if ((flags[0] & 1) == 0)
            return -1;
        return 0;
    }
    if (species < 1 || species > MON_SPRITES)
        return -1;
    use_shiny = shiny && (flags[species] & 2) != 0;
    if (!use_shiny && (flags[species] & 1) == 0)
        return -1;
    return 1 + (int)(species - 1) * 2 + (use_shiny ? 1 : 0);
}

static int sprite_pixel(const uint8_t *px, int x, int y)
{
    int tile = (y >> 3) * 8 + (x >> 3);
    return px[tile * 64 + (y & 7) * 8 + (x & 7)];
}

static void bmp_put(unsigned short *bmp, int x, int y, unsigned short color)
{
    if ((unsigned)x < 256 && (unsigned)y < 192)
        bmp[y * 256 + x] = color;
}

/* Cream egg with a dark edge, so it still reads when the sprite is missing. */
static void mark_egg(unsigned short *bmp, int x, int y)
{
    int py;
    int px;
    unsigned short fill = (unsigned short)(RGB15(31, 30, 24) | 0x8000);
    unsigned short line = (unsigned short)(RGB15(2, 2, 4) | 0x8000);
    for (py = 2; py < 30; py++) {
        for (px = 6; px < 26; px++) {
            int dx = px - 16;
            int dy = py - 15;
            int rx = 7 + py / 8;
            int ry = 12;
            int e = dx * dx * ry * ry + dy * dy * rx * rx;
            int limit = rx * rx * ry * ry;
            int rx_in = rx - 2;
            int ry_in = ry - 2;
            int inner;
            if (rx_in < 1)
                rx_in = 1;
            if (ry_in < 1)
                ry_in = 1;
            inner = rx_in * rx_in * ry_in * ry_in;
            if (e > limit)
                continue;
            bmp_put(bmp, x + px, y + py, e > inner ? line : fill);
        }
    }
}

/* 2x2 sample of the 64x64 tile picture. 0 keeps the panel showing through. */
static unsigned short half_at(int x, int y)
{
    const unsigned short *pal = (const unsigned short *)image;
    const uint8_t *px = image + 512;
    int sx = x * 2;
    int sy = y * 2;
    int i = sprite_pixel(px, sx, sy);
    if (!i)
        i = sprite_pixel(px, sx + 1, sy);
    if (!i)
        i = sprite_pixel(px, sx, sy + 1);
    if (!i)
        i = sprite_pixel(px, sx + 1, sy + 1);
    if (!i)
        return 0;
    return (unsigned short)(pal[i] | 0x8000);
}

static void decode_half(unsigned short *dst)
{
    int y;
    int x;
    for (y = 0; y < BOX_SPRITE; y++) {
        for (x = 0; x < BOX_SPRITE; x++)
            dst[y * BOX_SPRITE + x] = half_at(x, y);
    }
}

static void blit_cached(unsigned short *bmp, int dx, int dy, const unsigned short *src)
{
    int y;
    int x;
    for (y = 0; y < BOX_SPRITE; y++) {
        for (x = 0; x < BOX_SPRITE; x++) {
            unsigned short c = src[y * BOX_SPRITE + x];
            if (c)
                bmp_put(bmp, dx + x, dy + y, c);
        }
    }
}

static void blit_half(unsigned short *bmp, int dx, int dy)
{
    int y;
    int x;
    for (y = 0; y < BOX_SPRITE; y++) {
        for (x = 0; x < BOX_SPRITE; x++) {
            unsigned short c = half_at(x, y);
            if (c)
                bmp_put(bmp, dx + x, dy + y, c);
        }
    }
}

static CachedSprite *find_index(CachedSprite *list, int n, int index)
{
    int i;
    for (i = 0; i < n; i++) {
        if (list[i].index == index)
            return &list[i];
    }
    return NULL;
}

static CachedSprite *active_cache(int **count, int *cap, int *stamp)
{
    if (use_party) {
        *count = &party_n;
        *cap = CACHE_PARTY;
        *stamp = party_stamp;
        return party_cache;
    }
    *count = &box_n;
    *cap = CACHE_BOX;
    *stamp = box_stamp;
    return box_cache;
}

/* Claim a picture this grid still shows. Pixels are only species, shine,
   and egg, so a hit in the other half is just as good. */
static CachedSprite *cache_find(int index, int claim)
{
    int *count;
    int cap;
    int stamp;
    CachedSprite *active = active_cache(&count, &cap, &stamp);
    CachedSprite *e = find_index(active, *count, index);
    (void)cap;
    if (e) {
        if (claim)
            e->stamp = stamp;
        return e;
    }
    if (use_party)
        return find_index(box_cache, box_n, index);
    return find_index(party_cache, party_n, index);
}

static CachedSprite *cache_alloc(void)
{
    int *count;
    int cap;
    int stamp;
    CachedSprite *list = active_cache(&count, &cap, &stamp);
    CachedSprite *slot = NULL;
    int i;
    for (i = 0; i < *count; i++) {
        if (list[i].stamp != stamp) {
            slot = &list[i];
            break;
        }
    }
    if (!slot) {
        if (*count >= cap)
            return NULL;
        slot = &list[(*count)++];
    }
    /* -1 so a half-written slot is not treated as the egg (index 0). */
    slot->index = -1;
    slot->stamp = stamp;
    return slot;
}

void sprites_box_use(int party, int key)
{
    use_party = party ? 1 : 0;
    if (use_party) {
        if (party_key != key) {
            party_key = key;
            party_stamp++;
        }
    } else if (box_key != key) {
        box_key = key;
        box_stamp++;
    }
}

void sprites_box_keep(unsigned species, int shiny, int egg)
{
    int index;
    if (!ensure())
        return;
    index = image_index(species, shiny, egg);
    if (index < 0)
        return;
    cache_find(index, 1);
}

void sprites_blit(unsigned short *bmp, int x, int y, unsigned species, int shiny, int egg)
{
    int index;
    CachedSprite *hit;
    CachedSprite *slot;
    if (!bmp)
        return;
    if (!ensure()) {
        if (egg)
            mark_egg(bmp, x, y);
        return;
    }
    index = image_index(species, shiny, egg);
    if (index < 0) {
        if (egg)
            mark_egg(bmp, x, y);
        return;
    }
    hit = cache_find(index, 1);
    if (hit) {
        blit_cached(bmp, x, y, hit->pix);
        return;
    }
    if (!load_image(index)) {
        if (egg)
            mark_egg(bmp, x, y);
        return;
    }
    slot = cache_alloc();
    if (!slot) {
        blit_half(bmp, x, y);
        return;
    }
    slot->index = index;
    decode_half(slot->pix);
    blit_cached(bmp, x, y, slot->pix);
}

void sprites_show(unsigned species, int shiny, int egg)
{
    int index;

    sprites_hide_top();
    if (!ensure()) {
        sprites_hide();
        return;
    }
    index = image_index(species, shiny, egg);
    if (index < 0) {
        sprites_hide();
        return;
    }
    if (index == shown) {
        place(false);
        return;
    }
    if (!load_image(index)) {
        sprites_hide();
        return;
    }
    DC_FlushRange(image, MON_SPRITE_BYTES);
    dmaCopy(image, SPRITE_PALETTE_SUB, 512);
    dmaCopy(image + 512, gfx, 64 * 64);
    shown = index;
    place(false);
}

void sprites_show_top(unsigned species, int shiny, int egg, int dy)
{
    int index;

    if (gfx)
        place(true);
    if (!gfx_top || !ensure()) {
        sprites_hide_top();
        return;
    }
    index = image_index(species, shiny, egg);
    if (index < 0) {
        sprites_hide_top();
        return;
    }
    if (index != shown_top) {
        if (!load_image(index)) {
            sprites_hide_top();
            return;
        }
        DC_FlushRange(image, MON_SPRITE_BYTES);
        dmaCopy(image, SPRITE_PALETTE, 512);
        dmaCopy(image + 512, gfx_top, 64 * 64);
        shown_top = index;
    }
    place_top(dy, false);
}
