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
    NATIONAL = 649,
    DEX_PAGE = 20,
    COPY_PAGE = 20,
    VIEW_DEX = 0,
    VIEW_COPIES = 1,
    INK_SHINY = 6,
    INK_MUTED = 8,
    INK_GOLD = 11,
    INK_CREAM = 15,
    GLYPH_BAR = 0x10,
    GLYPH_RULE = 0x19
};

/* Glyphs 0x11-0x18 are stat bars. 0x19 is the title rule.
   Built by tools/build_font.py. */
static uint8_t font_1bpp[256 * 8] = {
#include "font.inc"
};

static PrintConsole top_console;
static PrintConsole bottom_console;
static SpeciesRow rows[650];
static int row_count;
static int dex_cursor;
static int dex_scroll;
static int copy_cursor;
static int copy_scroll;
static int view = VIEW_DEX;
static int caught;

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

static void refresh_rows(const Dex *dex)
{
    int mon = 0;
    int i;

    caught = 0;
    row_count = NATIONAL;
    for (i = 1; i <= NATIONAL; i++) {
        int count = 0;
        int first;
        while (mon < dex->mon_count && dex->mons[mon].species < (uint16_t)i)
            mon++;
        first = mon;
        while (mon < dex->mon_count && dex->mons[mon].species == (uint16_t)i) {
            count++;
            mon++;
        }
        rows[i - 1].species = (uint16_t)i;
        rows[i - 1].count = (uint16_t)count;
        rows[i - 1].first = (uint16_t)first;
        if (count > 0)
            caught++;
    }
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
    int i;
    for (i = 0; i < row->count; i++) {
        const MonRef *mon = &dex->mons[row->first + i];
        int shiny;
        int best_shiny;
        if (mon->flags & MON_EGG)
            continue;
        shiny = (mon->flags & MON_SHINY) != 0;
        best_shiny = best && (best->flags & MON_SHINY) != 0;
        if (!best || mon->level > best->level || (mon->level == best->level && shiny && !best_shiny))
            best = mon;
    }
    if (!best)
        best = &dex->mons[row->first];
    return best;
}

static void draw_dex_row(int row, const SpeciesRow *entry, int selected)
{
    char num[8];
    char qty[8];
    int owned = entry->count > 0;
    int ink = selected ? INK_GOLD : INK_MUTED;
    int name_ink = selected ? INK_GOLD : (owned ? INK_CREAM : INK_MUTED);
    qty[0] = 0;
    snprintf(num, sizeof num, "#%03u", entry->species);
    if (owned)
        snprintf(qty, sizeof qty, "x%u", (unsigned)entry->count);
    at(row, 0);
    emit(ink, selected ? ">" : "", 2);
    emit(ink, num, 5);
    emit(name_ink, species_name(entry->species), 17);
    emit_right(ink, owned ? qty : "-", 6);
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

static void draw_dex_list(const Dex *dex)
{
    char right[12];
    int i;
    int last;
    (void)dex;
    snprintf(right, sizeof right, "%d/%d", caught, NATIONAL);
    title_row(0, "Pokedex", right);
    rule_row(1);
    last = dex_scroll + DEX_PAGE;
    if (last > row_count)
        last = row_count;
    for (i = dex_scroll; i < last; i++)
        draw_dex_row(2 + (i - dex_scroll), &rows[i], i == dex_cursor);
}

static void draw_copy_list(const Dex *dex, const SpeciesRow *row)
{
    char left[32];
    char right[24];
    int i;
    int last;
    snprintf(left, sizeof left, "#%03u %s", row->species, species_name(row->species));
    snprintf(right, sizeof right, "%d/%u", copy_cursor + 1, (unsigned)row->count);
    title_row(0, left, right);
    rule_row(1);
    last = copy_scroll + COPY_PAGE;
    if (last > row->count)
        last = row->count;
    for (i = copy_scroll; i < last; i++)
        draw_copy_row(&dex->mons[row->first + i], 2 + (i - copy_scroll), i == copy_cursor);
}

static void draw_missing_card(const Dex *dex, const SpeciesRow *row)
{
    char buf[32];
    unsigned type = species_type(row->species, 0);

    at(0, 0);
    use_ink(INK_GOLD);
    printf("#%03u ", row->species);
    use_ink(INK_CREAM);
    fputs(species_name(row->species), stdout);

    at(1, 0);
    use_ink(type_ink(type));
    fputs(type_name(type), stdout);

    at(3, 0);
    use_ink(INK_MUTED);
    fputs("Not caught", stdout);
    if (dex->save_count == 0) {
        at(5, 0);
        fputs("Put .sav files in", stdout);
        at(6, 0);
        fputs("roms/nds/saves", stdout);
        at(7, 0);
        fputs("or roms/gba.", stdout);
    }
    snprintf(buf, sizeof buf, "%d saves", dex->save_count);
    at(16, 0);
    use_ink(INK_MUTED);
    fputs(buf, stdout);
    sprites_show(row->species, 0, 0);
}

static void draw_species_card(const Dex *dex, const SpeciesRow *row)
{
    const MonRef *face;
    int shiny = 0;
    int eggs = 0;
    int i;
    int line = 2;
    char buf[32];
    unsigned type = species_type(row->species, 0);

    if (row->count == 0) {
        draw_missing_card(dex, row);
        return;
    }
    face = best_mon(dex, row);

    for (i = 0; i < row->count; i++) {
        const MonRef *mon = &dex->mons[row->first + i];
        if (mon->flags & MON_SHINY)
            shiny++;
        if (mon->flags & MON_EGG)
            eggs++;
    }

    at(0, 0);
    use_ink(INK_GOLD);
    printf("#%03u ", row->species);
    use_ink(INK_CREAM);
    fputs(species_name(row->species), stdout);

    at(1, 0);
    use_ink(type_ink(type));
    fputs(type_name(type), stdout);
    snprintf(buf, sizeof buf, "x%u", (unsigned)row->count);
    at(1, COLS - text_len(buf));
    use_ink(INK_GOLD);
    fputs(buf, stdout);

    if (face->flags & MON_EGG) {
        at(line++, 0);
        use_ink(INK_CREAM);
        fputs("Egg", stdout);
    } else {
        at(line++, 0);
        use_ink(INK_MUTED);
        fputs("Best", stdout);
        use_ink(INK_CREAM);
        printf(" Lv %u", face->level);
    }
    if (shiny > 0) {
        at(line++, 0);
        use_ink(INK_SHINY);
        printf("Shiny x%d", shiny);
    }
    if (eggs > 0) {
        at(line++, 0);
        use_ink(INK_MUTED);
        printf("Eggs x%d", eggs);
    }
    snprintf(buf, sizeof buf, "%d saves", dex->save_count);
    at(16, 0);
    use_ink(INK_MUTED);
    fputs(buf, stdout);
    sprites_show(face->species, (face->flags & MON_SHINY) != 0, (face->flags & MON_EGG) != 0);
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

static void draw_copy_card(const Dex *dex, const MonRef *mon)
{
    const SaveInfo *info = save_of(dex, mon);
    uint16_t st[6];
    unsigned formed;
    int peak;
    int i;
    char buf[40];
    static const char *const labels[6] = {"HP", "Atk", "Def", "SpA", "SpD", "Spe"};

    at(0, 0);
    use_ink(INK_CREAM);
    if (mon->flags & MON_EGG)
        fputs("Egg", stdout);
    else
        printf("Lv %u", mon->level);
    if ((mon->flags & MON_EGG) == 0) {
        use_ink(INK_GOLD);
        printf("   %s", nature_name(mon->nature));
    }
    if (mon->flags & MON_SHINY) {
        at(0, 24);
        use_ink(INK_SHINY);
        fputs("Shiny", stdout);
    }

    formed = species_type(mon->species, mon->form);
    if (formed != species_type(mon->species, 0)) {
        at(1, 0);
        use_ink(type_ink(formed));
        fputs(type_name(formed), stdout);
    }

    if ((mon->flags & MON_EGG) == 0) {
        mon_battle_stats(mon, st);
        peak = 1;
        for (i = 0; i < 6; i++) {
            if (st[i] > peak)
                peak = st[i];
        }
        for (i = 0; i < 6; i++) {
            at(2 + i, 0);
            use_ink(INK_MUTED);
            printf("%-3.3s ", labels[i]);
            use_ink(INK_CREAM);
            printf("%3u ", (unsigned)st[i]);
            use_ink(INK_GOLD);
            stat_bar(st[i], peak);
        }
    }

    for (i = 0; i < 4; i++)
        draw_move(11 + i, mon->moves[i]);

    at(16, 0);
    use_ink(INK_CREAM);
    emit(INK_CREAM, info ? info->name : "?", COLS);
    at(17, 0);
    use_ink(INK_MUTED);
    if (mon->flags & MON_PARTY)
        snprintf(buf, sizeof buf, "%s   Party %u", info ? info->game : "?", (unsigned)mon->slot + 1);
    else
        snprintf(buf, sizeof buf, "%s   Box %u  slot %u",
                 info ? info->game : "?", (unsigned)mon->box + 1, (unsigned)mon->slot + 1);
    fputs(buf, stdout);
    sprites_show(mon->species, (mon->flags & MON_SHINY) != 0, (mon->flags & MON_EGG) != 0);
}

static void draw_controls(void)
{
    int owned = view == VIEW_DEX && dex_cursor >= 0 && dex_cursor < row_count
        && rows[dex_cursor].count > 0;
    at(19, 0);
    use_ink(INK_MUTED);
    fputs("D-pad scroll    L/R page", stdout);
    at(20, 0);
    if (view == VIEW_COPIES)
        fputs("B back          Y rescan", stdout);
    else if (owned)
        fputs("A open          Y rescan", stdout);
    else
        fputs("Y rescan", stdout);
    at(21, 0);
    fputs("SELECT exit", stdout);
}

static void draw(const Dex *dex)
{
    if (view == VIEW_COPIES && (dex_cursor < 0 || dex_cursor >= row_count))
        view = VIEW_DEX;
    if (view == VIEW_COPIES)
        clamp_cursor(&copy_cursor, &copy_scroll, rows[dex_cursor].count, COPY_PAGE);
    else
        clamp_cursor(&dex_cursor, &dex_scroll, row_count, DEX_PAGE);

    consoleSelect(&top_console);
    consoleClear();
    use_ink(INK_CREAM);
    if (view == VIEW_COPIES) {
        draw_copy_list(dex, &rows[dex_cursor]);
    } else {
        draw_dex_list(dex);
    }

    consoleSelect(&bottom_console);
    consoleClear();
    use_ink(INK_CREAM);
    if (view == VIEW_COPIES) {
        const SpeciesRow *row = &rows[dex_cursor];
        draw_copy_card(dex, &dex->mons[row->first + copy_cursor]);
    } else if (row_count > 0) {
        draw_species_card(dex, &rows[dex_cursor]);
    }
    if (dex->truncated) {
        at(18, 0);
        use_ink(INK_MUTED);
        fputs("List full.", stdout);
    }
    draw_controls();
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

void ui_run(Dex *dex, void (*rescan)(Dex *dex))
{
    keysSetRepeat(16, 5);
    dex_sort(dex, 1);
    refresh_rows(dex);
    view = VIEW_DEX;
    dex_cursor = 0;
    dex_scroll = 0;
    copy_cursor = 0;
    copy_scroll = 0;
    clamp_cursor(&dex_cursor, &dex_scroll, row_count, DEX_PAGE);
    draw(dex);

    while (1) {
        int dirty = 0;
        uint32_t down;
        uint32_t hit;
        swiWaitForVBlank();
        sprites_flush();
        music_pump();
        scanKeys();
        down = keysDownRepeat();
        hit = keysDown();

        if (hit & KEY_SELECT)
            return;

        if (hit & KEY_Y) {
            uint16_t species = 0;
            int keep = 0;
            if (row_count > 0 && dex_cursor >= 0 && dex_cursor < row_count) {
                species = rows[dex_cursor].species;
                keep = 1;
            }
            ui_status("Scanning the card...");
            rescan(dex);
            dex_sort(dex, 1);
            refresh_rows(dex);
            view = VIEW_DEX;
            copy_cursor = 0;
            copy_scroll = 0;
            if (keep)
                select_species(species);
            else
                clamp_cursor(&dex_cursor, &dex_scroll, row_count, DEX_PAGE);
            dirty = 1;
        }

        if (hit & KEY_B && view == VIEW_COPIES) {
            view = VIEW_DEX;
            music_click();
            dirty = 1;
        } else if (hit & KEY_A && view == VIEW_DEX && row_count > 0 && rows[dex_cursor].count > 0) {
            view = VIEW_COPIES;
            copy_cursor = 0;
            copy_scroll = 0;
            music_click();
            dirty = 1;
        } else if (row_count > 0 && (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R))) {
            int before;
            int after;
            if (view == VIEW_COPIES) {
                before = copy_cursor;
                nudge(&copy_cursor, rows[dex_cursor].count, COPY_PAGE, down);
                clamp_cursor(&copy_cursor, &copy_scroll, rows[dex_cursor].count, COPY_PAGE);
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
