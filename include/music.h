#ifndef POKEVAULT_MUSIC_H
#define POKEVAULT_MUSIC_H

/* Lake, then Eterna Forest, then Lake again. Call after nitroFSInit. */
void music_init(void);

/* Refill the stream, and sleep while the lid is closed. Safe before music_init. */
void music_pump(void);

/* Short cursor tick, in the style of the Gen 4 PC box. Safe before music_init. */
void music_click(void);

#endif
