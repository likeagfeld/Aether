/* menu/host-tests/test_artcache.c -- the cover cache's slot LRU and prefetch window. BSD-3-Clause. */
#include "th.h"
#include "artcache.h"

static int s_need[32];
static int need(void *ctx, int i) { (void)ctx; return s_need[i]; }
static int is_odd(uint32_t k) { return (int)(k & 1u); }

int main(void)
{
    om_artcache a;
    int i, v;
    om_ac_init(&a, 5);
    CHECK_INT(a.n, 5);
    CHECK_INT(om_ac_find(&a, 7), -1);
    CHECK_INT(om_ac_find(&a, OM_AC_NONE), -1);
    for (i = 0; i < 5; i++) {                                /* empty slots are used first, in order */
        v = om_ac_victim(&a, 0);
        CHECK_INT(v, i);
        om_ac_set(&a, v, 100u + (uint32_t)i, 0);
    }
    CHECK_INT(om_ac_find(&a, 102), 2);
    CHECK_INT(om_ac_victim(&a, 0), 0);                       /* full: the least recently used */
    om_ac_touch(&a, 0);
    CHECK_INT(om_ac_victim(&a, 0), 1);
    CHECK_INT(om_ac_victim(&a, 1u << 1), 2);                 /* a pinned (displayed) slot is never the victim */
    CHECK_INT(om_ac_victim(&a, 0x1Fu), -1);                  /* all pinned: none */
    om_ac_drop(&a, 3);
    CHECK_INT(om_ac_find(&a, 103), -1);
    CHECK_INT(om_ac_victim(&a, 0), 3);                       /* a dropped slot is empty again */
    om_ac_set(&a, 3, 7, 3);
    CHECK_INT(a.meta[3], 3);
    om_ac_drop_if(&a, is_odd);                               /* 101, 103(gone), 7 dropped; 100, 102, 104 kept */
    CHECK_INT(om_ac_find(&a, 7), -1);
    CHECK_INT(om_ac_find(&a, 101), -1);
    CHECK_INT(om_ac_find(&a, 104), 4);
    for (i = 0; i < 20; i++) om_ac_noart_add(&a, 500u + (uint32_t)i);   /* a ring of 16 */
    CHECK(!om_ac_noart(&a, 500));
    CHECK(om_ac_noart(&a, 519));
    CHECK(om_ac_noart(&a, 504));
    om_ac_init(&a, 99);
    CHECK_INT(a.n, OM_AC_MAX);
    /* the prefetch window: cur, +1, -1, +2, -2 */
    for (i = 0; i < 32; i++) s_need[i] = 1;
    CHECK_INT(om_pf_next(5, 10, 2, need, 0), 5);
    s_need[5] = 0;
    CHECK_INT(om_pf_next(5, 10, 2, need, 0), 6);
    s_need[6] = 0;
    CHECK_INT(om_pf_next(5, 10, 2, need, 0), 4);
    s_need[4] = 0;
    CHECK_INT(om_pf_next(5, 10, 2, need, 0), 7);
    s_need[7] = 0;
    CHECK_INT(om_pf_next(5, 10, 2, need, 0), 3);
    s_need[3] = 0;
    CHECK_INT(om_pf_next(5, 10, 2, need, 0), -1);            /* outside the radius: not fetched */
    CHECK_INT(om_pf_next(0, 10, 2, need, 0), 0);             /* clipped at the top */
    s_need[0] = 0; s_need[1] = 0;
    CHECK_INT(om_pf_next(0, 10, 2, need, 0), 2);
    CHECK_INT(om_pf_next(9, 10, 2, need, 0), 9);             /* clipped at the bottom */
    CHECK_INT(om_pf_next(0, 0, 2, need, 0), -1);             /* empty view */
    return th_done("test_artcache");
}
