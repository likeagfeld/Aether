/* menu/src/artcache.c -- see artcache.h. BSD-3-Clause. */
#include "artcache.h"

void om_ac_init(om_artcache *a, unsigned nslots)
{
    unsigned i;
    a->n = (uint8_t)(nslots > OM_AC_MAX ? OM_AC_MAX : nslots);
    a->clock = 0;
    for (i = 0; i < OM_AC_MAX; i++) { a->key[i] = OM_AC_NONE; a->stamp[i] = 0; a->meta[i] = 0; }
    for (i = 0; i < OM_AC_NOART; i++) a->noart[i] = OM_AC_NONE;
    a->noart_next = 0;
}

int om_ac_find(const om_artcache *a, uint32_t key)
{
    int i;
    if (key == OM_AC_NONE) return -1;
    for (i = 0; i < a->n; i++)
        if (a->key[i] == key) return i;
    return -1;
}

void om_ac_touch(om_artcache *a, int slot) { if (slot >= 0 && slot < a->n) a->stamp[slot] = ++a->clock; }

int om_ac_victim(const om_artcache *a, uint32_t pin_mask)
{
    int i, best = -1;
    for (i = 0; i < a->n; i++) {
        if (pin_mask & (1u << i)) continue;
        if (a->key[i] == OM_AC_NONE) return i;
        if (best < 0 || a->stamp[i] < a->stamp[best]) best = i;
    }
    return best;
}

void om_ac_set(om_artcache *a, int slot, uint32_t key, uint8_t meta)
{
    if (slot < 0 || slot >= a->n) return;
    a->key[slot] = key;
    a->meta[slot] = meta;
    om_ac_touch(a, slot);
}

void om_ac_drop(om_artcache *a, int slot) { if (slot >= 0 && slot < a->n) a->key[slot] = OM_AC_NONE; }

void om_ac_drop_if(om_artcache *a, int (*pred)(uint32_t key))
{
    int i;
    for (i = 0; i < a->n; i++)
        if (a->key[i] != OM_AC_NONE && pred(a->key[i])) a->key[i] = OM_AC_NONE;
}

void om_ac_noart_add(om_artcache *a, uint32_t key)
{
    if (om_ac_noart(a, key)) return;
    a->noart[a->noart_next] = key;
    a->noart_next = (uint8_t)((a->noart_next + 1u) % OM_AC_NOART);
}

int om_ac_noart(const om_artcache *a, uint32_t key)
{
    int i;
    if (key == OM_AC_NONE) return 0;
    for (i = 0; i < OM_AC_NOART; i++)
        if (a->noart[i] == key) return 1;
    return 0;
}

int om_pf_next(int cur, int n, int r, int (*need)(void *ctx, int idx), void *ctx)
{
    int d;
    if (cur < 0 || cur >= n) return -1;
    for (d = 0; d <= 2 * r; d++) {
        int i = d == 0 ? cur : (d & 1) ? cur + (d + 1) / 2 : cur - d / 2;
        if (i >= 0 && i < n && need(ctx, i)) return i;
    }
    return -1;
}
