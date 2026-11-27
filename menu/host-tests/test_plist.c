/* menu/host-tests/test_plist.c -- FAVS/RECENT model over the tiny.library.idx fixture (paths:
 * 0 Racing/Daytona USA/daytona.cue, 1 RPG/Grandia/disc1.cue, 2 RPG/Grandia/disc2.cue, 3 unknown.iso). BSD-3-Clause. */
#include "th.h"
#include "msort.h"
#include "plist.h"

static int cmp_u16(const void *ctx, uint16_t a, uint16_t b)
{
    const uint16_t *key = (const uint16_t *)ctx;
    return (int)key[a] - (int)key[b];
}

int main(void)
{
    uint32_t n, next;
    uint8_t *f = th_load(FIX("tiny.library.idx"), &n);
    om_libidx l;
    om_pathidx px;
    om_plist p;
    uint16_t order[8], tmp[8], a[7] = { 0, 1, 2, 3, 4, 5, 6 }, t7[7];
    static const uint16_t key[7] = { 5, 3, 5, 1, 3, 0, 5 };
    char out[256];
    const char *favs = "unknown.iso\r\nRacing/Daytona USA/daytona.cue\n# note\n\nmissing/x.cue\nunknown.iso\n";
    om_msort(a, 7, t7, cmp_u16, key);                            /* stable: equal keys keep input order */
    CHECK_INT(a[0], 5); CHECK_INT(a[1], 3); CHECK_INT(a[2], 1); CHECK_INT(a[3], 4);
    CHECK_INT(a[4], 0); CHECK_INT(a[5], 2); CHECK_INT(a[6], 6);
    {
        uint16_t zero[1] = { 9 }, zerotmp[1], one[1] = { 42 }, onetmp[1];
        om_msort(zero, 0, zerotmp, cmp_u16, key);                /* n=0: no-op, cmp never called */
        CHECK_INT(zero[0], 9);
        om_msort(one, 1, onetmp, cmp_u16, key);                  /* n=1: no-op, cmp never called */
        CHECK_INT(one[0], 42);
    }
    CHECK_INT(om_libidx_open(&l, f, n), OM_OK);
    om_pathidx_build(&px, &l, order, tmp);
    CHECK_INT(om_pathidx_find(&px, "RPG/Grandia/disc2.cue"), 2);
    CHECK_INT(om_pathidx_find(&px, "unknown.iso"), 3);
    CHECK_INT(om_pathidx_find(&px, "nope"), -1);
    om_plist_init(&p, OM_FAVS_MAX);
    CHECK_INT(om_plist_parse(&p, &px, favs, (uint32_t)strlen(favs)), OM_OK);
    CHECK_INT(p.n, 2); CHECK_INT(p.idx[0], 3); CHECK_INT(p.idx[1], 0);
    CHECK_INT(p.dropped, 2);                                     /* missing path + duplicate (plan Q3) */
    CHECK_INT(om_plist_toggle(&p, 3), 0);                        /* remove */
    CHECK_INT(om_plist_toggle(&p, 1), 1);                        /* add at the end */
    CHECK_INT(p.n, 2); CHECK_INT(p.idx[0], 0); CHECK_INT(p.idx[1], 1);
    next = 0;
    CHECK_INT(om_plist_emit(&p, &l, &next, out, sizeof out), 53);   /* 30 + 1 and 21 + 1 bytes */
    out[53] = 0;
    CHECK_STR(out, "Racing/Daytona USA/daytona.cue\nRPG/Grandia/disc1.cue\n");
    CHECK_INT(next, 2);
    next = 0;
    CHECK_INT(om_plist_emit(&p, &l, &next, out, 40), 31);        /* whole lines only */
    CHECK_INT(next, 1);
    om_plist_init(&p, 3);                                        /* recent, capacity 3 */
    om_plist_push_front(&p, 0); om_plist_push_front(&p, 1); om_plist_push_front(&p, 2);
    om_plist_push_front(&p, 1);                                  /* moves to the front, no duplicate */
    CHECK_INT(p.n, 3); CHECK_INT(p.idx[0], 1); CHECK_INT(p.idx[1], 2); CHECK_INT(p.idx[2], 0);
    om_plist_push_front(&p, 3);                                  /* oldest (0) falls off */
    CHECK_INT(p.n, 3); CHECK_INT(p.idx[0], 3); CHECK_INT(p.idx[2], 2);
    om_plist_init(&p, 1);
    CHECK_INT(om_plist_toggle(&p, 0), 1);
    CHECK_INT(om_plist_toggle(&p, 1), OM_E_FULL);
    om_plist_init(&p, 0);                                        /* clamped up, never down to 0 */
    CHECK_INT(p.cap, 1);
    {
        om_plist z;
        om_plist_init(&z, 5);
        z.cap = 0;                                                /* force the historically-unreachable case */
        om_plist_push_front(&z, 1);                               /* must be a safe no-op, not an underflow */
        CHECK_INT(z.n, 0);
    }
    free(f);
    return th_done("test_plist");
}
