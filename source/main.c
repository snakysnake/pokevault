#include "music.h"
#include "scan.h"
#include "ui.h"

#include <fat.h>
#include <filesystem.h>
#include <nds.h>
#include <stdio.h>

static Dex dex;

/* A CPU fault used to come back as nds-bootstrap's "An error has occurred",
   with no way to tell what failed. Stay on this screen instead. */
static void on_fault(ExcptContext *ctx, unsigned flags)
{
    const char *kind = "undefined instruction";
    unsigned mode = ctx->excpt_cpsr & 0x1fu;
    uint32_t pc = ctx->r[15];

    if (mode == 0x17) {
        if ((flags & 2u) == 0 || (flags & 1u))
            kind = "prefetch abort";
        else
            kind = "data abort";
    }

    consoleDemoInit();
    BG_PALETTE_SUB[0] = RGB15(0, 0, 0);
    BG_PALETTE_SUB[255] = RGB15(31, 31, 31);
    iprintf("PokeVault stopped.\n\n");
    iprintf("%s\n\n", kind);
    iprintf("pc %08lX\n", (unsigned long)pc);
    iprintf("lr %08lX\n\n", (unsigned long)ctx->r[14]);
    iprintf("Power off, then send\nthis screen.\n");
    while (1)
        swiWaitForVBlank();
}

int main(void)
{
    setExceptionHandler(on_fault);
    ui_init();
    /* Sprites and music live in the ROM filesystem. Saves stay on the SD card. */
    nitroFSInit(NULL);
    music_init();
    music_pump();
    if (!fatInitDefault())
        ui_fail("Could not open the SD card.");

    ui_status("Scanning the card...");
    dex_clear(&dex);
    scan_saves(&dex, ui_status);
    ui_run(&dex);
    return 0;
}
