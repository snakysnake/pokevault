#include "scan.h"
#include "ui.h"

#include <fat.h>
#include <filesystem.h>

static Dex dex;

static void rescan(Dex *list)
{
    dex_clear(list);
    scan_saves(list, ui_status);
}

int main(void)
{
    ui_init();
    /* Sprites live in the ROM filesystem. Saves stay on the SD card. */
    nitroFSInit(NULL);
    if (!fatInitDefault())
        ui_fail("Could not open the SD card.");

    ui_status("Scanning the card...");
    rescan(&dex);
    ui_run(&dex, rescan);
    return 0;
}
