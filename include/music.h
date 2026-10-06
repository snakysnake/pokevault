#ifndef POKEVAULT_MUSIC_H
#define POKEVAULT_MUSIC_H

/* Lake, then Eterna Forest, then Lake again. Call after nitroFSInit. */
void music_init(void);

/* Refill the stream. Safe before music_init. */
void music_pump(void);

#endif
