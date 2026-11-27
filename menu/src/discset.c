/* menu/src/discset.c -- see discset.h. BSD-3-Clause. */
#include <stddef.h>
#include <string.h>
#include "discset.h"

static uint16_t flags_of(const om_libidx *l, uint32_t i) { return om_be16(l->table + 32 * i + 0x1A); }
static uint8_t disc_of(const om_libidx *l, uint32_t i) { return l->table[32 * i + 0x10]; }
static const char *path_of(const om_libidx *l, uint32_t i) { return l->pool + om_be32(l->table + 32 * i + 0x14); }

/* pick[d] for d = 1..9: idx for its own number, else the set's primary of d; OM_DISCS_NONE where none */
#define OM_DISCS_NONE 0xFFFFFFFFu
static uint32_t picks(const om_libidx *l, uint32_t idx, uint32_t pick[OM_HO_DISCS_MAX + 1])
{
    uint16_t set = OM_LIB_SET(flags_of(l, idx));
    uint32_t i, d, n = 0;
    for (d = 0; d <= OM_HO_DISCS_MAX; d++) pick[d] = OM_DISCS_NONE;
    if (!set) return 0;
    for (i = 0; i < l->count; i++) {
        uint16_t f = flags_of(l, i);
        d = disc_of(l, i);
        if (OM_LIB_SET(f) != set || !d || d > OM_HO_DISCS_MAX || (f & OM_LIB_ALT) || pick[d] != OM_DISCS_NONE) continue;
        pick[d] = i;
    }
    d = disc_of(l, idx);
    if (d && d <= OM_HO_DISCS_MAX) pick[d] = idx;
    for (d = 1; d <= OM_HO_DISCS_MAX; d++) n += pick[d] != OM_DISCS_NONE;
    return n;
}

uint32_t om_discset_lead(const om_libidx *l, uint32_t idx)
{
    uint32_t pick[OM_HO_DISCS_MAX + 1], d;
    if (idx >= l->count || picks(l, idx, pick) < 2) return idx;
    for (d = 1; d <= OM_HO_DISCS_MAX; d++)
        if (pick[d] != OM_DISCS_NONE) return pick[d];
    return idx;
}

uint32_t om_discset_fill(om_handoff *h, const om_libidx *l, uint32_t idx)
{
    uint32_t pick[OM_HO_DISCS_MAX + 1], d, n, k = 0, cur = 0, pre = 0, o = 0, len;
    const char *q0 = 0, *p;
    if (idx >= l->count || (n = picks(l, idx, pick)) < 2) return 0;
    for (d = 1; d <= OM_HO_DISCS_MAX; d++) {               /* the longest common folder prefix ("a/b/"), byte-wise */
        uint32_t m, s;
        if (pick[d] == OM_DISCS_NONE) continue;
        p = path_of(l, pick[d]);
        if (!q0) {
            q0 = p;
            pre = strrchr(p, '/') ? (uint32_t)(strrchr(p, '/') - p) + 1 : 0;
            continue;
        }
        for (m = 0, s = 0; m < pre && p[m] == q0[m]; m++)
            if (p[m] == '/') s = m + 1;
        pre = s;
    }
    if (pre > 255) return 0;                               /* dir: "/" + the prefix without its '/' */
    for (d = 1; d <= OM_HO_DISCS_MAX; d++) {               /* the pool, straight into the (zeroed) block */
        if (pick[d] == OM_DISCS_NONE) continue;
        p = path_of(l, pick[d]) + pre;
        len = (uint32_t)strlen(p);
        if (!len || len > 255 || o + len + 1 > OM_HO_DISC_POOL) {
            memset(h->discs, 0, o);                        /* does not fit: the block stays one disc */
            return 0;
        }
        memcpy(h->discs + o, p, len + 1);
        o += len + 1;
        if (pick[d] == idx) cur = k + 1;
        k++;
    }
    p = path_of(l, idx);
    if (!cur || strlen(p + pre) > 255) {                   /* idx not among the picks (a corrupt disc byte): one disc */
        memset(h->discs, 0, o);
        return 0;
    }
    memset(h->dir, 0, sizeof h->dir);
    h->dir[0] = '/';
    memcpy(h->dir + 1, p, pre ? pre - 1 : 0);
    memset(h->image, 0, sizeof h->image);
    strcpy(h->image, p + pre);
    h->ndisc = (uint8_t)n;
    h->cur = (uint8_t)cur;
    h->crc = om_crc32(0, h, (uint32_t)offsetof(om_handoff, crc));
    return n;
}
