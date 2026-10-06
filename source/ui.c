#include "moves.h"
#include "save.h"
#include "species.h"
#include "sprites.h"
#include "stats.h"

#include <nds.h>
#include <stdio.h>
#include <string.h>

enum {
    DEX_PAGE = 20,
    COPY_PAGE = 4,
    VIEW_DEX = 0,
    VIEW_COPIES = 1
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

static void setup_consoles(void)
{
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(&top_console, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);
    consoleInit(&bottom_console, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);
}

static void bottom_text(const char *text)
{
    consoleSelect(&bottom_console);
    consoleClear();
    printf("%s", text);
}

static void line_at(int row, int width, const char *text)
{
    printf("\x1b[%d;1H%-*.*s", row, width, width, text);
}

static void title_line(const char *left, const char *right)
{
    int right_len = (int)strlen(right);
    int left_w = 30 - right_len;
    if (left_w < 1)
        left_w = 1;
    printf("%-*.*s %s\n", left_w, left_w, left, right);
}

static void card_line(char mark, const char *text)
{
    printf("%c%-30.30s\n", mark, text ? text : "");
}

static const char *move_or_dash(unsigned move)
{
    return move ? move_name(move) : "-";
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

void ui_status(const char *msg)
{
    char buf[128];
    snprintf(buf, sizeof buf, "PokeVault\n\n%s\n\nRead only.\nSaves are not modified.", msg);
    bottom_text(buf);
    sprites_hide();
    swiWaitForVBlank();
    sprites_flush();
}

static void refresh_rows(const Dex *dex)
{
    row_count = dex_species_rows(dex, rows, (int)(sizeof rows / sizeof rows[0]));
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

static const MonRef *face_mon(const Dex *dex, const SpeciesRow *row)
{
    int i;
    for (i = 0; i < row->count; i++) {
        const MonRef *mon = &dex->mons[row->first + i];
        if ((mon->flags & MON_EGG) == 0)
            return mon;
    }
    return &dex->mons[row->first];
}

static void draw_dex_list(const Dex *dex)
{
    int i;
    int last;
    char left[32];
    char right[12];

    snprintf(left, sizeof left, "%d/%d species", dex_cursor + 1, row_count);
    snprintf(right, sizeof right, "x%d", dex->mon_count);
    title_line(left, right);
    last = dex_scroll + DEX_PAGE;
    if (last > row_count)
        last = row_count;
    for (i = dex_scroll; i < last; i++) {
        char qty[8];
        snprintf(qty, sizeof qty, "x%u", (unsigned)rows[i].count);
        printf("%c#%03u %-18.18s %6.6s\n",
               i == dex_cursor ? '>' : ' ',
               rows[i].species,
               species_name(rows[i].species),
               qty);
    }
}

static void draw_copy_list(const Dex *dex, const SpeciesRow *row)
{
    int i;
    int last;
    char title[40];
    char pos[24];
    char buf[40];

    snprintf(title, sizeof title, "#%03u %s", row->species, species_name(row->species));
    snprintf(pos, sizeof pos, "%d/%u", copy_cursor + 1, (unsigned)row->count);
    title_line(title, pos);

    last = copy_scroll + COPY_PAGE;
    if (last > row->count)
        last = row->count;
    for (i = copy_scroll; i < last; i++) {
        const MonRef *mon = &dex->mons[row->first + i];
        const SaveInfo *info = save_of(dex, mon);
        uint16_t st[6];
        char lv[8];
        char where[12];
        mon_battle_stats(mon, st);
        if (mon->flags & MON_EGG)
            snprintf(lv, sizeof lv, "Egg");
        else
            snprintf(lv, sizeof lv, "Lv%-3u", mon->level);
        where_of(mon, where, sizeof where);
        snprintf(buf, sizeof buf, "%-5.5s %c %-7.7s %-14.14s",
                 lv,
                 (mon->flags & MON_SHINY) ? '*' : ' ',
                 where,
                 info ? info->name : "?");
        card_line(i == copy_cursor ? '>' : ' ', buf);
        snprintf(buf, sizeof buf, "HP %3u  Atk %3u  Def %3u", st[0], st[1], st[2]);
        card_line(' ', buf);
        snprintf(buf, sizeof buf, "SpA %3u  SpD %3u  Spe %3u", st[3], st[4], st[5]);
        card_line(' ', buf);
        snprintf(buf, sizeof buf, "%-14.14s  %-14.14s",
                 move_or_dash(mon->moves[0]), move_or_dash(mon->moves[1]));
        card_line(' ', buf);
        snprintf(buf, sizeof buf, "%-14.14s  %-14.14s",
                 move_or_dash(mon->moves[2]), move_or_dash(mon->moves[3]));
        card_line(' ', buf);
    }
}

static void draw_species_card(const Dex *dex, const SpeciesRow *row)
{
    const MonRef *face = face_mon(dex, row);
    int shiny = 0;
    int eggs = 0;
    int i;
    int line = 1;
    char buf[32];

    for (i = 0; i < row->count; i++) {
        const MonRef *mon = &dex->mons[row->first + i];
        if (mon->flags & MON_SHINY)
            shiny++;
        if (mon->flags & MON_EGG)
            eggs++;
    }

    snprintf(buf, sizeof buf, "#%03u %s", row->species, species_name(row->species));
    line_at(line++, 22, buf);
    snprintf(buf, sizeof buf, "Owned x%u", (unsigned)row->count);
    line_at(line++, 22, buf);
    if (face->flags & MON_EGG) {
        line_at(line++, 22, "Egg");
    } else {
        snprintf(buf, sizeof buf, "Best Lv %u%s", face->level, (face->flags & MON_SHINY) ? " *" : "");
        line_at(line++, 22, buf);
    }
    if (shiny > 0) {
        snprintf(buf, sizeof buf, "Shiny x%d", shiny);
        line_at(line++, 22, buf);
    }
    if (eggs > 0) {
        snprintf(buf, sizeof buf, "Eggs x%d", eggs);
        line_at(line++, 22, buf);
    }
    snprintf(buf, sizeof buf, "%d saves", dex->save_count);
    line_at(16, 31, buf);
    sprites_show(face->species, (face->flags & MON_SHINY) != 0, (face->flags & MON_EGG) != 0);
}

static void draw_copy_card(const Dex *dex, const MonRef *mon)
{
    const SaveInfo *info = save_of(dex, mon);
    uint16_t st[6];
    char buf[40];

    mon_battle_stats(mon, st);
    if (mon->flags & MON_EGG)
        snprintf(buf, sizeof buf, "Egg%s", (mon->flags & MON_SHINY) ? " *" : "");
    else
        snprintf(buf, sizeof buf, "Lv %u%s", mon->level, (mon->flags & MON_SHINY) ? " *" : "");
    line_at(1, 22, buf);
    line_at(2, 22, nature_name(mon->nature));
    snprintf(buf, sizeof buf, "HP %3u  Atk %3u", st[0], st[1]);
    line_at(3, 22, buf);
    snprintf(buf, sizeof buf, "Def %3u  SpA %3u", st[2], st[3]);
    line_at(4, 22, buf);
    snprintf(buf, sizeof buf, "SpD %3u  Spe %3u", st[4], st[5]);
    line_at(5, 22, buf);
    line_at(7, 22, move_or_dash(mon->moves[0]));
    line_at(8, 22, move_or_dash(mon->moves[1]));
    line_at(9, 22, move_or_dash(mon->moves[2]));
    line_at(10, 22, move_or_dash(mon->moves[3]));
    line_at(12, 31, info ? info->name : "?");
    if (mon->flags & MON_PARTY)
        snprintf(buf, sizeof buf, "%s  Party %u", info ? info->game : "?", (unsigned)mon->slot + 1);
    else
        snprintf(buf, sizeof buf, "%s  Box %u  slot %u",
                 info ? info->game : "?", (unsigned)mon->box + 1, (unsigned)mon->slot + 1);
    line_at(13, 31, buf);
    sprites_show(mon->species, (mon->flags & MON_SHINY) != 0, (mon->flags & MON_EGG) != 0);
}

static void draw_controls(void)
{
    printf("\x1b[22;1HD-pad scroll   L/R page\n");
    if (view == VIEW_COPIES)
        printf("B back     Y rescan\n");
    else
        printf("A open     Y rescan\n");
    printf("SELECT exit");
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
    if (row_count <= 0) {
        printf("No Pokemon found.\n\n");
        printf("Put .sav files in\nroms/nds/saves\nor roms/gba.\n");
    } else if (view == VIEW_COPIES) {
        draw_copy_list(dex, &rows[dex_cursor]);
    } else {
        draw_dex_list(dex);
    }

    consoleSelect(&bottom_console);
    consoleClear();
    if (row_count <= 0) {
        sprites_hide();
        printf("PokeVault\n\n");
        printf("Saves seen: %d\n", dex->files_seen);
    } else if (view == VIEW_COPIES) {
        const SpeciesRow *row = &rows[dex_cursor];
        draw_copy_card(dex, &dex->mons[row->first + copy_cursor]);
    } else {
        draw_species_card(dex, &rows[dex_cursor]);
    }
    if (dex->truncated)
        line_at(18, 31, "List full.");
    draw_controls();
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
            dirty = 1;
        } else if (hit & KEY_A && view == VIEW_DEX && row_count > 0) {
            view = VIEW_COPIES;
            copy_cursor = 0;
            copy_scroll = 0;
            dirty = 1;
        } else if (row_count > 0 && (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R))) {
            if (view == VIEW_COPIES) {
                nudge(&copy_cursor, rows[dex_cursor].count, COPY_PAGE, down);
                clamp_cursor(&copy_cursor, &copy_scroll, rows[dex_cursor].count, COPY_PAGE);
            } else {
                nudge(&dex_cursor, row_count, DEX_PAGE, down);
                clamp_cursor(&dex_cursor, &dex_scroll, row_count, DEX_PAGE);
            }
            dirty = 1;
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
    while (1)
        swiWaitForVBlank();
}
