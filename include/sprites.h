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

/* One Pokémon, 32x32, into a 256-wide direct-color picture (bit 15 set).
   Hardware sprites share 16 palettes of 256 colors, which is short of a
   30-slot box, so the grid is this picture rather than OAM. The art is
   the same bank sprites_show uses, shiny included. An egg with no image
   is drawn as an egg mark. */
void sprites_blit(unsigned short *bmp, int x, int y, unsigned species, int shiny, int egg);
/* Party (6) and one box (30) of decoded pictures. key is the save and box.
   The same key keeps what was already decoded. Call keep for each occupied
   slot before blit, so a new species does not drop one this box still shows. */
void sprites_box_use(int party, int key);
void sprites_box_keep(unsigned species, int shiny, int egg);
/* Hand that sits on the selected box slot. A sprite, so it can move on its own. */
void sprites_hand(int x, int y);
void sprites_hand_nudge(int dy);
void sprites_hand_hide(void);

#endif
