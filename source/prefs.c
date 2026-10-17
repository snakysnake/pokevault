#include "prefs.h"
#include "crypto.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum {
    PREFS_VERSION = 1,
    PREFS_HEADER = 8,
    PREFS_FILE_MAX = 24576
};

/* Set once a load has decided whether this build may replace the file. */
static int may_store;
static char store_root[8];

static int same_path(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int path_on_card(const Dex *dex, const char *path)
{
    int i;
    if (!dex || !path || path[0] == 0)
        return 0;
    for (i = 0; i < dex->save_count; i++) {
        if (same_path(dex->saves[i].path, path))
            return 1;
    }
    return 0;
}

static int listed(const PrefsHidden *list, int n, const char *path)
{
    int i;
    for (i = 0; i < n; i++) {
        if (same_path(list[i].path, path))
            return 1;
    }
    return 0;
}

static void copy_path(char *dst, const char *src)
{
    size_t i;
    for (i = 0; i < SAVE_PATH_MAX - 1 && src && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

static int append_extra(Prefs *prefs, const uint8_t *rec, size_t n)
{
    if (prefs->extra_len < 0 || n > (size_t)(PREFS_EXTRA_MAX - prefs->extra_len))
        return 0;
    memcpy(prefs->extra + prefs->extra_len, rec, n);
    prefs->extra_len += (int)n;
    return 1;
}

/* 1 stored, 0 not a path this build applies (caller keeps the record),
   -1 the hidden list is full and the file must be left alone. */
static int take_hidden(Prefs *prefs, const uint8_t *payload, size_t size)
{
    char path[SAVE_PATH_MAX];
    size_t i;
    if (size == 0 || size >= SAVE_PATH_MAX)
        return 0;
    for (i = 0; i < size; i++) {
        if (payload[i] == 0)
            return 0;
    }
    memcpy(path, payload, size);
    path[size] = 0;
    if (listed(prefs->hidden, prefs->hidden_count, path))
        return 1;
    if (prefs->hidden_count >= PREFS_HIDDEN_MAX)
        return -1;
    memcpy(prefs->hidden[prefs->hidden_count].path, path, size + 1);
    prefs->hidden_count++;
    return 1;
}

bool prefs_decode(Prefs *prefs, const uint8_t *data, size_t len)
{
    uint16_t version;
    uint16_t header_size;
    uint16_t crc;
    size_t records;
    size_t at;
    if (!prefs)
        return false;
    memset(prefs, 0, sizeof *prefs);
    if (!data || len < 8 + PREFS_HEADER)
        return false;
    if (data[0] != 'P' || data[1] != 'V' || data[2] != 'S' || data[3] != 'T')
        return false;
    version = pv_read16(data + 4);
    header_size = pv_read16(data + 6);
    /* Another container generation. Leave it for a build that knows it. */
    if (version != PREFS_VERSION)
        return false;
    if (header_size < PREFS_HEADER || header_size > 1024)
        return false;
    if (len < 8u + header_size)
        return false;
    records = 8u + header_size;
    crc = pv_read16(data + 8 + 4);
    if (crc != pv_crc16_ccitt(data + records, len - records))
        return false;

    at = records;
    while (at < len) {
        uint16_t type;
        uint16_t size;
        int taken;
        if (len - at < 4) {
            memset(prefs, 0, sizeof *prefs);
            return false;
        }
        type = pv_read16(data + at);
        size = pv_read16(data + at + 2);
        if ((size_t)size > len - at - 4) {
            memset(prefs, 0, sizeof *prefs);
            return false;
        }
        if (type == PREFS_TYPE_DESIGN && size == 1) {
            prefs->design = data[at + 4];
        } else if (type == PREFS_TYPE_HIDDEN) {
            taken = take_hidden(prefs, data + at + 4, size);
            if (taken < 0) {
                memset(prefs, 0, sizeof *prefs);
                return false;
            }
            if (taken == 0 && !append_extra(prefs, data + at, 4u + size)) {
                memset(prefs, 0, sizeof *prefs);
                return false;
            }
        } else if (!append_extra(prefs, data + at, 4u + size)) {
            memset(prefs, 0, sizeof *prefs);
            return false;
        }
        at += 4u + size;
    }
    return true;
}

static int write_record(uint8_t *out, size_t cap, size_t *at, uint16_t type,
                        const uint8_t *payload, size_t size)
{
    if (size > 0xFFFF || *at > cap || cap - *at < 4u + size)
        return 0;
    pv_write16(out + *at, type);
    pv_write16(out + *at + 2, (uint16_t)size);
    if (size)
        memcpy(out + *at + 4, payload, size);
    *at += 4u + size;
    return 1;
}

size_t prefs_encode(const Prefs *prefs, uint8_t *out, size_t cap)
{
    size_t at;
    size_t i;
    int n;
    if (!prefs || !out || cap < 8u + PREFS_HEADER)
        return 0;
    out[0] = 'P';
    out[1] = 'V';
    out[2] = 'S';
    out[3] = 'T';
    pv_write16(out + 4, PREFS_VERSION);
    pv_write16(out + 6, PREFS_HEADER);
    pv_write32(out + 8, 0);
    pv_write16(out + 12, 0);
    pv_write16(out + 14, 0);
    at = 8u + PREFS_HEADER;
    n = prefs->hidden_count;
    if (n < 0)
        n = 0;
    if (n > PREFS_HIDDEN_MAX)
        n = PREFS_HIDDEN_MAX;
    for (i = 0; i < (size_t)n; i++) {
        size_t len = strlen(prefs->hidden[i].path);
        if (len == 0 || len >= SAVE_PATH_MAX)
            continue;
        if (!write_record(out, cap, &at, PREFS_TYPE_HIDDEN,
                          (const uint8_t *)prefs->hidden[i].path, len))
            return 0;
    }
    if (prefs->extra_len < 0 || prefs->extra_len > PREFS_EXTRA_MAX)
        return 0;
    if (prefs->extra_len > 0) {
        if ((size_t)prefs->extra_len > cap - at)
            return 0;
        memcpy(out + at, prefs->extra, (size_t)prefs->extra_len);
        at += (size_t)prefs->extra_len;
    }
    if (!write_record(out, cap, &at, PREFS_TYPE_DESIGN, &prefs->design, 1))
        return 0;
    pv_write16(out + 12, pv_crc16_ccitt(out + 16, at - 16));
    return at;
}

void prefs_apply(const Prefs *prefs, const Dex *dex, int *shown, int count)
{
    int i;
    int h;
    if (!prefs || !dex || !shown || count <= 0)
        return;
    if (count > dex->save_count)
        count = dex->save_count;
    for (i = 0; i < count; i++) {
        if (dex->saves[i].path[0] == 0)
            continue;
        for (h = 0; h < prefs->hidden_count; h++) {
            if (same_path(dex->saves[i].path, prefs->hidden[h].path)) {
                shown[i] = 0;
                break;
            }
        }
    }
}

void prefs_capture(Prefs *prefs, const Dex *dex, const int *shown, int count)
{
    static PrefsHidden next[PREFS_HIDDEN_MAX];
    int n = 0;
    int i;
    if (!prefs)
        return;
    if (dex && shown && count > 0) {
        if (count > dex->save_count)
            count = dex->save_count;
        for (i = 0; i < count && n < PREFS_HIDDEN_MAX; i++) {
            if (shown[i])
                continue;
            if (dex->saves[i].path[0] == 0)
                continue;
            if (listed(next, n, dex->saves[i].path))
                continue;
            copy_path(next[n].path, dex->saves[i].path);
            n++;
        }
    }
    for (i = 0; i < prefs->hidden_count && n < PREFS_HIDDEN_MAX; i++) {
        if (prefs->hidden[i].path[0] == 0)
            continue;
        if (path_on_card(dex, prefs->hidden[i].path))
            continue;
        if (listed(next, n, prefs->hidden[i].path))
            continue;
        copy_path(next[n].path, prefs->hidden[i].path);
        n++;
    }
    memset(prefs->hidden, 0, sizeof prefs->hidden);
    if (n > 0)
        memcpy(prefs->hidden, next, (size_t)n * sizeof next[0]);
    prefs->hidden_count = n;
}

static const char *card_roots[] = { "fat:", "sd:", NULL };
static uint8_t file_buf[PREFS_FILE_MAX];

static void remember_root(const char *root)
{
    size_t i;
    for (i = 0; i < sizeof store_root - 1 && root && root[i]; i++)
        store_root[i] = root[i];
    store_root[i] = 0;
}

/* mkdir faults when the drive in the path is not mounted. opendir reports
   that case and returns. */
static int volume_ok(const char *root)
{
    char probe[16];
    DIR *dir;
    int wrote;
    if (!root || root[0] == 0)
        return 0;
    wrote = snprintf(probe, sizeof probe, "%s/", root);
    if (wrote <= 0 || (size_t)wrote >= sizeof probe)
        return 0;
    dir = opendir(probe);
    if (!dir)
        return 0;
    closedir(dir);
    return 1;
}

static void make_store(const char *root)
{
    char path[64];
    snprintf(path, sizeof path, "%s/_nds", root);
    (void)mkdir(path, 0777);
    snprintf(path, sizeof path, "%s/_nds/pokevault", root);
    (void)mkdir(path, 0777);
}

/* 1 read, 0 missing, -1 present but larger than this build will rewrite. */
static int read_store(const char *path, size_t *out)
{
    FILE *f;
    size_t n;
    int extra;
    f = fopen(path, "rb");
    if (!f)
        return 0;
    n = fread(file_buf, 1, sizeof file_buf, f);
    extra = fgetc(f);
    fclose(f);
    if (extra != EOF)
        return -1;
    *out = n;
    return 1;
}

static int write_store(const char *root, size_t n)
{
    char final[64];
    char tmp[72];
    FILE *f;
    if (!volume_ok(root))
        return 0;
    make_store(root);
    snprintf(final, sizeof final, "%s/_nds/pokevault/settings", root);
    snprintf(tmp, sizeof tmp, "%s/_nds/pokevault/settings.tmp", root);
    f = fopen(tmp, "wb");
    if (!f)
        return 0;
    if (fwrite(file_buf, 1, n, f) != n) {
        fclose(f);
        remove(tmp);
        return 0;
    }
    if (fclose(f) != 0) {
        remove(tmp);
        return 0;
    }
    remove(final);
    if (rename(tmp, final) != 0) {
        remove(tmp);
        return 0;
    }
    remember_root(root);
    return 1;
}

bool prefs_load(Prefs *prefs)
{
    int i;
    may_store = 0;
    store_root[0] = 0;
    if (!prefs)
        return false;
    memset(prefs, 0, sizeof *prefs);
    for (i = 0; card_roots[i]; i++) {
        char path[64];
        size_t n = 0;
        int got;
        snprintf(path, sizeof path, "%s/_nds/pokevault/settings", card_roots[i]);
        got = read_store(path, &n);
        if (got == 0)
            continue;
        remember_root(card_roots[i]);
        if (got < 0 || !prefs_decode(prefs, file_buf, n))
            return false;
        may_store = 1;
        return true;
    }
    may_store = 1;
    return false;
}

static const char *root_for(const char *path)
{
    int i;
    static const char *prefix[] = { "sd:", "fat:", "nand:", NULL };
    if (!path)
        return NULL;
    for (i = 0; prefix[i]; i++) {
        size_t n = strlen(prefix[i]);
        if (strncmp(path, prefix[i], n) == 0 && volume_ok(prefix[i]))
            return prefix[i];
    }
    return NULL;
}

bool prefs_commit(const Prefs *prefs)
{
    const char *root = NULL;
    size_t n;
    int i;
    if (!prefs || !may_store)
        return false;
    n = prefs_encode(prefs, file_buf, sizeof file_buf);
    if (n == 0)
        return false;
    if (store_root[0] && volume_ok(store_root))
        root = store_root;
    for (i = 0; !root && i < prefs->hidden_count; i++)
        root = root_for(prefs->hidden[i].path);
    if (root)
        return write_store(root, n) != 0;
    for (i = 0; card_roots[i]; i++) {
        if (write_store(card_roots[i], n))
            return true;
    }
    return false;
}
