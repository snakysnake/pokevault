#include "moves.h"
#include "save.h"
#include "species.h"
#include "sprites.h"

#include <nds.h>
#include <stdio.h>
#include <string.h>

#define PAGE 20

static PrintConsole top_console;
static PrintConsole bottom_console;
static int cursor;
static int scroll;
static int by_species = 1;

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

void ui_status(const char *msg)
{
    char buf[128];
    snprintf(buf, sizeof buf, "PokeVault\n\n%s\n\nRead only.\nSaves are not modified.", msg);
    bottom_text(buf);
    sprites_hide();
    swiWaitForVBlank();
    sprites_flush();
}

static void mon_label(const MonRef *mon, char *out, size_t cap)
{
    const char *name = (mon->flags & MON_EGG) ? "Egg" : species_name(mon->species);
    if (mon->flags & MON_SHINY)
        snprintf(out, cap, "*%s", name);
    else
        snprintf(out, cap, "%s", name);
}

static void draw(const Dex *dex)
{
    int i;
    int last;
    const MonRef *sel;
    char label[16];
    char where[24];

    consoleSelect(&top_console);
    consoleClear();
    if (dex->mon_count == 0) {
        printf("No Pokemon found.\n\n");
        printf("Put .sav files in\nroms/nds/saves\nor roms/gba.\n");
    } else {
        printf("%3d/%-4d %-7s %s\n",
               cursor + 1,
               dex->mon_count,
               dex->saves[dex->mons[cursor].save_index].game,
               by_species ? "species" : "file");
        last = scroll + PAGE;
        if (last > dex->mon_count)
            last = dex->mon_count;
        for (i = scroll; i < last; i++) {
            const MonRef *mon = &dex->mons[i];
            mon_label(mon, label, sizeof label);
            printf("%c%3u %-12.12s %-12.12s\n",
                   i == cursor ? '>' : ' ',
                   mon->level,
                   label,
                   dex->saves[mon->save_index].name);
        }
    }

    consoleSelect(&bottom_console);
    consoleClear();
    if (dex->mon_count == 0) {
        sprites_hide();
        printf("PokeVault\n\n");
        printf("Saves seen: %d\n", dex->files_seen);
    } else {
        char title[32];
        char level[24];
        char game[24];
        char count[32];
        int move_i;
        sel = &dex->mons[cursor];
        if (sel->flags & MON_PARTY)
            snprintf(where, sizeof where, "Party slot %u", (unsigned)sel->slot + 1);
        else
            snprintf(where, sizeof where, "Box %u slot %u", (unsigned)sel->box + 1, (unsigned)sel->slot + 1);
        if (sel->flags & MON_EGG)
            snprintf(title, sizeof title, "Egg (%s)", species_name(sel->species));
        else
            snprintf(title, sizeof title, "%s", species_name(sel->species));
        snprintf(level, sizeof level, "Lv %u%s", sel->level, (sel->flags & MON_SHINY) ? "  Shiny" : "");
        snprintf(game, sizeof game, "Game %s", dex->saves[sel->save_index].game);
        /* The sprite covers the top-right 64x64. These lines stay in the left 22 columns. */
        line_at(1, 22, title);
        line_at(2, 22, level);
        line_at(3, 22, where);
        line_at(4, 22, dex->saves[sel->save_index].name);
        line_at(5, 22, game);
        for (move_i = 0; move_i < 4; move_i++) {
            unsigned move = sel->moves[move_i];
            line_at(6 + move_i, 22, move ? move_name(move) : "-");
        }
        snprintf(count, sizeof count, "%d saves, %d Pokemon", dex->save_count, dex->mon_count);
        line_at(11, 31, count);
        if (dex->truncated)
            line_at(12, 31, "List full.");
        sprites_show(sel->species, (sel->flags & MON_SHINY) != 0, (sel->flags & MON_EGG) != 0);
    }
    printf("\x1b[21;1HD-pad scroll   L/R page\n");
    printf("X sort   Y rescan\n");
    printf("SELECT  exit");
}

static void clamp_view(const Dex *dex)
{
    if (dex->mon_count <= 0) {
        cursor = 0;
        scroll = 0;
        return;
    }
    if (cursor < 0)
        cursor = 0;
    if (cursor >= dex->mon_count)
        cursor = dex->mon_count - 1;
    if (cursor < scroll)
        scroll = cursor;
    if (cursor >= scroll + PAGE)
        scroll = cursor - PAGE + 1;
}

static void keep_selection(Dex *dex, uint16_t order)
{
    int i;
    cursor = 0;
    for (i = 0; i < dex->mon_count; i++) {
        if (dex->mons[i].order == order) {
            cursor = i;
            break;
        }
    }
    clamp_view(dex);
}

void ui_run(Dex *dex, void (*rescan)(Dex *dex))
{
    uint16_t order = 0;
    int have_order = 0;

    keysSetRepeat(16, 5);
    dex_sort(dex, by_species);
    clamp_view(dex);
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
            ui_status("Scanning the card...");
            rescan(dex);
            dex_sort(dex, by_species);
            cursor = 0;
            scroll = 0;
            dirty = 1;
        }

        if (hit & KEY_X && dex->mon_count > 0) {
            order = dex->mons[cursor].order;
            have_order = 1;
            by_species = !by_species;
            dex_sort(dex, by_species);
            if (have_order)
                keep_selection(dex, order);
            dirty = 1;
        }

        if (dex->mon_count > 0 && (down & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R))) {
            if (down & KEY_UP)
                cursor--;
            if (down & KEY_DOWN)
                cursor++;
            if (down & KEY_LEFT)
                cursor -= 5;
            if (down & KEY_RIGHT)
                cursor += 5;
            if (down & KEY_L)
                cursor -= PAGE;
            if (down & KEY_R)
                cursor += PAGE;
            clamp_view(dex);
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
