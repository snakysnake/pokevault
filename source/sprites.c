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
    MON_SPRITE_Y = 24
};

static u16 *gfx;
static FILE *bank;
static int ready;
static int failed;
static int shown = -2;
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

void sprites_init(void)
{
    vramSetBankD(VRAM_D_SUB_SPRITE);
    oamInit(&oamSub, SpriteMapping_1D_128, false);
    gfx = oamAllocateGfx(&oamSub, SpriteSize_64x64, SpriteColorFormat_256Color);
    if (gfx)
        place(true);
}

void sprites_flush(void)
{
    if (gfx)
        oamUpdate(&oamSub);
}

void sprites_hide(void)
{
    shown = -1;
    if (gfx)
        place(true);
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

void sprites_show(unsigned species, int shiny, int egg)
{
    int index;
    int use_shiny;

    if (!ensure()) {
        sprites_hide();
        return;
    }

    if (egg) {
        if ((flags[0] & 1) == 0) {
            sprites_hide();
            return;
        }
        index = 0;
    } else if (species < 1 || species > MON_SPRITES) {
        sprites_hide();
        return;
    } else {
        use_shiny = shiny && (flags[species] & 2) != 0;
        if (!use_shiny && (flags[species] & 1) == 0) {
            sprites_hide();
            return;
        }
        index = 1 + (int)(species - 1) * 2 + (use_shiny ? 1 : 0);
    }

    if (index == shown)
        return;
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
