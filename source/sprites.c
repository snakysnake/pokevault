#include "sprites.h"

#include <nds.h>
#include <stdio.h>
#include <string.h>

/* nitro:/sprites.bin, written by tools/build_assets.py.
   Header: magic, version, count, width, height, image_bytes, data_off.
   Then one flag byte per species (index 0 is the egg). Bit 0 = normal, bit 1 = shiny.
   Images follow at data_off: egg, then normal/shiny pairs for species 1..649.
   Each image is 256 RGB555 colors and a 64x64 8bpp sprite in 8x8 tile order.
   Palette index 0 is transparent. */

enum {
    MON_SPRITES = 649,
    MON_SPRITE_BYTES = 512 + 64 * 64,
    /* Sits in the right-hand pocket, under the two header rows and
       clear of the one-tile frame. */
    MON_SPRITE_X = 184,
    MON_SPRITE_Y = 24,
    /* Centered on the top screen for the idle screensaver. */
    MON_TOP_X = (256 - 64) / 2,
    MON_TOP_Y = (192 - 64) / 2
};

static u16 *gfx;
static u16 *gfx_top;
static FILE *bank;
static int ready;
static int failed;
static int shown = -2;
static int shown_top = -2;
static uint32_t data_off;
static uint8_t flags[MON_SPRITES + 1];
static uint8_t image[MON_SPRITE_BYTES] __attribute__((aligned(32)));

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
}

void sprites_flush(void)
{
    if (gfx)
        oamUpdate(&oamSub);
    if (gfx_top)
        oamUpdate(&oamMain);
}

void sprites_hide_top(void)
{
    if (gfx_top)
        place_top(0, true);
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
    return 1;
bad:
    if (bank) {
        fclose(bank);
        bank = NULL;
    }
    failed = 1;
    return 0;
}

static int load_image(int index)
{
    long off = (long)data_off + (long)index * MON_SPRITE_BYTES;
    if (fseek(bank, off, SEEK_SET) != 0)
        return 0;
    return fread(image, 1, MON_SPRITE_BYTES, bank) == MON_SPRITE_BYTES;
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
