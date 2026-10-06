#ifndef POKEVAULT_SCAN_H
#define POKEVAULT_SCAN_H

#include "save.h"

typedef void (*ScanProgress)(const char *msg);

/* Walks the SD card for .sav files and appends them to dex. Read-only. */
void scan_saves(Dex *dex, ScanProgress progress);

#endif
