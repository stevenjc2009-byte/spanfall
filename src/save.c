#include "save.h"

#include <stdio.h>
#include <string.h>

#define PATH_CAP 128

static uint32_t fnv1a(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void save_encode(uint32_t best, uint8_t out[SAVE_SIZE])
{
    memcpy(out, "SPNF", 4);
    put_u32(out + 4, best);
    put_u32(out + 8, fnv1a(out, 8));
}

int save_decode(const uint8_t *in, size_t n, uint32_t *best)
{
    if (n != SAVE_SIZE || memcmp(in, "SPNF", 4) != 0 || get_u32(in + 8) != fnv1a(in, 8))
        return -1;
    *best = get_u32(in + 4);
    return 0;
}

/* 1 = valid (*best set), 0 = no such file, -1 = present but unreadable. */
static int load_one(const char *path, uint32_t *best)
{
    uint8_t buf[SAVE_SIZE + 1];
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    return save_decode(buf, n, best) == 0 ? 1 : -1;
}

static int suffixed(char *out, const char *path, const char *suffix)
{
    int n = snprintf(out, PATH_CAP, "%s%s", path, suffix);
    return n > 0 && n < PATH_CAP ? 0 : -1;
}

SaveStatus save_load(const char *path, uint32_t *best)
{
    static const char *const SUFFIXES[3] = {"", ".tmp", ".bak"};
    char p[PATH_CAP];
    int seen = 0;
    for (int i = 0; i < 3; i++) {
        uint32_t v;
        if (suffixed(p, path, SUFFIXES[i]) != 0)
            continue;
        int r = load_one(p, &v);
        if (r == 1) {
            *best = v;
            return SAVE_OK;
        }
        seen |= r != 0;
    }
    *best = 0;
    return seen ? SAVE_CORRUPT : SAVE_MISSING;
}

static int exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

int save_write(const char *path, uint32_t best)
{
    char tmp[PATH_CAP], bak[PATH_CAP];
    uint8_t blob[SAVE_SIZE];
    uint32_t check;

    if (suffixed(tmp, path, ".tmp") != 0 || suffixed(bak, path, ".bak") != 0)
        return -1;
    save_encode(best, blob);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return -1;
    size_t n = fwrite(blob, 1, SAVE_SIZE, f);
    if (fclose(f) != 0 || n != SAVE_SIZE || load_one(tmp, &check) != 1 || check != best) {
        remove(tmp);
        return -1;
    }
    if (exists(path)) {
        remove(bak);
        if (rename(path, bak) != 0)
            return -1; /* the verified .tmp stays; save_load() finds it */
    }
    return rename(tmp, path) == 0 ? 0 : -1;
}
