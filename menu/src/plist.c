/* menu/src/plist.c -- see plist.h. BSD-3-Clause. */
#include <string.h>
#include "ini.h"
#include "msort.h"
#include "om_fmt.h"
#include "plist.h"

static int cmp_path(const void *ctx, uint16_t a, uint16_t b)
{
    const om_libidx *l = (const om_libidx *)ctx;
    om_lib_entry ea, eb;
    om_libidx_entry(l, a, &ea);
    om_libidx_entry(l, b, &eb);
    return strcmp(ea.path, eb.path);
}

void om_pathidx_build(om_pathidx *px, const om_libidx *l, uint16_t *order, uint16_t *tmp)
{
    uint32_t i;
    px->lib = l;
    px->order = order;
    for (i = 0; i < l->count; i++)
        order[i] = (uint16_t)i;
    om_msort(order, l->count, tmp, cmp_path, l);
}

int om_pathidx_find(const om_pathidx *px, const char *path)
{
    uint32_t lo = 0, hi = px->lib->count;
    om_lib_entry e;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        int c;
        om_libidx_entry(px->lib, px->order[mid], &e);
        c = strcmp(e.path, path);
        if (!c)
            return px->order[mid];
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return -1;
}

void om_plist_init(om_plist *p, uint16_t cap)
{
    p->n = p->dropped = 0;
    if (cap < 1)                                     /* cap 0 would underflow push_front's cap-1 */
        cap = 1;
    p->cap = cap > OM_FAVS_MAX ? (uint16_t)OM_FAVS_MAX : cap;
}

int om_plist_find(const om_plist *p, uint16_t e)
{
    uint16_t i;
    for (i = 0; i < p->n; i++)
        if (p->idx[i] == e)
            return i;
    return -1;
}

static void del(om_plist *p, uint16_t at)
{
    memmove(&p->idx[at], &p->idx[at + 1], (size_t)(p->n - at - 1) * sizeof p->idx[0]);
    p->n--;
}

int om_plist_toggle(om_plist *p, uint16_t e)
{
    int at = om_plist_find(p, e);
    if (at >= 0) { del(p, (uint16_t)at); return 0; }
    if (p->n >= p->cap)
        return OM_E_FULL;
    p->idx[p->n++] = e;
    return 1;
}

void om_plist_push_front(om_plist *p, uint16_t e)
{
    int at;
    if (!p->cap)                                      /* cap 0 has no room, not even for 1 entry */
        return;
    at = om_plist_find(p, e);
    if (at >= 0)
        del(p, (uint16_t)at);
    if (p->n >= p->cap)
        p->n = (uint16_t)(p->cap - 1);
    memmove(&p->idx[1], &p->idx[0], (size_t)p->n * sizeof p->idx[0]);
    p->idx[0] = e;
    p->n++;
}

int om_plist_parse(om_plist *p, const om_pathidx *px, const char *text, uint32_t len)
{
    char line[OM_INI_LINE + 2];
    uint32_t pos = 0;
    int r, e;
    while ((r = om_text_line(text, len, &pos, line)) > 0) {
        if (!line[0] || line[0] == '#')
            continue;
        e = om_pathidx_find(px, line);
        if (e < 0 || om_plist_find(p, (uint16_t)e) >= 0 || p->n >= p->cap) {
            p->dropped++;
            continue;
        }
        p->idx[p->n++] = (uint16_t)e;
    }
    return r < 0 ? r : OM_OK;
}

uint32_t om_plist_emit(const om_plist *p, const om_libidx *l, uint32_t *next, char *out, uint32_t cap)
{
    uint32_t o = 0;
    om_lib_entry e;
    while (*next < p->n) {
        uint32_t n;
        om_libidx_entry(l, p->idx[*next], &e);
        n = (uint32_t)strlen(e.path);
        if (o + n + 1 > cap)
            break;
        memcpy(out + o, e.path, n);
        out[o + n] = '\n';
        o += n + 1;
        (*next)++;
    }
    return o;
}
