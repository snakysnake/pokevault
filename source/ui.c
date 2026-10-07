#include "flavor.h"
#include "moves.h"
#include "music.h"
#include "save.h"
#include "species.h"
#include "sprites.h"
#include "stats.h"
#include "types.h"

#include <nds.h>
#include <stdio.h>
#include <string.h>

/* Both screens are a 30x22 text window inside a one-tile frame.
   Palette entry 15 of each 4bpp palette is a text color. The frame
   uses palette 0 entries 1 (panel) and 2 (gold edge).
   The list is the full National Dex, species 1 through 649. */

enum {
    COLS = 30,
    ROWS = 22,
    DEX_PAGE = 20,
    COPY_PAGE = 20,
    VIEW_DEX = 0,
    VIEW_COPIES = 1,
    VIEW_FILTER = 2,
    VIEW_MOVE = 3,
    PAGE_HOME = 0,
    PAGE_DEX = 1,
    PAGE_GAMES = 2,
    PAGE_PROGRESS = 3,
    GAME_PAGE = 7,
    FILTER_CAUGHT = 0,
    FILTER_SEEN = 1,
    FILTER_DEX = 2,
    FILTER_SHINY = 3,
    FILTER_GAMES = 4,
    FILTER_MODE = 5,
    FILTER_ROOT = 6,
    FILTER_PAGE = 20,
    PANE_ROOT = 0,
    PANE_GAMES = 1,
    PANE_MODE = 2,
    DEX_ALL = 0,
    DEX_KANTO = 1,
    DEX_JOHTO = 2,
    DEX_HOENN = 3,
    DEX_SINNOH = 4,
    DEX_UNOVA = 5,
    DEX_MODES = 6,
    INK_SHINY = 6,
    INK_MUTED = 8,
    INK_GOLD = 11,
    INK_CREAM = 15,
    GLYPH_BAR = 0x10,
    GLYPH_RULE = 0x19,
    GLYPH_BOX = 0x1A,
    GLYPH_BOX_X = 0x1B,
    CARD_STATS = 0,
    CARD_ENTRY = 1,
    CARD_WEAK = 2,
    CARD_PAGES = 3,
    /* Idle long enough to read as "put the DS down", then a few seconds on each Pokémon. */
    SAVER_IDLE = 20 * 60,
    SAVER_HOLD = 4 * 60
};

/* Glyphs 0x11-0x18 are stat bars. 0x19 is the title rule.
   Built by tools/build_font.py. */
static uint8_t font_1bpp[256 * 8] = {
#include "font.inc"
};

static PrintConsole top_console;
static PrintConsole bottom_console;
static SpeciesRow catalog[NATIONAL_DEX];
static SpeciesRow rows[NATIONAL_DEX];
static int row_count;
static int dex_cursor;
static int dex_scroll;
static int copy_cursor;
static int copy_scroll;
static int copy_page;
static int move_cursor;
static int page = PAGE_HOME;
static int home_cursor;
static int game_cursor;
static int game_scroll;
static int view = VIEW_DEX;
static int filter_caught;
static int filter_seen;
static int filter_dex;
static int filter_shiny;
static int filter_version[MAX_SAVES];
static int filter_pane;
static int filter_cursor;
static int filter_scroll;
static int game_filter_cursor;
static int game_filter_scroll;
static int mode_cursor;
static int mode_scroll;
static int dex_mode;
static int list_span_caught;
static int idle_frames;
static int saver_on;
static int saver_mon = -1;
static int saver_hold;
static int saver_tick;
static uint32_t saver_rng = 0x6D2B79F5u;

static const char *const ink_code[16] = {
    "\x1b[30;0m", "\x1b[31;0m", "\x1b[32;0m", "\x1b[33;0m",
    "\x1b[34;0m", "\x1b[35;0m", "\x1b[36;0m", "\x1b[37;0m",
    "\x1b[30;1m", "\x1b[31;1m", "\x1b[32;1m", "\x1b[33;1m",
    "\x1b[34;1m", "\x1b[35;1m", "\x1b[36;1m", "\x1b[37;1m"
};

static const uint8_t type_palette[TYPE_COUNT] = {
    7, 9, 12, 5, 3, 3, 10, 5, 0, 1, 4, 2, 11, 13, 6, 14, 8
};

static void use_ink(int pal)
{
    fputs(ink_code[pal & 15], stdout);
}

static void at(int row, int col)
{
    printf("\x1b[%d;%dH", row, col);
}

static int text_len(const char *text)
{
    int n = 0;
    if (!text)
        return 0;
    while (text[n])
        n++;
    return n;
}

static void emit(int pal, const char *text, int width)
{
    int n = 0;
    use_ink(pal);
    if (!text)
        text = "";
    while (text[n] && n < width) {
        putchar((unsigned char)text[n]);
        n++;
    }
    while (n < width) {
        putchar(' ');
        n++;
    }
}

static void emit_right(int pal, const char *text, int width)
{
    int len = text_len(text);
    int i;
    if (len > width) {
        text += len - width;
        len = width;
    }
    use_ink(pal);
    for (i = 0; i < width - len; i++)
        putchar(' ');
    for (i = 0; i < len; i++)
        putchar((unsigned char)text[i]);
}

static void load_font(PrintConsole *console)
{
    ConsoleFont font;
    memset(&font, 0, sizeof font);
    font.gfx = (u16 *)font_1bpp;
    font.bpp = 1;
    font.numChars = 256;
    font.convertSingleColor = true;
    consoleSetFont(console, &font);
}

static int frame_pixel(int kind, int x, int y)
{
    int rim = 0;
    int gold = 0;
    switch (kind) {
    case 1:
        rim = y == 0;
        gold = y == 1 || y == 2;
        break;
    case 2:
        rim = y == 7;
        gold = y == 5 || y == 6;
        break;
    case 3:
        rim = x == 0;
        gold = x == 1 || x == 2;
        break;
    case 4:
        rim = x == 7;
        gold = x == 5 || x == 6;
        break;
    case 5:
        rim = x == 0 || y == 0;
        gold = (x <= 2 || y <= 2) && !rim;
        break;
    case 6:
        rim = x == 7 || y == 0;
        gold = (x >= 5 || y <= 2) && !rim;
        break;
    case 7:
        rim = x == 0 || y == 7;
        gold = (x <= 2 || y >= 5) && !rim;
        break;
    case 8:
        rim = x == 7 || y == 7;
        gold = (x >= 5 || y >= 5) && !rim;
        break;
    default:
        return 1;
    }
    if (rim)
        return 0;
    if (gold)
        return 2;
    return 1;
}

static void write_frame_tile(u16 *gfx, int kind)
{
    u16 *tile = gfx + kind * 16;
    int y;
    int x;
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x += 4) {
            int p0 = frame_pixel(kind, x, y);
            int p1 = frame_pixel(kind, x + 1, y);
            int p2 = frame_pixel(kind, x + 2, y);
            int p3 = frame_pixel(kind, x + 3, y);
            tile[y * 2 + x / 4] = (u16)(p0 | (p1 << 4) | (p2 << 8) | (p3 << 12));
        }
    }
}

static void build_frame(int console_id, int sub)
{
    int id;
    u16 *gfx;
    u16 *map;
    int x;
    int y;
    int kind;

    if (sub)
        id = bgInitSub(1, BgType_Text4bpp, BgSize_T_256x256, 30, 1);
    else
        id = bgInit(1, BgType_Text4bpp, BgSize_T_256x256, 30, 1);
    gfx = bgGetGfxPtr(id);
    map = bgGetMapPtr(id);
    for (kind = 0; kind < 9; kind++)
        write_frame_tile(gfx, kind);
    for (y = 0; y < 32; y++) {
        for (x = 0; x < 32; x++) {
            int top = y == 0;
            int bottom = y == 23;
            int left = x == 0;
            int right = x == 31;
            kind = 0;
            if (y < 24) {
                if (top && left)
                    kind = 5;
                else if (top && right)
                    kind = 6;
                else if (bottom && left)
                    kind = 7;
                else if (bottom && right)
                    kind = 8;
                else if (top)
                    kind = 1;
                else if (bottom)
                    kind = 2;
                else if (left)
                    kind = 3;
                else if (right)
                    kind = 4;
            }
            map[y * 32 + x] = (u16)kind;
        }
    }
    /* Priority 0 is the front. Text sits above the panel, and the
       transparent pixels inside each glyph show the panel behind it. */
    bgSetPriority(console_id, 0);
    bgSetPriority(id, 3);
}

static void paint_palette(u16 *pal)
{
    static const u16 ink[16] = {
        RGB15(22, 24, 28),
        RGB15(31, 15, 6),
        RGB15(14, 26, 8),
        RGB15(28, 22, 10),
        RGB15(10, 18, 31),
        RGB15(24, 10, 24),
        RGB15(14, 30, 31),
        RGB15(26, 24, 20),
        RGB15(18, 22, 28),
        RGB15(31, 10, 8),
        RGB15(24, 26, 8),
        RGB15(31, 26, 6),
        RGB15(20, 18, 31),
        RGB15(31, 12, 18),
        RGB15(16, 10, 31),
        RGB15(31, 30, 26)
    };
    int i;
    for (i = 0; i < 256; i++)
        pal[i] = 0;
    for (i = 0; i < 16; i++)
        pal[i * 16 + 15] = ink[i];
    pal[1] = RGB15(4, 4, 4);
    pal[2] = RGB15(28, 22, 8);
}

static void setup_consoles(void)
{
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(&top_console, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, false);
    consoleInit(&bottom_console, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, false);
    load_font(&top_console);
    load_font(&bottom_console);
    consoleSetWindow(&top_console, 1, 1, COLS, ROWS);
    consoleSetWindow(&bottom_console, 1, 1, COLS, ROWS);
    paint_palette(BG_PALETTE);
    paint_palette(BG_PALETTE_SUB);
    build_frame(top_console.bgId, 0);
    build_frame(bottom_console.bgId, 1);
    setBackdropColor(RGB15(0, 0, 0));
    setBackdropColorSub(RGB15(0, 0, 0));
}

static int type_ink(unsigned type)
{
    if (type >= TYPE_COUNT)
        return INK_CREAM;
    return type_palette[type];
}

static void title_row(int row, const char *left, const char *right)
{
    int n = 0;
    int rlen;
    int gap;
    int room;
    at(row, 0);
    if (!left)
        left = "";
    if (!right)
        right = "";
    use_ink(INK_CREAM);
    while (left[n] && n < COLS - 8) {
        putchar((unsigned char)left[n]);
        n++;
    }
    room = COLS - n;
    rlen = text_len(right);
    if (rlen >= room)
        rlen = room > 0 ? room : 0;
    gap = room - rlen;
    use_ink(INK_MUTED);
    while (gap-- > 0)
        putchar(' ');
    emit(INK_GOLD, right, rlen);
}

static void rule_row(int row)
{
    int i;
    at(row, 0);
    use_ink(INK_MUTED);
    for (i = 0; i < COLS; i++)
        putchar(GLYPH_RULE);
}

static void stat_bar(int value, int max_value)
{
    int px;
    int i;
    if (max_value < 1)
        max_value = 1;
    if (value < 0)
        value = 0;
    if (value > max_value)
        value = max_value;
    px = value * 64 / max_value;
    for (i = 0; i < 8; i++) {
        int n = px > 8 ? 8 : px;
        px -= n;
        if (n <= 0)
            putchar(' ');
        else
            putchar(GLYPH_BAR + n);
    }
}

static const SaveInfo *save_of(const Dex *dex, const MonRef *mon)
{
    if (!dex || !mon || mon->save_index >= dex->save_count)
        return NULL;
    return &dex->saves[mon->save_index];
}

static void where_of(const MonRef *mon, char *out, size_t cap)
{
    if (mon->flags & MON_PARTY)
        snprintf(out, cap, "Party %u", (unsigned)mon->slot + 1);
    else
        snprintf(out, cap, "Box %u", (unsigned)mon->box + 1);
}

static int dex_bit(const uint8_t *bits, int species)
{
    int bit;
    if (species < 1 || species > NATIONAL_DEX)
        return 0;
    bit = species - 1;
    return (bits[bit >> 3] >> (bit & 7)) & 1;
}

static int species_in_dex(const Dex *dex, int species, int want_caught)
{
    int i;
    for (i = 0; i < dex->save_count; i++) {
        const uint8_t *bits = want_caught ? dex->saves[i].dex_caught : dex->saves[i].dex_seen;
        if (dex_bit(bits, species))
            return 1;
    }
    return 0;
}

static int versions_narrowed(const Dex *dex)
{
    int i;
    if (!dex)
        return 0;
    for (i = 0; i < dex->save_count; i++) {
        if (!filter_version[i])
            return 1;
    }
    return 0;
}

static int version_on(const Dex *dex, int save_index)
{
    if (!dex || save_index < 0 || save_index >= dex->save_count)
        return 0;
    return filter_version[save_index] != 0;
}

/* Counts living copies and Pokédex flags on versions that are still enabled. */
static void measure_species(const Dex *dex, const SpeciesRow *row, int *stored, int *shiny,
                            int *seen, int *in_dex)
{
    int i;
    *stored = 0;
    *shiny = 0;
    *seen = 0;
    *in_dex = 0;
    if (!dex || !row)
        return;
    for (i = 0; i < row->count; i++) {
        const MonRef *mon = &dex->mons[row->first + i];
        if (!version_on(dex, mon->save_index))
            continue;
        (*stored)++;
        if (mon->flags & MON_SHINY)
            *shiny = 1;
    }
    for (i = 0; i < dex->save_count; i++) {
        if (!filter_version[i])
            continue;
        if (dex_bit(dex->saves[i].dex_caught, row->species))
            *in_dex = 1;
        if (dex_bit(dex->saves[i].dex_seen, row->species))
            *seen = 1;
    }
}

static int species_from_disabled_only(const Dex *dex, const SpeciesRow *row)
{
    int i;
    int any = 0;
    int enabled = 0;
    if (!versions_narrowed(dex))
        return 0;
    for (i = 0; i < row->count; i++) {
        const MonRef *mon = &dex->mons[row->first + i];
        any = 1;
        if (version_on(dex, mon->save_index))
            enabled = 1;
    }
    for (i = 0; i < dex->save_count && !enabled; i++) {
        int got = dex_bit(dex->saves[i].dex_caught, row->species);
        int saw = dex_bit(dex->saves[i].dex_seen, row->species);
        if (!got && !saw)
            continue;
        any = 1;
        if (filter_version[i])
            enabled = 1;
    }
    return any && !enabled;
}

static const char *dex_mode_name(int mode)
{
    static const char *const names[DEX_MODES] = {
        "All", "Kanto", "Johto", "Hoenn", "Sinnoh", "Unova"
    };
    if (mode < 0 || mode >= DEX_MODES)
        return names[0];
    return names[mode];
}

/* National numbers for the species introduced in that region. */
static void mode_range(int mode, int *first, int *last)
{
    switch (mode) {
    case DEX_KANTO:
        *first = 1;
        *last = 151;
        break;
    case DEX_JOHTO:
        *first = 152;
        *last = 251;
        break;
    case DEX_HOENN:
        *first = 252;
        *last = 386;
        break;
    case DEX_SINNOH:
        *first = 387;
        *last = 493;
        break;
    case DEX_UNOVA:
        *first = 494;
        *last = NATIONAL_DEX;
        break;
    default:
        *first = 1;
        *last = NATIONAL_DEX;
        break;
    }
}

static int species_in_mode(int species)
{
    int first;
    int last;
    mode_range(dex_mode, &first, &last);
    return species >= first && species <= last;
}

static int mode_span(void)
{
    int first;
    int last;
    mode_range(dex_mode, &first, &last);
    return last - first + 1;
}

static void apply_filter(const Dex *dex)
{
    int i;
    int n = 0;
    int owned = 0;
    for (i = 0; i < NATIONAL_DEX; i++) {
        int stored;
        int shiny;
        int seen;
        int in_dex;
        if (!species_in_mode(catalog[i].species))
            continue;
        measure_species(dex, &catalog[i], &stored, &shiny, &seen, &in_dex);
        if (stored > 0)
            owned++;
        if (filter_caught && stored == 0)
            continue;
        if (filter_seen && !seen)
            continue;
        if (filter_dex && !in_dex)
            continue;
        if (filter_shiny && !shiny)
            continue;
        if (species_from_disabled_only(dex, &catalog[i]))
            continue;
        rows[n++] = catalog[i];
    }
    row_count = n;
    list_span_caught = owned;
}

static int enabled_stored(const Dex *dex, const SpeciesRow *row)
{
    int stored;
    int shiny;
    int seen;
    int in_dex;
    measure_species(dex, row, &stored, &shiny, &seen, &in_dex);
    return stored;
}

static const MonRef *enabled_mon(const Dex *dex, const SpeciesRow *row, int nth)
{
    int i;
    int n = 0;
    for (i = 0; i < row->count; i++) {
        const MonRef *mon = &dex->mons[row->first + i];
        if (!version_on(dex, mon->save_index))
            continue;
        if (n == nth)
            return mon;
        n++;
    }
    return NULL;
}

static void refresh_rows(const Dex *dex)
{
    int mon = 0;
    int i;

    for (i = 1; i <= NATIONAL_DEX; i++) {
        int count = 0;
        int first;
        while (mon < dex->mon_count && dex->mons[mon].species < (uint16_t)i)
            mon++;
        first = mon;
        while (mon < dex->mon_count && dex->mons[mon].species == (uint16_t)i) {
            count++;
            mon++;
        }
        catalog[i - 1].species = (uint16_t)i;
        catalog[i - 1].count = (uint16_t)count;
        catalog[i - 1].first = (uint16_t)first;
        catalog[i - 1].dex_caught = (uint8_t)species_in_dex(dex, i, 1);
        catalog[i - 1].dex_seen = (uint8_t)species_in_dex(dex, i, 0);
    }
    apply_filter(dex);
}

static void clamp_cursor(int *cursor, int *scroll, int count, int page)
{
    if (count <= 0) {
        *cursor = 0;
        *scroll = 0;
        return;
    }
    if (*cursor < 0)
        *cursor = 0;
    if (*cursor >= count)
        *cursor = count - 1;
    if (*cursor < *scroll)
        *scroll = *cursor;
    if (*cursor >= *scroll + page)
        *scroll = *cursor - page + 1;
}

static void nudge(int *cursor, int count, int page, uint32_t down)
{
    int step = page > 4 ? 5 : 1;
    if (count <= 0)
        return;
    if (down & KEY_UP)
        *cursor -= 1;
    if (down & KEY_DOWN)
        *cursor += 1;
    if (down & KEY_LEFT)
        *cursor -= step;
    if (down & KEY_RIGHT)
        *cursor += step;
    if (down & KEY_L)
        *cursor -= page;
    if (down & KEY_R)
        *cursor += page;
}

static void select_species(uint16_t species)
{
    int i;
    dex_cursor = 0;
    for (i = 0; i < row_count; i++) {
        if (rows[i].species == species) {
            dex_cursor = i;
            break;
        }
    }
    clamp_cursor(&dex_cursor, &dex_scroll, row_count, DEX_PAGE);
}

static const MonRef *best_mon(const Dex *dex, const SpeciesRow *row)
{
    const MonRef *best = NULL;
    const MonRef *fallback = NULL;
    int i;
    for (i = 0; i < row->count; i++) {
        const MonRef *mon = &dex->mons[row->first + i];
        int shiny;
        int best_shiny;
        if (!version_on(dex, mon->save_index))
            continue;
        if (!fallback)
            fallback = mon;
        if (mon->flags & MON_EGG)
            continue;
        shiny = (mon->flags & MON_SHINY) != 0;
        best_shiny = best && (best->flags & MON_SHINY) != 0;
        if (!best || mon->level > best->level || (mon->level == best->level && shiny && !best_shiny))
            best = mon;
    }
    if (!best)
        best = fallback;
    return best;
}

static void draw_dex_row(const Dex *dex, int row, const SpeciesRow *entry, int selected)
{
    char num[8];
    char qty[16];
    int stored;
    int seen;
    int in_dex;
    int ignore;
    int ink;
    int name_ink;
    measure_species(dex, entry, &stored, &ignore, &seen, &in_dex);
    (void)ignore;
    ink = selected ? INK_GOLD : INK_MUTED;
    name_ink = selected ? INK_GOLD : (stored > 0 ? INK_CREAM : INK_MUTED);
    qty[0] = 0;
    snprintf(num, sizeof num, "#%03u", entry->species);
    if (stored > 0)
        snprintf(qty, sizeof qty, "x%u", (unsigned)stored);
    else if (in_dex)
        snprintf(qty, sizeof qty, "Dex");
    else if (seen)
        snprintf(qty, sizeof qty, "Seen");
    else
        snprintf(qty, sizeof qty, "-");
    at(row, 0);
    emit(ink, selected ? ">" : "", 2);
    emit(ink, num, 5);
    emit(name_ink, species_name(entry->species), 17);
    emit_right(ink, qty, 6);
}

static void draw_copy_row(const MonRef *mon, int row, int selected)
{
    char level[8];
    char where[12];
    int ink = selected ? INK_GOLD : INK_CREAM;
    int egg = (mon->flags & MON_EGG) != 0;
    int shiny = (mon->flags & MON_SHINY) != 0;
    const char *nature = "";
    where_of(mon, where, sizeof where);
    if (egg)
        snprintf(level, sizeof level, "Egg");
    else {
        snprintf(level, sizeof level, "Lv%u", mon->level);
        nature = nature_name(mon->nature);
    }
    at(row, 0);
    emit(ink, selected ? ">" : "", 2);
    emit(ink, level, 6);
    emit(shiny ? INK_SHINY : ink, shiny ? "Shiny" : "", 6);
    emit(ink, nature, 8);
    emit(ink, where, 8);
}

static const char *list_heading(const Dex *dex)
{
    int n = (filter_caught ? 1 : 0) + (filter_seen ? 1 : 0) + (filter_dex ? 1 : 0)
        + (filter_shiny ? 1 : 0) + (versions_narrowed(dex) ? 1 : 0);
    if (n == 0)
        return dex_mode == DEX_ALL ? "Pokedex" : dex_mode_name(dex_mode);
    if (n > 1 || dex_mode != DEX_ALL)
        return "Filtered";
    if (filter_caught)
        return "Caught";
    if (filter_seen)
        return "Seen";
    if (filter_dex)
        return "In Dex";
    if (filter_shiny)
        return "Shiny";
    return "Games";
}

static void draw_dex_list(const Dex *dex)
{
    char right[12];
    const char *heading = list_heading(dex);
    int shown = (filter_caught || filter_seen || filter_dex || filter_shiny)
        ? row_count : list_span_caught;
    int i;
    int last;
    snprintf(right, sizeof right, "%d/%d", shown, mode_span());
    title_row(0, heading, right);
    rule_row(1);
    if (row_count == 0) {
        at(3, 0);
        use_ink(INK_MUTED);
        fputs("Nothing matches.", stdout);
        return;
    }
    last = dex_scroll + DEX_PAGE;
    if (last > row_count)
        last = row_count;
    for (i = dex_scroll; i < last; i++)
        draw_dex_row(dex, 2 + (i - dex_scroll), &rows[i], i == dex_cursor);
}

static void draw_copy_list(const Dex *dex, const SpeciesRow *row)
{
    char left[32];
    char right[24];
    int total = enabled_stored(dex, row);
    int i;
    int last;
    snprintf(left, sizeof left, "#%03u %s", row->species, species_name(row->species));
    if (total > 0)
        snprintf(right, sizeof right, "%d/%d", copy_cursor + 1, total);
    else
        snprintf(right, sizeof right, "0");
    title_row(0, left, right);
    rule_row(1);
    last = copy_scroll + COPY_PAGE;
    if (last > total)
        last = total;
    for (i = copy_scroll; i < last; i++) {
        const MonRef *mon = enabled_mon(dex, row, i);
        if (mon)
            draw_copy_row(mon, 2 + (i - copy_scroll), i == copy_cursor);
    }
}

/* The sprite sits on the right of these text rows. */
static int line_cols(int row)
{
    if (row >= 2 && row <= 9)
        return 22;
    return COLS;
}

static void draw_types_at(int row, unsigned type1, unsigned type2)
{
    at(row, 0);
    use_ink(type_ink(type1));
    fputs(type_name(type1), stdout);
    if (type2 < TYPE_COUNT) {
        putchar(' ');
        use_ink(type_ink(type2));
        fputs(type_name(type2), stdout);
    }
}

static void draw_check(int row, int done, int ink, const char *label)
{
    at(row, 0);
    use_ink(done ? ink : INK_CREAM);
    putchar(done ? GLYPH_BOX_X : GLYPH_BOX);
    putchar(' ');
    use_ink(done ? ink : INK_MUTED);
    fputs(label, stdout);
}

/* Shiny implies caught, caught implies the Dex, and the Dex implies seen. */
static void draw_completion(int row, int shiny, int stored, int in_dex, int seen)
{
    int caught = stored > 0;
    draw_check(row, shiny, INK_SHINY, "Shiny");
    draw_check(row + 1, caught, INK_GOLD, "Caught");
    draw_check(row + 2, in_dex || caught, INK_CREAM, "In Dex");
    draw_check(row + 3, seen || in_dex || caught, INK_CREAM, "Seen");
}

static void draw_species_card(const Dex *dex, const SpeciesRow *row)
{
    const MonRef *face;
    int stored = 0;
    int shiny = 0;
    int seen = 0;
    int in_dex = 0;
    int eggs = 0;
    int i;
    int line = 3;
    char buf[16];

    face = best_mon(dex, row);
    measure_species(dex, row, &stored, &shiny, &seen, &in_dex);
    for (i = 0; i < row->count; i++) {
        const MonRef *mon = &dex->mons[row->first + i];
        if (version_on(dex, mon->save_index) && (mon->flags & MON_EGG))
            eggs++;
    }

    at(0, 0);
    use_ink(INK_GOLD);
    printf("#%03u ", row->species);
    use_ink(INK_CREAM);
    fputs(species_name(row->species), stdout);

    draw_types_at(1, species_type(row->species, 0), species_type2(row->species, 0));
    if (stored > 0) {
        snprintf(buf, sizeof buf, "x%d", stored);
        at(1, COLS - text_len(buf));
        use_ink(INK_GOLD);
        fputs(buf, stdout);
    }

    /* Same row for every species, so the checklist below does not jump. */
    at(line, 0);
    use_ink(INK_MUTED);
    fputs("Best", stdout);
    if (face && (face->flags & MON_EGG) == 0) {
        use_ink(INK_CREAM);
        printf(" Lv %u", face->level);
    } else {
        use_ink(INK_MUTED);
        fputs(" Lv -", stdout);
    }
    if (face)
        sprites_show(face->species, (face->flags & MON_SHINY) != 0, (face->flags & MON_EGG) != 0);
    else
        sprites_show(row->species, 0, 0);

    draw_completion(line + 2, shiny, stored, in_dex, seen);
    if (eggs > 0) {
        at(line + 7, 0);
        use_ink(INK_MUTED);
        printf("Eggs x%d", eggs);
    }
    if (!face && dex->save_count == 0) {
        at(line + 7, 0);
        use_ink(INK_MUTED);
        fputs("Put .sav files in", stdout);
        at(line + 8, 0);
        fputs("roms/nds/saves", stdout);
        at(line + 9, 0);
        fputs("or roms/gba.", stdout);
    }
}

static const char *ball_name(unsigned ball)
{
    static const char *const names[] = {
        "-",
        "Master Ball",
        "Ultra Ball",
        "Great Ball",
        "Poke Ball",
        "Safari Ball",
        "Net Ball",
        "Dive Ball",
        "Nest Ball",
        "Repeat Ball",
        "Timer Ball",
        "Luxury Ball",
        "Premier Ball",
        "Dusk Ball",
        "Heal Ball",
        "Quick Ball",
        "Cherish Ball",
        "Fast Ball",
        "Level Ball",
        "Lure Ball",
        "Heavy Ball",
        "Love Ball",
        "Friend Ball",
        "Moon Ball",
        "Sport Ball",
        "Park Ball",
        "Dream Ball"
    };
    if (ball >= sizeof names / sizeof names[0])
        return "Ball";
    return names[ball];
}

static void format_met(const MonRef *mon, char *out, size_t cap)
{
    if (mon->met_month < 1 || mon->met_month > 12 || mon->met_day < 1 || mon->met_day > 31
        || mon->met_year > 99) {
        snprintf(out, cap, "-");
        return;
    }
    snprintf(out, cap, "%04u-%02u-%02u", 2000u + mon->met_year, mon->met_month, mon->met_day);
}

static void draw_move(int row, unsigned move)
{
    at(row, 0);
    if (!move || move_type(move) == TYPE_COUNT) {
        use_ink(INK_MUTED);
        fputs(move ? move_name(move) : "-", stdout);
        return;
    }
    use_ink(type_ink(move_type(move)));
    fputs(move_name(move), stdout);
}

static void draw_species_title(unsigned species, unsigned form, int shiny)
{
    at(0, 0);
    use_ink(INK_CREAM);
    emit(INK_CREAM, species_name(species), 22);
    if (shiny) {
        use_ink(INK_SHINY);
        fputs("Shiny", stdout);
    }
    draw_types_at(1, species_type(species, form), species_type2(species, form));
}

static void draw_mon_title(const MonRef *mon)
{
    draw_species_title(mon->species, mon->form, (mon->flags & MON_SHINY) != 0);
}

static void draw_card_pager(int which)
{
    static const char *const names[CARD_PAGES] = {"Stats", "Entry", "Weak"};
    const char *name = names[which >= 0 && which < CARD_PAGES ? which : 0];
    int len = text_len(name);
    int start = (COLS - len) / 2;
    at(21, 0);
    use_ink(INK_MUTED);
    fputs("L", stdout);
    at(21, start);
    use_ink(INK_GOLD);
    fputs(name, stdout);
    at(21, COLS - 1);
    use_ink(INK_MUTED);
    fputs("R", stdout);
}

static int wrap_width(int row, int fixed)
{
    if (fixed > 0)
        return fixed;
    return line_cols(row);
}

static int draw_wrapped(int row, int last, const char *text, int fixed)
{
    int col = 0;
    if (!text)
        return row;
    while (*text && row <= last) {
        int word = 0;
        int width;
        while (*text == ' ')
            text++;
        while (text[word] && text[word] != ' ')
            word++;
        if (word == 0)
            break;
        width = wrap_width(row, fixed);
        if (col > 0 && col + 1 + word > width) {
            row++;
            col = 0;
            if (row > last)
                break;
            width = wrap_width(row, fixed);
        }
        if (word > width)
            word = width;
        if (col > 0) {
            at(row, col);
            use_ink(INK_CREAM);
            putchar(' ');
            col++;
        } else {
            at(row, 0);
            use_ink(INK_CREAM);
        }
        while (word > 0) {
            putchar((unsigned char)*text++);
            col++;
            word--;
        }
    }
    return row;
}

static int draw_type_chips(int row, const unsigned *types, int count)
{
    int i;
    int col = 0;
    for (i = 0; i < count && row <= 19; i++) {
        const char *name = type_name(types[i]);
        int len = text_len(name);
        int width = line_cols(row);
        if (col > 0 && col + 1 + len > width) {
            row++;
            col = 0;
            if (row > 19)
                break;
            width = line_cols(row);
        }
        if (col > 0)
            col++;
        at(row, col);
        use_ink(type_ink(types[i]));
        fputs(name, stdout);
        col += len;
    }
    return row + 1;
}

static void draw_copy_card(const Dex *dex, const MonRef *mon)
{
    const SaveInfo *info = save_of(dex, mon);
    uint16_t st[6];
    int peak;
    int i;
    int egg = (mon->flags & MON_EGG) != 0;
    char buf[40];
    static const char *const labels[6] = {"HP", "Atk", "Def", "SpA", "SpD", "Spe"};

    draw_mon_title(mon);
    at(2, 0);
    use_ink(INK_CREAM);
    if (egg)
        fputs("Egg", stdout);
    else {
        printf("Lv %u", mon->level);
        use_ink(INK_GOLD);
        printf("  %s", nature_name(mon->nature));
    }

    if (!egg) {
        mon_battle_stats(mon, st);
        peak = 1;
        for (i = 0; i < 6; i++) {
            if (st[i] > peak)
                peak = st[i];
        }
        for (i = 0; i < 6; i++) {
            at(3 + i, 0);
            use_ink(INK_MUTED);
            printf("%-3.3s ", labels[i]);
            use_ink(INK_CREAM);
            printf("%3u ", (unsigned)st[i]);
            use_ink(INK_GOLD);
            stat_bar(st[i], peak);
        }
        for (i = 0; i < 4; i++)
            draw_move(10 + i, mon->moves[i]);
    }

    at(15, 0);
    use_ink(INK_CREAM);
    fputs(ball_name(mon->ball), stdout);
    format_met(mon, buf, sizeof buf);
    at(16, 0);
    use_ink(INK_GOLD);
    fputs(buf, stdout);

    at(17, 0);
    use_ink(INK_CREAM);
    emit(INK_CREAM, info ? info->name : "?", COLS);
    at(18, 0);
    use_ink(INK_MUTED);
    if (mon->flags & MON_PARTY)
        snprintf(buf, sizeof buf, "%s   Party %u", info ? info->game : "?", (unsigned)mon->slot + 1);
    else
        snprintf(buf, sizeof buf, "%s   Box %u  slot %u",
                 info ? info->game : "?", (unsigned)mon->box + 1, (unsigned)mon->slot + 1);
    fputs(buf, stdout);
}

static const char *category_name(unsigned category)
{
    if (category == MOVE_PHYSICAL)
        return "Physical";
    if (category == MOVE_SPECIAL)
        return "Special";
    if (category == MOVE_STATUS)
        return "Status";
    return "-";
}

static int category_ink(unsigned category)
{
    if (category == MOVE_PHYSICAL)
        return 9;
    if (category == MOVE_SPECIAL)
        return 12;
    return INK_MUTED;
}

static void draw_meter(int row, const char *label, const char *value)
{
    at(row, 0);
    emit(INK_MUTED, label, 10);
    use_ink(INK_CREAM);
    fputs(value, stdout);
}

static void draw_move_card(unsigned move, int index, int total)
{
    MoveInfo info;
    char buf[32];
    const char *effect;
    const char *category;
    int known;
    int line = 3;

    known = move_info(move, &info);
    category = known ? category_name(info.category) : "-";
    at(0, 0);
    emit(type_ink(move_type(move)), move_name(move), total > 1 ? COLS - 4 : COLS);
    if (total > 1) {
        snprintf(buf, sizeof buf, "%d/%d", index + 1, total);
        at(0, COLS - text_len(buf));
        use_ink(INK_GOLD);
        fputs(buf, stdout);
    }
    at(1, 0);
    use_ink(type_ink(move_type(move)));
    fputs(type_name(move_type(move)), stdout);
    at(1, COLS - text_len(category));
    use_ink(category_ink(known ? info.category : 0));
    fputs(category, stdout);
    rule_row(2);

    if (!known) {
        at(line, 0);
        use_ink(INK_MUTED);
        fputs("No info.", stdout);
    } else {
        if (info.power)
            snprintf(buf, sizeof buf, "%u", info.power);
        else
            snprintf(buf, sizeof buf, "-");
        draw_meter(line++, "Power", buf);
        if (info.accuracy)
            snprintf(buf, sizeof buf, "%u%%", info.accuracy);
        else
            snprintf(buf, sizeof buf, "Sure");
        draw_meter(line++, "Accuracy", buf);
        snprintf(buf, sizeof buf, "%u", info.pp);
        draw_meter(line++, "PP", buf);
        if (info.priority) {
            if (info.priority > 0)
                snprintf(buf, sizeof buf, "+%d", info.priority);
            else
                snprintf(buf, sizeof buf, "%d", info.priority);
            draw_meter(line++, "Priority", buf);
        }
        effect = move_effect(move);
        if (effect && effect[0]) {
            line++;
            draw_wrapped(line, 19, effect, COLS);
        }
    }
    at(21, 0);
    use_ink(INK_MUTED);
    fputs("B back", stdout);
    if (total > 1) {
        at(21, COLS - 7);
        fputs("Up Down", stdout);
    }
}

static void draw_uncaught_stats(unsigned species)
{
    draw_species_title(species, 0, 0);
    at(3, 0);
    use_ink(INK_CREAM);
    fputs("Catch Pokemon to", stdout);
    at(4, 0);
    fputs("show stats.", stdout);
}

static void draw_copy_entry(unsigned species, unsigned form, int shiny)
{
    const char *text = species_flavor(species);
    draw_species_title(species, form, shiny);
    if (!text || !text[0]) {
        at(3, 0);
        use_ink(INK_MUTED);
        fputs("No entry.", stdout);
        return;
    }
    draw_wrapped(3, 19, text, 0);
}

static void draw_copy_weak(unsigned species, unsigned form, int shiny)
{
    unsigned type1 = species_type(species, form);
    unsigned type2 = species_type2(species, form);
    unsigned quad[TYPE_COUNT];
    unsigned doub[TYPE_COUNT];
    int quad_n = 0;
    int doub_n = 0;
    int attack;
    int line = 3;

    draw_species_title(species, form, shiny);
    for (attack = 0; attack < TYPE_COUNT; attack++) {
        int factor = type_effect((unsigned)attack, type1, type2);
        if (factor >= 16)
            quad[quad_n++] = (unsigned)attack;
        else if (factor >= 8)
            doub[doub_n++] = (unsigned)attack;
    }
    if (quad_n == 0 && doub_n == 0) {
        at(line, 0);
        use_ink(INK_CREAM);
        fputs("No weaknesses.", stdout);
        return;
    }
    if (quad_n > 0) {
        at(line++, 0);
        use_ink(INK_MUTED);
        fputs("Weak x4", stdout);
        line = draw_type_chips(line, quad, quad_n);
        line++;
    }
    if (doub_n > 0 && line <= 19) {
        at(line++, 0);
        use_ink(INK_MUTED);
        fputs("Weak x2", stdout);
        draw_type_chips(line, doub, doub_n);
    }
}

static void clamp_pane(int *cursor, int *scroll, int count)
{
    if (count < 1)
        count = 1;
    if (*cursor < 0)
        *cursor = 0;
    if (*cursor >= count)
        *cursor = count - 1;
    if (*cursor < *scroll)
        *scroll = *cursor;
    if (*cursor >= *scroll + FILTER_PAGE)
        *scroll = *cursor - FILTER_PAGE + 1;
    if (*scroll < 0)
        *scroll = 0;
}

static void clamp_filter(const Dex *dex)
{
    if (filter_pane == PANE_GAMES)
        clamp_pane(&game_filter_cursor, &game_filter_scroll, dex->save_count);
    else if (filter_pane == PANE_MODE)
        clamp_pane(&mode_cursor, &mode_scroll, DEX_MODES);
    else
        clamp_pane(&filter_cursor, &filter_scroll, FILTER_ROOT);
}

static void toggle_filter(int item)
{
    if (item == FILTER_CAUGHT)
        filter_caught = !filter_caught;
    else if (item == FILTER_SEEN)
        filter_seen = !filter_seen;
    else if (item == FILTER_DEX)
        filter_dex = !filter_dex;
    else if (item == FILTER_SHINY)
        filter_shiny = !filter_shiny;
}

static int games_enabled(const Dex *dex)
{
    int i;
    int on = 0;
    for (i = 0; i < dex->save_count; i++) {
        if (filter_version[i])
            on++;
    }
    return on;
}

static void draw_toggle_row(int screen_row, int selected, const char *label, const char *value, int active)
{
    int ink = selected ? INK_GOLD : INK_CREAM;
    at(screen_row, 0);
    emit(ink, selected ? ">" : "", 2);
    emit(ink, label, 16);
    emit_right(active ? INK_GOLD : INK_MUTED, value, 12);
}

static void draw_root_filter(const Dex *dex)
{
    char games[24];
    int i;
    int on = games_enabled(dex);
    if (dex->save_count <= 0)
        snprintf(games, sizeof games, "None");
    else if (on == dex->save_count)
        snprintf(games, sizeof games, "All");
    else if (on == 0)
        snprintf(games, sizeof games, "Off");
    else
        snprintf(games, sizeof games, "%d/%d", on, dex->save_count);
    title_row(0, "Filter", "");
    rule_row(1);
    for (i = 0; i < FILTER_ROOT; i++) {
        int row = 2 + i;
        int selected = i == filter_cursor;
        if (i == FILTER_CAUGHT)
            draw_toggle_row(row, selected, "Caught", filter_caught ? "On" : "Off", filter_caught);
        else if (i == FILTER_SEEN)
            draw_toggle_row(row, selected, "Seen", filter_seen ? "On" : "Off", filter_seen);
        else if (i == FILTER_DEX)
            draw_toggle_row(row, selected, "In Dex", filter_dex ? "On" : "Off", filter_dex);
        else if (i == FILTER_SHINY)
            draw_toggle_row(row, selected, "Shiny", filter_shiny ? "On" : "Off", filter_shiny);
        else if (i == FILTER_GAMES)
            draw_toggle_row(row, selected, "Games", games, dex->save_count > 0 && on != dex->save_count);
        else
            draw_toggle_row(row, selected, "Pokedex", dex_mode_name(dex_mode), dex_mode != DEX_ALL);
    }
}

static const char *profile_name(const SaveInfo *info);

static void draw_game_filters(const Dex *dex)
{
    int last;
    int i;
    char right[24];
    if (dex->save_count <= 0) {
        title_row(0, "Games", "");
        rule_row(1);
        at(3, 0);
        use_ink(INK_MUTED);
        fputs("No saves.", stdout);
        return;
    }
    snprintf(right, sizeof right, "%d/%d", games_enabled(dex), dex->save_count);
    title_row(0, "Games", right);
    rule_row(1);
    last = game_filter_scroll + FILTER_PAGE;
    if (last > dex->save_count)
        last = dex->save_count;
    for (i = game_filter_scroll; i < last; i++) {
        const SaveInfo *info = &dex->saves[i];
        int selected = i == game_filter_cursor;
        int ink = selected ? INK_GOLD : INK_CREAM;
        int on = filter_version[i];
        at(2 + (i - game_filter_scroll), 0);
        emit(ink, selected ? ">" : "", 2);
        emit(ink, info->game, 6);
        emit(ink, profile_name(info), 16);
        emit_right(on ? INK_GOLD : INK_MUTED, on ? "On" : "Off", 6);
    }
}

static void draw_mode_filters(void)
{
    int i;
    title_row(0, "Pokedex", dex_mode_name(dex_mode));
    rule_row(1);
    for (i = mode_scroll; i < DEX_MODES && i < mode_scroll + FILTER_PAGE; i++) {
        int on = i == dex_mode;
        draw_toggle_row(2 + (i - mode_scroll), i == mode_cursor, dex_mode_name(i),
                        on ? "On" : "Off", on);
    }
}

static void draw_filter(const Dex *dex)
{
    if (filter_pane == PANE_GAMES)
        draw_game_filters(dex);
    else if (filter_pane == PANE_MODE)
        draw_mode_filters();
    else
        draw_root_filter(dex);
}

static void draw_mode_note(int mode)
{
    int first;
    int last;
    mode_range(mode, &first, &last);
    at(9, 0);
    use_ink(INK_CREAM);
    if (mode == DEX_ALL) {
        fputs("The full National Dex.", stdout);
        return;
    }
    fputs(dex_mode_name(mode), stdout);
    fputs(" National Dex.", stdout);
    at(11, 0);
    use_ink(INK_GOLD);
    printf("#%03d - #%03d", first, last);
}

static void draw_games_rule(const Dex *dex)
{
    int on = games_enabled(dex);
    at(9, 0);
    use_ink(INK_CREAM);
    if (dex->save_count <= 0)
        fputs("No saves.", stdout);
    else if (on == dex->save_count)
        fputs("Every save is included.", stdout);
    else if (on == 0)
        fputs("No saves are included.", stdout);
    else
        printf("%d of %d saves included.", on, dex->save_count);
}

/* Bottom screen while filtering: only the sentence for the highlighted rule. */
static void draw_filter_note(const Dex *dex)
{
    if (filter_pane == PANE_GAMES) {
        const SaveInfo *info;
        if (dex->save_count <= 0 || game_filter_cursor < 0 || game_filter_cursor >= dex->save_count)
            return;
        info = &dex->saves[game_filter_cursor];
        at(8, 0);
        use_ink(INK_CREAM);
        fputs("Include Pokemon from", stdout);
        at(10, 0);
        use_ink(INK_GOLD);
        emit(INK_GOLD, info->name[0] ? info->name : profile_name(info), COLS);
        if (info->game[0]) {
            at(11, 0);
            use_ink(INK_MUTED);
            fputs(info->game, stdout);
        }
        return;
    }
    if (filter_pane == PANE_MODE) {
        draw_mode_note(mode_cursor);
        return;
    }
    if (filter_cursor == FILTER_CAUGHT) {
        at(9, 0);
        use_ink(INK_CREAM);
        fputs("Stored in a box or the party.", stdout);
    } else if (filter_cursor == FILTER_SEEN) {
        at(9, 0);
        use_ink(INK_CREAM);
        fputs("Seen in a Pokedex.", stdout);
    } else if (filter_cursor == FILTER_DEX) {
        at(9, 0);
        use_ink(INK_CREAM);
        fputs("Caught in a Pokedex.", stdout);
    } else if (filter_cursor == FILTER_SHINY) {
        at(9, 0);
        use_ink(INK_CREAM);
        fputs("A shiny copy is stored.", stdout);
    } else if (filter_cursor == FILTER_GAMES) {
        draw_games_rule(dex);
    } else if (filter_cursor == FILTER_MODE) {
        draw_mode_note(dex_mode);
    }
}

static int game_national(const SaveInfo *info)
{
    if (!info || !info->game[0])
        return NATIONAL_DEX;
    if (strcmp(info->game, "B/W") == 0 || strcmp(info->game, "B2/W2") == 0)
        return 649;
    if (strcmp(info->game, "D/P") == 0 || strcmp(info->game, "Pt") == 0
        || strcmp(info->game, "HG/SS") == 0)
        return 493;
    return 386;
}

static int dex_flag_count(const SaveInfo *info, int want_caught, int cap)
{
    const uint8_t *bits;
    int i;
    int n = 0;
    if (!info)
        return 0;
    if (cap > NATIONAL_DEX)
        cap = NATIONAL_DEX;
    bits = want_caught ? info->dex_caught : info->dex_seen;
    for (i = 0; i < cap; i++)
        n += (bits[i >> 3] >> (i & 7)) & 1;
    return n;
}

static int save_shiny_count(const Dex *dex, int index)
{
    int i;
    int n = 0;
    if (!dex || index < 0)
        return 0;
    for (i = 0; i < dex->mon_count; i++) {
        const MonRef *mon = &dex->mons[i];
        if (mon->save_index == (uint8_t)index && (mon->flags & MON_SHINY))
            n++;
    }
    return n;
}

static const char *profile_name(const SaveInfo *info)
{
    if (info->trainer[0])
        return info->trainer;
    if (info->name[0])
        return info->name;
    if (info->game[0])
        return info->game;
    return "Save";
}

static void format_money(uint32_t value, char *out, size_t cap)
{
    char digits[16];
    char grouped[24];
    int n;
    int i;
    int g = 0;
    snprintf(digits, sizeof digits, "%u", (unsigned)value);
    n = text_len(digits);
    if (g + 1 < (int)sizeof grouped)
        grouped[g++] = '$';
    for (i = 0; i < n && g + 1 < (int)sizeof grouped; i++) {
        if (i > 0 && (n - i) % 3 == 0 && g + 1 < (int)sizeof grouped)
            grouped[g++] = ',';
        grouped[g++] = digits[i];
    }
    grouped[g] = 0;
    snprintf(out, cap, "%s", grouped);
}

static void format_play(const SaveInfo *info, char *out, size_t cap)
{
    unsigned minutes = info->minutes;
    unsigned seconds = info->seconds;
    if (minutes > 59)
        minutes = 59;
    if (seconds > 59)
        seconds = 59;
    snprintf(out, cap, "%u:%02u:%02u", info->hours, minutes, seconds);
}

static void draw_home(void)
{
    static const char *const names[3] = {"Pokedex", "Games", "Progress"};
    int i;
    title_row(0, "PokeVault", "");
    rule_row(1);
    for (i = 0; i < 3; i++) {
        int on = i == home_cursor;
        at(3 + i, 0);
        emit(on ? INK_GOLD : INK_CREAM, on ? ">" : "", 2);
        emit(on ? INK_GOLD : INK_CREAM, names[i], 16);
    }
}

static void draw_home_note(void)
{
    at(8, 0);
    use_ink(INK_CREAM);
    if (home_cursor == 0) {
        fputs("Every species, stored", stdout);
        at(9, 0);
        fputs("or still missing.", stdout);
    } else if (home_cursor == 1) {
        fputs("Trainer, money, play", stdout);
        at(9, 0);
        fputs("time, and Pokedex.", stdout);
    } else {
        fputs("Stored fills the bar", stdout);
        at(9, 0);
        fputs("to 100%. Shiny goes", stdout);
        at(10, 0);
        fputs("past that.", stdout);
    }
    at(20, 0);
    use_ink(INK_MUTED);
    fputs("A open", stdout);
}

static void draw_progress_bar(int row, const int *counts, const int *inks, int total);

static void value_row(int row, const char *label, const char *value, int value_ink)
{
    at(row, 0);
    emit(INK_MUTED, label, 14);
    emit_right(value_ink, value, COLS - 14);
}

static void draw_games(const Dex *dex)
{
    int last;
    int i;
    char right[24];
    if (dex->save_count <= 0) {
        title_row(0, "Games", "");
        rule_row(1);
        at(3, 0);
        use_ink(INK_MUTED);
        fputs("No saves.", stdout);
        return;
    }
    clamp_cursor(&game_cursor, &game_scroll, dex->save_count, GAME_PAGE);
    snprintf(right, sizeof right, "%d/%d", game_cursor + 1, dex->save_count);
    title_row(0, "Games", right);
    rule_row(1);
    last = game_scroll + GAME_PAGE;
    if (last > dex->save_count)
        last = dex->save_count;
    for (i = game_scroll; i < last; i++) {
        const SaveInfo *info = &dex->saves[i];
        char play[16];
        char dexn[12];
        int row = 2 + (i - game_scroll) * 3;
        int on = i == game_cursor;
        int cap = game_national(info);
        format_play(info, play, sizeof play);
        snprintf(dexn, sizeof dexn, "%d/%d", dex_flag_count(info, 1, cap), cap);
        at(row, 0);
        emit(on ? INK_GOLD : INK_CREAM, on ? ">" : "", 2);
        emit(on ? INK_GOLD : INK_CREAM, profile_name(info), 20);
        emit_right(on ? INK_GOLD : INK_MUTED, info->game, 8);
        at(row + 1, 0);
        emit(INK_MUTED, "", 2);
        emit(on ? INK_CREAM : INK_MUTED, play, 12);
        emit_right(on ? INK_CREAM : INK_MUTED, dexn, COLS - 14);
    }
}

static void draw_game_card(const Dex *dex)
{
    const SaveInfo *info;
    char money[16];
    char play[16];
    char stored[12];
    char shiny[12];
    char caught_txt[16];
    char seen_txt[16];
    int cap;
    int caught_n;
    int seen_n;
    int seen_only;
    int rest;
    int counts[5];
    int inks[4];
    if (dex->save_count <= 0) {
        at(8, 0);
        use_ink(INK_MUTED);
        fputs("Put .sav files in", stdout);
        at(9, 0);
        fputs("roms/nds/saves", stdout);
        at(10, 0);
        fputs("or roms/gba.", stdout);
        at(21, 0);
        fputs("B back", stdout);
        return;
    }
    info = &dex->saves[game_cursor];
    cap = game_national(info);
    caught_n = dex_flag_count(info, 1, cap);
    seen_n = dex_flag_count(info, 0, cap);
    if (seen_n < caught_n)
        seen_n = caught_n;
    seen_only = seen_n - caught_n;
    rest = cap - seen_n;
    if (rest < 0)
        rest = 0;
    format_money(info->money, money, sizeof money);
    format_play(info, play, sizeof play);
    snprintf(stored, sizeof stored, "%u", (unsigned)info->count);
    snprintf(shiny, sizeof shiny, "%d", save_shiny_count(dex, game_cursor));
    snprintf(caught_txt, sizeof caught_txt, "%d/%d", caught_n, cap);
    snprintf(seen_txt, sizeof seen_txt, "%d/%d", seen_n, cap);
    at(0, 0);
    emit(INK_GOLD, profile_name(info), COLS);
    at(1, 0);
    if (info->trainer[0] && info->name[0])
        emit(INK_CREAM, info->name, COLS);
    else
        emit(INK_MUTED, info->game, COLS);
    at(2, 0);
    use_ink(INK_MUTED);
    if (info->trainer[0] && info->name[0])
        fputs(info->game, stdout);
    rule_row(3);
    value_row(5, "Money", money, INK_GOLD);
    value_row(6, "Play time", play, INK_CREAM);
    value_row(7, "Pokemon", stored, INK_CREAM);
    value_row(8, "Shiny", shiny, INK_SHINY);
    value_row(10, "Caught", caught_txt, INK_GOLD);
    counts[0] = caught_n;
    counts[1] = seen_only;
    counts[2] = 0;
    counts[3] = 0;
    counts[4] = rest;
    inks[0] = INK_GOLD;
    inks[1] = INK_MUTED;
    inks[2] = INK_MUTED;
    inks[3] = INK_MUTED;
    draw_progress_bar(11, counts, inks, cap);
    value_row(13, "Seen", seen_txt, INK_CREAM);
    at(21, 0);
    use_ink(INK_MUTED);
    fputs("B back", stdout);
}

static void tally_progress(const Dex *dex, int *shiny, int *stored, int *in_dex, int *seen, int *unseen)
{
    int i;
    *shiny = 0;
    *stored = 0;
    *in_dex = 0;
    *seen = 0;
    *unseen = 0;
    for (i = 0; i < row_count; i++) {
        int st;
        int sh;
        int saw;
        int owned;
        measure_species(dex, &rows[i], &st, &sh, &saw, &owned);
        if (sh)
            (*shiny)++;
        else if (st > 0)
            (*stored)++;
        else if (owned)
            (*in_dex)++;
        else if (saw)
            (*seen)++;
        else
            (*unseen)++;
    }
}

static int percent_of(int part, int total)
{
    if (total <= 0 || part <= 0)
        return 0;
    if (part >= total)
        return 100;
    return part * 100 / total;
}

static void fill_px(int ink, int px)
{
    use_ink(ink);
    while (px >= 8) {
        putchar(0x18);
        px -= 8;
    }
    if (px > 0)
        putchar(0x10 + px);
}

/* counts[0..4] are shiny, stored, in dex, seen, not seen. The last stays empty. */
static void draw_progress_bar(int row, const int *counts, const int *inks, int total)
{
    int width = COLS * 8;
    int px[5];
    int rem[5];
    int used = 0;
    int left;
    int i;
    for (i = 0; i < 5; i++) {
        px[i] = 0;
        rem[i] = 0;
    }
    if (total > 0) {
        for (i = 0; i < 5; i++) {
            px[i] = counts[i] * width / total;
            rem[i] = counts[i] * width % total;
            used += px[i];
        }
        left = width - used;
        while (left > 0) {
            int best = 0;
            for (i = 1; i < 5; i++) {
                if (rem[i] > rem[best])
                    best = i;
            }
            px[best]++;
            rem[best] = -1;
            left--;
        }
    }
    at(row, 0);
    for (i = 0; i < 4; i++)
        fill_px(inks[i], px[i]);
}

static void legend_line(int row, int ink, const char *label, int count)
{
    char num[8];
    snprintf(num, sizeof num, "%d", count);
    at(row, 0);
    use_ink(ink);
    putchar(0x18);
    putchar(' ');
    emit(INK_CREAM, label, 16);
    emit_right(INK_GOLD, num, 6);
}

static int list_filtered(const Dex *dex)
{
    return filter_caught || filter_seen || filter_dex || filter_shiny
        || versions_narrowed(dex) || dex_mode != DEX_ALL;
}

static void draw_progress(const Dex *dex)
{
    int shiny;
    int stored;
    int in_dex;
    int seen;
    int unseen;
    int total = row_count;
    int boxed;
    int counts[5];
    int inks[4];
    char right[12];
    tally_progress(dex, &shiny, &stored, &in_dex, &seen, &unseen);
    boxed = shiny + stored;
    snprintf(right, sizeof right, "%d%%", percent_of(boxed, total));
    title_row(0, "Progress", right);
    rule_row(1);
    at(3, 0);
    use_ink(INK_MUTED);
    fputs("In a box", stdout);
    at(3, 18);
    use_ink(INK_GOLD);
    printf("%d / %d", boxed, total);
    counts[0] = shiny;
    counts[1] = stored;
    counts[2] = in_dex;
    counts[3] = seen;
    counts[4] = unseen;
    inks[0] = INK_SHINY;
    inks[1] = INK_GOLD;
    inks[2] = 10;
    inks[3] = INK_MUTED;
    draw_progress_bar(5, counts, inks, total);
    at(7, 0);
    use_ink(INK_MUTED);
    fputs("Shiny", stdout);
    at(7, 18);
    use_ink(INK_SHINY);
    printf("%d / %d", shiny, total);
    legend_line(9, INK_SHINY, "Shiny", shiny);
    legend_line(10, INK_GOLD, "In box", stored);
    legend_line(11, 10, "In dex", in_dex);
    legend_line(12, INK_MUTED, "Seen", seen);
    legend_line(13, 0, "Not seen", unseen);
}

static void draw_progress_note(const Dex *dex)
{
    at(8, 0);
    use_ink(INK_CREAM);
    fputs("One bar for the list.", stdout);
    at(10, 0);
    fputs("Stored is 100%.", stdout);
    at(11, 0);
    fputs("Shiny is the mark", stdout);
    at(12, 0);
    fputs("past that.", stdout);
    if (list_filtered(dex)) {
        at(14, 0);
        use_ink(INK_MUTED);
        fputs("Uses the current list.", stdout);
    }
    at(21, 0);
    use_ink(INK_MUTED);
    fputs("B back", stdout);
}

static void draw_controls(const Dex *dex)
{
    if (view != VIEW_DEX)
        return;
    at(19, 0);
    use_ink(INK_MUTED);
    fputs("D-pad scroll    L/R page", stdout);
    at(20, 0);
    if (row_count > 0)
        fputs("A open          X filter", stdout);
    else
        fputs("X filter", stdout);
    at(21, 0);
    fputs("B menu", stdout);
}

static void center_line(int row, int ink, const char *text)
{
    int len = text_len(text);
    if (!text)
        return;
    if (len > COLS)
        len = COLS;
    at(row, (COLS - len) / 2);
    use_ink(ink);
    while (len-- > 0)
        putchar((unsigned char)*text++);
}

static int saver_alive(const MonRef *mon)
{
    return mon && (mon->flags & MON_EGG) == 0
        && mon->species >= 1 && mon->species <= NATIONAL_DEX;
}

/* rows[] is the filtered list, in national order. */
static int species_listed(uint16_t species)
{
    int lo = 0;
    int hi = row_count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (rows[mid].species < species)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < row_count && rows[lo].species == species;
}

/* Same rules as the list: the species must still be shown, and this copy's
   game must still be included. Shiny narrows to the shiny copies themselves. */
static int saver_visible(const Dex *dex, const MonRef *mon)
{
    if (!saver_alive(mon))
        return 0;
    if (!version_on(dex, mon->save_index))
        return 0;
    if (!species_listed(mon->species))
        return 0;
    if (filter_shiny && (mon->flags & MON_SHINY) == 0)
        return 0;
    return 1;
}

static int saver_count(const Dex *dex)
{
    int i;
    int n = 0;
    for (i = 0; i < dex->mon_count; i++) {
        if (saver_visible(dex, &dex->mons[i]))
            n++;
    }
    return n;
}

static int saver_nth(const Dex *dex, int nth)
{
    int i;
    int seen = 0;
    for (i = 0; i < dex->mon_count; i++) {
        if (!saver_visible(dex, &dex->mons[i]))
            continue;
        if (seen == nth)
            return i;
        seen++;
    }
    return -1;
}

static uint32_t saver_rnd(void)
{
    saver_rng ^= ((uint32_t)REG_VCOUNT + 1u) * 0x9E3779B9u;
    saver_rng ^= saver_rng << 13;
    saver_rng ^= saver_rng >> 17;
    saver_rng ^= saver_rng << 5;
    return saver_rng;
}

static int saver_pick(const Dex *dex)
{
    int n;
    /* Toggles apply to the list when the filter screen closes. Apply them
       here too, so a game turned off still counts while that screen is open. */
    apply_filter(dex);
    n = saver_count(dex);
    int choice;
    int index;
    if (n <= 0)
        return -1;
    choice = (int)(saver_rnd() % (uint32_t)n);
    index = saver_nth(dex, choice);
    if (n > 1 && index == saver_mon)
        index = saver_nth(dex, (choice + 1) % n);
    return index;
}

/* A slow rise and fall, about two seconds, a few pixels each way. */
static int saver_dy(void)
{
    int t = saver_tick & 127;
    int tri = t < 64 ? t : 128 - t;
    return (tri * 10) / 64 - 5;
}

static void draw_saver(const Dex *dex)
{
    const MonRef *mon;
    const SaveInfo *info;
    char level[16];
    int shiny;
    int row;
    if (saver_mon < 0 || saver_mon >= dex->mon_count)
        return;
    mon = &dex->mons[saver_mon];
    info = save_of(dex, mon);
    shiny = (mon->flags & MON_SHINY) != 0;

    consoleSelect(&top_console);
    consoleClear();
    consoleSelect(&bottom_console);
    consoleClear();
    center_line(8, shiny ? INK_SHINY : INK_CREAM, species_name(mon->species));
    snprintf(level, sizeof level, "Lv %u", mon->level);
    center_line(10, INK_GOLD, level);
    row = 12;
    if (info && info->name[0]) {
        center_line(row, INK_CREAM, info->name);
        row++;
    }
    if (info && info->game[0])
        center_line(row, INK_MUTED, info->game);
    center_line(21, INK_MUTED, "Any button");
    sprites_show_top(mon->species, shiny, 0, saver_dy());
    sprites_flush();
}

static void draw(const Dex *dex)
{
    int copies = 0;
    sprites_hide_top();
    if (page != PAGE_DEX) {
        consoleSelect(&top_console);
        consoleClear();
        use_ink(INK_CREAM);
        if (page == PAGE_GAMES)
            draw_games(dex);
        else if (page == PAGE_PROGRESS)
            draw_progress(dex);
        else
            draw_home();
        consoleSelect(&bottom_console);
        consoleClear();
        use_ink(INK_CREAM);
        if (page == PAGE_GAMES)
            draw_game_card(dex);
        else if (page == PAGE_PROGRESS)
            draw_progress_note(dex);
        else
            draw_home_note();
        sprites_hide();
        return;
    }
    if ((view == VIEW_COPIES || view == VIEW_MOVE) && dex_cursor >= 0 && dex_cursor < row_count)
        copies = enabled_stored(dex, &rows[dex_cursor]);
    if (view == VIEW_COPIES || view == VIEW_MOVE)
        clamp_cursor(&copy_cursor, &copy_scroll, copies, COPY_PAGE);
    else if (view == VIEW_FILTER)
        clamp_filter(dex);
    else
        clamp_cursor(&dex_cursor, &dex_scroll, row_count, DEX_PAGE);

    consoleSelect(&top_console);
    consoleClear();
    use_ink(INK_CREAM);
    if (view == VIEW_FILTER)
        draw_filter(dex);
    else if (view == VIEW_COPIES || view == VIEW_MOVE)
        draw_copy_list(dex, &rows[dex_cursor]);
    else
        draw_dex_list(dex);

    consoleSelect(&bottom_console);
    consoleClear();
    use_ink(INK_CREAM);
    if (view == VIEW_FILTER) {
        draw_filter_note(dex);
        sprites_hide();
        return;
    }
    if (view == VIEW_COPIES || view == VIEW_MOVE) {
        const SpeciesRow *row = &rows[dex_cursor];
        const MonRef *mon = copies > 0 ? enabled_mon(dex, row, copy_cursor) : NULL;
        if (view == VIEW_MOVE && (!mon || (mon->flags & MON_EGG)
                                  || move_cursor < 0 || move_cursor > 3
                                  || mon->moves[move_cursor] == 0))
            view = VIEW_COPIES;
        if (view == VIEW_MOVE) {
            int index = 0;
            int total = 0;
            int i;
            for (i = 0; i < 4; i++) {
                if (!mon->moves[i])
                    continue;
                if (i == move_cursor)
                    index = total;
                total++;
            }
            draw_move_card(mon->moves[move_cursor], index, total);
            sprites_hide();
        } else if (mon) {
            if (copy_page == CARD_ENTRY)
                draw_copy_entry(mon->species, mon->form, (mon->flags & MON_SHINY) != 0);
            else if (copy_page == CARD_WEAK)
                draw_copy_weak(mon->species, mon->form, (mon->flags & MON_SHINY) != 0);
            else
                draw_copy_card(dex, mon);
            draw_card_pager(copy_page);
            sprites_show(mon->species, (mon->flags & MON_SHINY) != 0, (mon->flags & MON_EGG) != 0);
        } else {
            if (copy_page == CARD_ENTRY)
                draw_copy_entry(row->species, 0, 0);
            else if (copy_page == CARD_WEAK)
                draw_copy_weak(row->species, 0, 0);
            else
                draw_uncaught_stats(row->species);
            draw_card_pager(copy_page);
            sprites_show(row->species, 0, 0);
        }
    } else if (row_count > 0) {
        draw_species_card(dex, &rows[dex_cursor]);
    }
    if (dex->truncated && view != VIEW_COPIES && view != VIEW_MOVE) {
        at(18, 0);
        use_ink(INK_MUTED);
        fputs("List full.", stdout);
    }
    draw_controls(dex);
}

void ui_status(const char *msg)
{
    consoleSelect(&top_console);
    consoleClear();
    use_ink(INK_GOLD);
    fputs("PokeVault", stdout);
    consoleSelect(&bottom_console);
    consoleClear();
    at(1, 0);
    use_ink(INK_CREAM);
    printf("%s\n\n", msg ? msg : "");
    use_ink(INK_MUTED);
    fputs("Read only.\nSaves are not modified.", stdout);
    sprites_hide();
    swiWaitForVBlank();
    sprites_flush();
    music_pump();
}

static const MonRef *current_copy(const Dex *dex)
{
    int total;
    if (dex_cursor < 0 || dex_cursor >= row_count)
        return NULL;
    total = enabled_stored(dex, &rows[dex_cursor]);
    if (total <= 0)
        return NULL;
    return enabled_mon(dex, &rows[dex_cursor], copy_cursor);
}

static int first_move_slot(const MonRef *mon)
{
    int i;
    if (!mon || (mon->flags & MON_EGG))
        return -1;
    for (i = 0; i < 4; i++) {
        if (mon->moves[i])
            return i;
    }
    return -1;
}

static int neighbor_move(const MonRef *mon, int slot, int dir)
{
    int i;
    if (!mon || (mon->flags & MON_EGG))
        return -1;
    if (slot < 0 || slot > 3)
        slot = dir > 0 ? 3 : 0;
    for (i = 0; i < 4; i++) {
        slot += dir;
        if (slot > 3)
            slot = 0;
        if (slot < 0)
            slot = 3;
        if (mon->moves[slot])
            return slot;
    }
    return -1;
}

static int touch_row(uint32_t hit)
{
    touchPosition touch;
    int row;
    if ((hit & KEY_TOUCH) == 0)
        return -1;
    touchRead(&touch);
    if (touch.px < 8)
        return -1;
    row = (int)(touch.py / 8) - 1;
    if (row < 0 || row >= ROWS)
        return -1;
    return row;
}

/* A opens the first move. A tap on one of the four move rows opens that move. */
static int open_move(const Dex *dex, uint32_t hit)
{
    const MonRef *mon;
    int slot = -1;
    int row;
    if (view != VIEW_COPIES || copy_page != CARD_STATS)
        return 0;
    mon = current_copy(dex);
    row = touch_row(hit);
    if (row >= 10 && row <= 13)
        slot = row - 10;
    else if (hit & KEY_A)
        slot = first_move_slot(mon);
    else
        return 0;
    if (!mon || slot < 0 || slot > 3 || mon->moves[slot] == 0)
        return 0;
    move_cursor = slot;
    view = VIEW_MOVE;
    return 1;
}

void ui_run(Dex *dex)
{
    int i;
    keysSetRepeat(16, 5);
    for (i = 0; i < MAX_SAVES; i++)
        filter_version[i] = 1;
    filter_caught = 0;
    filter_seen = 0;
    filter_dex = 0;
    filter_shiny = 0;
    filter_pane = PANE_ROOT;
    filter_cursor = 0;
    filter_scroll = 0;
    game_filter_cursor = 0;
    game_filter_scroll = 0;
    mode_cursor = 0;
    mode_scroll = 0;
    dex_mode = DEX_ALL;
    dex_sort(dex, 1);
    refresh_rows(dex);
    page = PAGE_HOME;
    home_cursor = 0;
    game_cursor = 0;
    game_scroll = 0;
    view = VIEW_DEX;
    dex_cursor = 0;
    dex_scroll = 0;
    copy_cursor = 0;
    copy_scroll = 0;
    copy_page = CARD_STATS;
    clamp_cursor(&dex_cursor, &dex_scroll, row_count, DEX_PAGE);
    draw(dex);

    while (1) {
        int dirty = 0;
        uint32_t down;
        uint32_t hit;
        uint32_t held;
        swiWaitForVBlank();
        if (saver_on && saver_mon >= 0 && saver_mon < dex->mon_count) {
            const MonRef *mon = &dex->mons[saver_mon];
            saver_tick++;
            sprites_show_top(mon->species, (mon->flags & MON_SHINY) != 0, 0, saver_dy());
        }
        sprites_flush();
        scanKeys();
        down = keysDownRepeat();
        hit = keysDown();
        held = keysHeld();

        if (saver_on) {
            if (hit) {
                saver_on = 0;
                idle_frames = 0;
                draw(dex);
                sprites_flush();
            } else if (++saver_hold >= SAVER_HOLD) {
                int next = saver_pick(dex);
                saver_hold = 0;
                if (next < 0) {
                    saver_on = 0;
                    draw(dex);
                    sprites_flush();
                } else {
                    saver_mon = next;
                    draw_saver(dex);
                }
            }
            music_pump();
            continue;
        }

        if (held)
            idle_frames = 0;
        else if (idle_frames < SAVER_IDLE)
            idle_frames++;
        if (!held && idle_frames >= SAVER_IDLE) {
            int next = saver_pick(dex);
            if (next >= 0) {
                saver_mon = next;
                saver_on = 1;
                saver_hold = 0;
                saver_tick = 0;
                draw_saver(dex);
                music_pump();
                continue;
            }
        }

        if (page == PAGE_HOME) {
            if (hit & KEY_A) {
                page = home_cursor + 1;
                if (page == PAGE_GAMES)
                    clamp_cursor(&game_cursor, &game_scroll, dex->save_count, GAME_PAGE);
                music_click();
                dirty = 1;
            } else if (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) {
                int before = home_cursor;
                if (down & (KEY_UP | KEY_LEFT))
                    home_cursor--;
                if (down & (KEY_DOWN | KEY_RIGHT))
                    home_cursor++;
                if (home_cursor < 0)
                    home_cursor = 0;
                if (home_cursor > 2)
                    home_cursor = 2;
                if (home_cursor != before) {
                    music_click();
                    dirty = 1;
                }
            }
        } else if (page == PAGE_GAMES) {
            if (hit & KEY_B) {
                page = PAGE_HOME;
                music_click();
                dirty = 1;
            } else if (dex->save_count > 0
                       && (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R))) {
                int before = game_cursor;
                nudge(&game_cursor, dex->save_count, GAME_PAGE, down);
                clamp_cursor(&game_cursor, &game_scroll, dex->save_count, GAME_PAGE);
                if (game_cursor != before) {
                    music_click();
                    dirty = 1;
                }
            }
        } else if (page == PAGE_PROGRESS) {
            if (hit & KEY_B) {
                page = PAGE_HOME;
                music_click();
                dirty = 1;
            }
        } else if (view == VIEW_FILTER) {
            if (hit & KEY_X || (hit & KEY_B && filter_pane == PANE_ROOT)) {
                uint16_t species = 0;
                int keep = 0;
                if (dex_cursor >= 0 && dex_cursor < row_count) {
                    species = rows[dex_cursor].species;
                    keep = 1;
                }
                view = VIEW_DEX;
                filter_pane = PANE_ROOT;
                apply_filter(dex);
                if (keep)
                    select_species(species);
                else
                    clamp_cursor(&dex_cursor, &dex_scroll, row_count, DEX_PAGE);
                music_click();
                dirty = 1;
            } else if (hit & KEY_B) {
                filter_pane = PANE_ROOT;
                music_click();
                dirty = 1;
            } else if (hit & KEY_A) {
                if (filter_pane == PANE_ROOT && filter_cursor == FILTER_GAMES) {
                    if (dex->save_count > 0) {
                        filter_pane = PANE_GAMES;
                        clamp_filter(dex);
                    }
                } else if (filter_pane == PANE_ROOT && filter_cursor == FILTER_MODE) {
                    filter_pane = PANE_MODE;
                    mode_cursor = dex_mode;
                    clamp_filter(dex);
                } else if (filter_pane == PANE_GAMES) {
                    if (game_filter_cursor >= 0 && game_filter_cursor < dex->save_count)
                        filter_version[game_filter_cursor] = !filter_version[game_filter_cursor];
                } else if (filter_pane == PANE_MODE) {
                    dex_mode = mode_cursor;
                } else {
                    toggle_filter(filter_cursor);
                }
                music_click();
                dirty = 1;
            } else if (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R)) {
                int before;
                int count;
                int *cursor;
                if (filter_pane == PANE_GAMES) {
                    cursor = &game_filter_cursor;
                    count = dex->save_count;
                } else if (filter_pane == PANE_MODE) {
                    cursor = &mode_cursor;
                    count = DEX_MODES;
                } else {
                    cursor = &filter_cursor;
                    count = FILTER_ROOT;
                }
                before = *cursor;
                nudge(cursor, count, FILTER_PAGE, down);
                clamp_filter(dex);
                if (*cursor != before) {
                    music_click();
                    dirty = 1;
                }
            }
        } else if (hit & KEY_X && view == VIEW_DEX) {
            view = VIEW_FILTER;
            filter_pane = PANE_ROOT;
            music_click();
            dirty = 1;
        } else if (view == VIEW_MOVE) {
            const MonRef *mon = current_copy(dex);
            if (hit & KEY_B) {
                view = VIEW_COPIES;
                music_click();
                dirty = 1;
            } else if (hit & (KEY_L | KEY_R)) {
                view = VIEW_COPIES;
                if (hit & KEY_L)
                    copy_page = (copy_page + CARD_PAGES - 1) % CARD_PAGES;
                if (hit & KEY_R)
                    copy_page = (copy_page + 1) % CARD_PAGES;
                music_click();
                dirty = 1;
            } else if (row_count > 0 && (down & (KEY_LEFT | KEY_RIGHT))) {
                int before = dex_cursor;
                if (down & KEY_LEFT)
                    dex_cursor--;
                if (down & KEY_RIGHT)
                    dex_cursor++;
                clamp_cursor(&dex_cursor, &dex_scroll, row_count, DEX_PAGE);
                if (dex_cursor != before) {
                    view = VIEW_COPIES;
                    move_cursor = -1;
                    copy_cursor = 0;
                    copy_scroll = 0;
                    music_click();
                    dirty = 1;
                }
            } else if (mon && (down & (KEY_UP | KEY_DOWN))) {
                int next = neighbor_move(mon, move_cursor, (down & KEY_DOWN) ? 1 : -1);
                if (next >= 0 && next != move_cursor) {
                    move_cursor = next;
                    music_click();
                    dirty = 1;
                }
            }
        } else if (open_move(dex, hit)) {
            music_click();
            dirty = 1;
        } else if (hit & KEY_B && view == VIEW_COPIES) {
            view = VIEW_DEX;
            music_click();
            dirty = 1;
        } else if (hit & KEY_B && view == VIEW_DEX) {
            page = PAGE_HOME;
            music_click();
            dirty = 1;
        } else if (hit & KEY_A && view == VIEW_DEX && row_count > 0) {
            view = VIEW_COPIES;
            copy_cursor = 0;
            copy_scroll = 0;
            copy_page = CARD_STATS;
            music_click();
            dirty = 1;
        } else if (view == VIEW_COPIES && (hit & (KEY_L | KEY_R))) {
            if (hit & KEY_L)
                copy_page = (copy_page + CARD_PAGES - 1) % CARD_PAGES;
            if (hit & KEY_R)
                copy_page = (copy_page + 1) % CARD_PAGES;
            music_click();
            dirty = 1;
        } else if (view == VIEW_COPIES && row_count > 0 && (down & (KEY_LEFT | KEY_RIGHT))) {
            int before = dex_cursor;
            if (down & KEY_LEFT)
                dex_cursor--;
            if (down & KEY_RIGHT)
                dex_cursor++;
            clamp_cursor(&dex_cursor, &dex_scroll, row_count, DEX_PAGE);
            if (dex_cursor != before) {
                copy_cursor = 0;
                copy_scroll = 0;
                music_click();
                dirty = 1;
            }
        } else if (row_count > 0 && (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R))) {
            int before;
            int after;
            if (view == VIEW_COPIES) {
                int total = enabled_stored(dex, &rows[dex_cursor]);
                before = copy_cursor;
                nudge(&copy_cursor, total, COPY_PAGE, down & ~(KEY_L | KEY_R | KEY_LEFT | KEY_RIGHT));
                clamp_cursor(&copy_cursor, &copy_scroll, total, COPY_PAGE);
                after = copy_cursor;
            } else {
                before = dex_cursor;
                nudge(&dex_cursor, row_count, DEX_PAGE, down);
                clamp_cursor(&dex_cursor, &dex_scroll, row_count, DEX_PAGE);
                after = dex_cursor;
            }
            if (after != before) {
                music_click();
                dirty = 1;
            }
        }

        /* Mix the click into this frame's buffer, before the next screen draw. */
        music_pump();
        if (dirty)
            draw(dex);
    }
}

void ui_init(void)
{
    setup_consoles();
    sprites_init();
    ui_status("Starting...");
}

void ui_fail(const char *msg)
{
    ui_status(msg);
    while (1) {
        swiWaitForVBlank();
        music_pump();
    }
}
