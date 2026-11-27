/* menu/src/fakefs.c -- see fakefs.h. BSD-3-Clause. */
#include <string.h>
#include "fakefs.h"
#include "om_fmt.h"

static const char *path_at(const om_fakefs *f, uint32_t i) { return (const char *)(f->base + 16u + i * OM_FAKEFS_ENT); }
static const char *norm(const char *p) { while (*p == '/') p++; return p; }

static uint32_t trimmed_len(const char *p)
{
    uint32_t n = (uint32_t)strlen(p);
    while (n && p[n - 1] == '/') n--;
    return n;
}

static int under(const char *p, const char *dir, uint32_t dl)
{
    return dl == 0 || (!strncmp(p, dir, dl) && p[dl] == '/');
}

int om_fakefs_open(om_fakefs *f, const uint8_t *blob, uint32_t len)
{
    uint32_t i, n;
    if (len < 16 || memcmp(blob, "OMFAKEFS", 8))
        return OM_E_MAGIC;
    n = om_be32(blob + 8);
    if (n > (len - 16) / OM_FAKEFS_ENT)
        return OM_E_RANGE;
    for (i = 0; i < n; i++) {
        const uint8_t *e = blob + 16 + i * OM_FAKEFS_ENT;
        if (e[255] || !om_in_range(om_be32(e + 256), om_be32(e + 260), len))
            return OM_E_FIELD;
    }
    f->base = blob;
    f->len = len;
    f->count = n;
    return OM_OK;
}

int om_fakefs_find(const om_fakefs *f, const char *path, uint32_t *off, uint32_t *size)
{
    uint32_t i, pl;
    path = norm(path);
    pl = trimmed_len(path);
    for (i = 0; i < f->count; i++) {
        const char *p = path_at(f, i);
        if (strlen(p) == pl && !strncmp(p, path, pl)) {
            *off = om_be32((const uint8_t *)p + 256);
            *size = om_be32((const uint8_t *)p + 260);
            return 1;
        }
    }
    for (i = 0; i < f->count; i++)
        if (under(path_at(f, i), path, pl))
            return 2;
    return 0;
}

int om_fakefs_child(const om_fakefs *f, const char *dir, uint32_t want, char name[256], int *is_dir)
{
    uint32_t i, j, dl, found = 0;
    dir = norm(dir);
    dl = trimmed_len(dir);
    for (i = 0; i < f->count; i++) {
        const char *p = path_at(f, i), *c, *slash;
        uint32_t cl;
        int dup = 0;
        if (!under(p, dir, dl))
            continue;
        c = dl ? p + dl + 1 : p;
        slash = strchr(c, '/');
        cl = slash ? (uint32_t)(slash - c) : (uint32_t)strlen(c);
        for (j = 0; j < i && !dup; j++) {
            const char *q = path_at(f, j), *d;
            if (!under(q, dir, dl))
                continue;
            d = dl ? q + dl + 1 : q;
            dup = !strncmp(d, c, cl) && (d[cl] == '/' || d[cl] == 0);
        }
        if (dup)
            continue;
        if (found++ == want) {
            memcpy(name, c, cl);
            name[cl] = 0;
            *is_dir = slash != NULL;
            return 1;
        }
    }
    return 0;
}
