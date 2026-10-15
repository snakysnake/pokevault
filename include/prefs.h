#ifndef POKEVAULT_PREFS_H
#define POKEVAULT_PREFS_H

#include "save.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* PokeVault settings on the card, at _nds/pokevault/settings.
   Pokémon saves are never opened for writing.

   The file is a little-endian container of typed records, so a later
   kind of setting can be added without a new file and without breaking
   a build that does not know that kind yet.

     0  4   magic "PVST"
     4  2   version (1)
     6  2   header_size, bytes after this field and before the records
     8  ..  header
     then records to the end of the file

   Version 1 header, header_size 8:
     0  4   flags, write 0
     4  2   CRC-16/CCITT of the record bytes
     6  2   reserved, write 0

   A larger header_size is allowed. Extra header bytes are skipped.
   The CRC stays at byte 4 of the header and covers only the records.

   Record:
     0  2   type
     2  2   payload size
     4  ..  payload

   Type numbers are never reused. Unknown types are copied through and
   written back. A version other than 1 is left untouched, so a future
   container change is not rewritten by an older build.

   Type 1, hidden game: the save path, with no trailing NUL. */

enum {
    PREFS_TYPE_HIDDEN = 1,
    PREFS_HIDDEN_MAX = 64,
    PREFS_EXTRA_MAX = 8192
};

typedef struct {
    char path[SAVE_PATH_MAX];
} PrefsHidden;

typedef struct {
    PrefsHidden hidden[PREFS_HIDDEN_MAX];
    int hidden_count;
    /* Record bytes this build does not interpret, kept in file order. */
    uint8_t extra[PREFS_EXTRA_MAX];
    int extra_len;
} Prefs;

/* False when the image is missing, torn, or a newer container. On failure
   prefs is cleared. */
bool prefs_decode(Prefs *prefs, const uint8_t *data, size_t len);

/* Bytes written, or 0 when the image does not fit. */
size_t prefs_encode(const Prefs *prefs, uint8_t *out, size_t cap);

/* Turns matching saves off. Other entries are left as the caller set them.
   Comparison ignores letter case. */
void prefs_apply(const Prefs *prefs, const Dex *dex, int *shown, int count);

/* Rebuilds the hidden list from the saves that are switched off. A hidden
   path that is not on the card this boot is kept. extra is left as it was. */
void prefs_capture(Prefs *prefs, const Dex *dex, const int *shown, int count);

/* Reads _nds/pokevault/settings from the card. A missing file yields an
   empty prefs and still allows a later commit. A file this build cannot
   safely rewrite is left on the card, and commit then does nothing. */
bool prefs_load(Prefs *prefs);
bool prefs_commit(const Prefs *prefs);

#endif
