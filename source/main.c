#include "music.h"
#include "scan.h"
#include "ui.h"

#include <fat.h>
#include <filesystem.h>

static Dex dex;

int main(void)
{
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
