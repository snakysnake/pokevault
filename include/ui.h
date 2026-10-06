#ifndef POKEVAULT_UI_H
#define POKEVAULT_UI_H

#include "save.h"

void ui_init(void);
void ui_status(const char *msg);
void ui_fail(const char *msg);
void ui_run(Dex *dex, void (*rescan)(Dex *dex));

#endif
