/* menu/src/artcache.h -- the cover cache's bookkeeping: which item each VDP1 VRAM cover slot holds,
 * least-recently-used eviction that never picks a pinned (displayed or uploading) slot, a small ring of items known
 * to have no art, and the prefetch window around the cursor. Pure C (host-tested); menu/ui/art.c owns the VRAM.
 * BSD-3-Clause. */
#ifndef OM_ARTCACHE_H
#define OM_ARTCACHE_H
#include <stdint.h>
#define OM_AC_MAX    8
#define OM_AC_NOART  16
#define OM_AC_NONE   0xFFFFFFFFu
typedef struct {
    uint32_t key[OM_AC_MAX], stamp[OM_AC_MAX], clock;
    uint8_t meta[OM_AC_MAX], n;
    uint32_t noart[OM_AC_NOART];
    uint8_t noart_next;
} om_artcache;
void om_ac_init(om_artcache *a, unsigned nslots);
int om_ac_find(const om_artcache *a, uint32_t key);              /* slot, or -1 */
void om_ac_touch(om_artcache *a, int slot);
int om_ac_victim(const om_artcache *a, uint32_t pin_mask);       /* an empty slot first, else the LRU; -1 if all pinned */
void om_ac_set(om_artcache *a, int slot, uint32_t key, uint8_t meta);
void om_ac_drop(om_artcache *a, int slot);
void om_ac_drop_if(om_artcache *a, int (*pred)(uint32_t key));  /* e.g. the theme-colored tiles on a theme change */
void om_ac_noart_add(om_artcache *a, uint32_t key);
int om_ac_noart(const om_artcache *a, uint32_t key);
/* The prefetch window: the next index to fetch in the order cur, cur+1, cur-1, cur+2, cur-2 ... (radius r), within
 * [0, n), that need() says still needs fetching; -1 when none. */
int om_pf_next(int cur, int n, int r, int (*need)(void *ctx, int idx), void *ctx);
#endif
