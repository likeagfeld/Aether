/* menu/host-tests/test_safearea.c -- the safe-area edge transform. BSD-3-Clause. */
#include "th.h"
#include <math.h>
#include "safearea.h"

int main(void)
{
    om_safe t;
    int lv, e, r[4], bad = 0, prev;
    om_safe_set(&t, OM_OVS_STD);
    CHECK_INT(t.ident, 1);
    for (e = 0; e <= 320; e++) if (om_safe_x(&t, e) != e) bad++;
    for (e = 0; e <= 224; e++) if (om_safe_y(&t, e) != e) bad++;
    CHECK_INT(bad, 0);                                                   /* Standard: the identity */
    {   /* and by the formula too, not only by the short-circuit flag */
        om_safe u = t;
        u.ident = 0;
        for (e = 0; e <= 320; e++) if (om_safe_x(&u, e) != e) bad++;
        for (e = 0; e <= 224; e++) if (om_safe_y(&u, e) != e) bad++;
        CHECK_INT(bad, 0);
    }
    om_safe_set(&t, OM_OVS_MAX);
    CHECK_INT(t.m, 24); CHECK_INT(t.ident, 0);
    CHECK_INT(om_safe_x(&t, 8), 24); CHECK_INT(om_safe_x(&t, 312), 296);
    CHECK_INT(om_safe_y(&t, 8), 24); CHECK_INT(om_safe_y(&t, 216), 200);
    om_safe_rect(&t, 8, 8, 311, 215, r);                                 /* the design safe box -> 24 px margin */
    CHECK_INT(r[0], 24); CHECK_INT(r[1], 24); CHECK_INT(r[2], 295); CHECK_INT(r[3], 199);
    CHECK_INT(om_safe_x(&t, 9), 25); CHECK_INT(om_safe_y(&t, 9), 25);   /* 0.89 and 0.85 px round up, not down */
    for (lv = 0; lv < OM_OVS_COUNT; lv++) {                              /* every edge against a double-precision */
        int m = om_safe_margin((uint8_t)lv);                             /* reference: m + floor(x + 0.5), clamped */
        om_safe_set(&t, (uint8_t)lv);
        for (e = 0; e <= 320; e++) {
            double x = m + (double)(e - 8) * (320 - 2 * m) / 304.0;
            int ref = (int)floor(x + 0.5);
            ref = ref < 0 ? 0 : ref > 320 ? 320 : ref;
            if (om_safe_x(&t, e) != ref) bad++;
        }
        for (e = 0; e <= 224; e++) {
            double y = m + (double)(e - 8) * (224 - 2 * m) / 208.0;
            int ref = (int)floor(y + 0.5);
            ref = ref < 0 ? 0 : ref > 224 ? 224 : ref;
            if (om_safe_y(&t, e) != ref) bad++;
        }
    }
    CHECK_INT(bad, 0);
    om_safe_set(&t, OM_OVS_OFF);
    om_safe_rect(&t, 8, 8, 311, 215, r);
    CHECK_INT(r[0], 0); CHECK_INT(r[1], 0); CHECK_INT(r[2], 319); CHECK_INT(r[3], 223);
    CHECK_INT(om_safe_x(&t, 0), 0);                                      /* clamped, never negative */
    for (lv = 0; lv < OM_OVS_COUNT; lv++) {
        int a[4], b[4];
        om_safe_set(&t, (uint8_t)lv);
        prev = -1;
        for (e = 0; e <= 320; e++) { int v = om_safe_x(&t, e); if (v < prev || v < 0 || v > 320) bad++; prev = v; }
        prev = -1;
        for (e = 0; e <= 224; e++) { int v = om_safe_y(&t, e); if (v < prev || v < 0 || v > 224) bad++; prev = v; }
        for (e = 9; e < 300; e += 7) {                                   /* neighbors stay neighbors */
            om_safe_rect(&t, 8, 20, e, 40, a);
            om_safe_rect(&t, e + 1, 20, 311, 40, b);
            if (b[0] != a[2] + 1) bad++;
        }
    }
    CHECK_INT(bad, 0);
    for (lv = 0; lv < OM_OVS_COUNT; lv++) {   /* the unclamped forms (drawing): monotone off screen too, equal on it */
        int a[4], b[4];
        om_safe_set(&t, (uint8_t)lv);
        prev = -100000;
        for (e = -400; e <= 800; e++) { int v = om_safe_xu(&t, e); if (v < prev) bad++; prev = v; }
        prev = -100000;
        for (e = -400; e <= 800; e++) { int v = om_safe_yu(&t, e); if (v < prev) bad++; prev = v; }
        for (e = 0; e <= 320; e++) { int v = om_safe_xu(&t, e); bad += v >= 0 && v <= 320 && v != om_safe_x(&t, e); }
        for (e = 0; e <= 224; e++) { int v = om_safe_yu(&t, e); bad += v >= 0 && v <= 224 && v != om_safe_y(&t, e); }
        om_safe_rect_u(&t, -60, 20, -21, 40, a);           /* off the left edge: keeps its scaled width (40 px) */
        if (a[2] - a[0] + 1 < 35 || a[2] >= 0) bad++;
        om_safe_rect_u(&t, 330, 20, 409, 40, b);           /* the settings panel sliding in from the right */
        if (b[2] - b[0] + 1 < 70 || b[0] <= 300) bad++;
    }
    CHECK_INT(bad, 0);
    om_safe_set(&t, 77);
    CHECK_INT(t.level, OM_OVS_STD); CHECK_INT(t.ident, 1);              /* out of range -> Standard */
    CHECK_INT(g_om_safe.ident, 1);                                       /* the global starts at Standard */
    CHECK_INT(g_om_safe.level, OM_OVS_STD); CHECK_INT(g_om_safe.m, OM_SAFE_DESIGN);
    return th_done("test_safearea");
}
