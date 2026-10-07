#ifndef POKEVAULT_SPRITES_H
#define POKEVAULT_SPRITES_H

void sprites_init(void);
void sprites_hide(void);
void sprites_show(unsigned species, int shiny, int egg);
/* Centered on the top screen. dy shifts it for the idle float. Hides the bottom sprite. */
void sprites_show_top(unsigned species, int shiny, int egg, int dy);
/* Drops the top-screen sprite without forgetting the bottom one. */
void sprites_hide_top(void);
void sprites_flush(void);

#endif
