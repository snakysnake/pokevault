#include "music.h"
#include "scan.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int same_name(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int skip_dir(const char *name)
{
    static const char *skip[] = {
        "_nds", "hiya", "gm9", "dcim", "private", NULL
    };
    int i;
    for (i = 0; skip[i]; i++) {
        if (same_name(name, skip[i]))
            return 1;
    }
    return 0;
}

static int is_sav(const char *name)
{
    size_t n = strlen(name);
    char a, b, c;
    if (n < 5 || name[0] == '.')
        return 0;
    a = name[n - 3];
    b = name[n - 2];
    c = name[n - 1];
    if (name[n - 4] != '.')
        return 0;
    if (a >= 'A' && a <= 'Z')
        a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z')
        b = (char)(b - 'A' + 'a');
    if (c >= 'A' && c <= 'Z')
        c = (char)(c - 'A' + 'a');
    return a == 's' && b == 'a' && c == 'v';
}

static void display_name(const char *path, char *out, size_t cap)
{
    const char *base = path;
    const char *p;
    size_t n;
    for (p = path; *p; p++) {
        if (*p == '/')
            base = p + 1;
    }
    n = strlen(base);
    if (n >= 4 && is_sav(base))
        n -= 4;
    if (n >= cap)
        n = cap - 1;
    memcpy(out, base, n);
    out[n] = 0;
}

static int join_path(char *dst, size_t cap, const char *dir, const char *name)
{
    size_t dl = strlen(dir);
    int wrote;
    if (dl > 0 && dir[dl - 1] == '/')
        wrote = snprintf(dst, cap, "%s%s", dir, name);
    else
        wrote = snprintf(dst, cap, "%s/%s", dir, name);
    return wrote > 0 && (size_t)wrote < cap;
}

static void read_sav(Dex *dex, const char *path, uint8_t *buf, size_t cap, ScanProgress progress)
{
    FILE *f;
    size_t n;
    char name[32];
    char msg[64];

    /* Read-only. This app never opens a save for writing. */
    f = fopen(path, "rb");
    if (!f)
        return;
    n = 0;
    while (n < cap) {
        size_t want = cap - n;
        size_t got;
        if (want > 16384)
            want = 16384;
        got = fread(buf + n, 1, want, f);
        if (got == 0)
            break;
        n += got;
        /* The soundtrack stream has to be refilled while this read holds the card. */
        music_pump();
    }
    fclose(f);
    if (n == 0)
        return;

    dex->files_seen++;
    display_name(path, name, sizeof name);
    snprintf(msg, sizeof msg, "Reading %s", name);
    if (progress)
        progress(msg);
    save_read(dex, name, buf, n);
}

static void walk(Dex *dex, const char *path, int depth, uint8_t *buf, size_t cap, ScanProgress progress)
{
    DIR *dir;
    struct dirent *ent;

    if (depth > 8 || dex->save_count >= MAX_SAVES)
        return;
    dir = opendir(path);
    if (!dir)
        return;

    while ((ent = readdir(dir)) != NULL) {
        char child[512];
        music_pump();
        struct stat st;
        if (ent->d_name[0] == '.')
            continue;
        if (!join_path(child, sizeof child, path, ent->d_name))
            continue;
        if (stat(child, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (!skip_dir(ent->d_name))
                walk(dex, child, depth + 1, buf, cap, progress);
        } else if (S_ISREG(st.st_mode) && is_sav(ent->d_name)) {
            read_sav(dex, child, buf, cap, progress);
        }
    }
    closedir(dir);
}

void scan_saves(Dex *dex, ScanProgress progress)
{
    uint8_t *buf = malloc(0x80000 + 0x400);
    if (!buf) {
        if (progress)
            progress("Out of memory");
        return;
    }
    if (progress)
        progress("Scanning fat:/");
    walk(dex, "fat:/", 0, buf, 0x80000 + 0x400, progress);
    if (dex->save_count == 0) {
        if (progress)
            progress("Scanning sd:/");
        walk(dex, "sd:/", 0, buf, 0x80000 + 0x400, progress);
    }
    free(buf);
}
