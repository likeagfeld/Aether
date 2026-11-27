/* menu/host-tests/test_saver.c -- the screensaver logo stays inside the safe area at every
 * level, bounces off its edges, and spins (a horizontal squash) on a bounce and every 8 s. BSD-3-Clause. */
#include "th.h"
#include "saver.h"

int main(void)
{
    om_saver s, s2;
    om_saver_pose p, p2;
    uint32_t f, bad = 0, flips = 0, first_narrow = 0, same = 1, m, spins = 0, quiet = 0;
    int32_t pvx;
    int minw = 1000, narrow = 0;
    static const int k_margin[4] = { 0, 8, 16, 24 };            /* safearea.h: Off, 8, 16, 24 px */
    for (m = 0; m < 4; m++) {                                    /* the drawn rectangle never leaves the safe area */
        int g = k_margin[m];
        uint32_t bad_m = 0, hitx = 0, hity = 0;
        om_saver_init(&s, 152, 101, g, g, 319 - g, 223 - g, 12345u + m);
        for (f = 1; f <= 100000u; f++) {
            om_saver_step(&s, &p);
            if (p.x0 < g || p.x1 > 319 - g || p.y0 < g || p.y1 > 223 - g || p.x1 < p.x0) bad_m++;
            if (p.y0 == g || p.y1 == 223 - g) hity = 1;
            if ((s.x >> 8) == g || (s.x >> 8) + 151 == 319 - g) hitx = 1;
        }
        CHECK_INT(bad_m, 0);
        CHECK(hitx && hity);                                     /* it reaches every edge, so the bound is tight */
    }
    om_saver_init(&s, 152, 101, 24, 24, 295, 199, 12345u);
    pvx = s.vx;
    CHECK(s.vx == OM_SAVER_VX || s.vx == -OM_SAVER_VX);
    CHECK(s.vy == OM_SAVER_VY || s.vy == -OM_SAVER_VY);
    for (f = 1; f <= 200000u; f++) {
        om_saver_step(&s, &p);
        if (p.x0 < 24 || p.x1 > 295 || p.y0 < 24 || p.y1 > 199 || p.x1 < p.x0) bad++;
        if (p.x1 - p.x0 + 1 < minw) minw = p.x1 - p.x0 + 1;
        if (!first_narrow && p.x1 - p.x0 + 1 < 152) first_narrow = f;
        if (p.x1 - p.x0 + 1 < 150) { if (!narrow) spins++; narrow = 1; } else narrow = 0;
        if (s.vx != pvx) { flips++; pvx = s.vx; }
    }
    CHECK_INT(bad, 0);
    CHECK(flips >= 2);
    CHECK(minw <= 4);                                       /* the spin turns it edge-on */
    CHECK(first_narrow > 0 && first_narrow <= 163);         /* the first bounce (<= 120 px at 0.75) starts a spin */
    CHECK(spins >= 200000u / (OM_SAVER_SPIN_EVERY + OM_SAVER_SPIN_LEN));   /* at least one per 8 s + a spin */
    /* with no bounce at all (an area exactly the logo's size: it stands still), it still spins every 8 s */
    om_saver_init(&s, 152, 101, 0, 0, 151, 100, 3u);
    CHECK_INT(s.vx, 0); CHECK_INT(s.vy, 0);
    for (f = 1, narrow = 0, spins = 0; f <= 4000u; f++) {
        om_saver_step(&s, &p);
        if (p.x1 - p.x0 + 1 < 150) { if (!narrow) spins++; narrow = 1; } else narrow = 0;
        if (p.y0 != 0) quiet++;
    }
    CHECK_INT(quiet, 0);
    CHECK_INT(spins, 4000u / OM_SAVER_SPIN_EVERY);          /* 8: one every 480 frames (8 s at 60 fps) */
    om_saver_init(&s, 152, 101, 0, 0, 319, 223, 99u);
    om_saver_init(&s2, 152, 101, 0, 0, 319, 223, 99u);
    for (f = 0; f < 1000u; f++) {
        om_saver_step(&s, &p); om_saver_step(&s2, &p2);
        if (p.x0 != p2.x0 || p.y0 != p2.y0 || p.x1 != p2.x1) same = 0;
    }
    CHECK(same);                                            /* deterministic */
    om_saver_init(&s, 300, 101, 24, 24, 295, 199, 7u);      /* wider than the area: held still, no spin past it */
    CHECK_INT(s.vx, 0);
    for (f = 0; f < 100u; f++) om_saver_step(&s, &p);
    CHECK_INT(s.x >> 8, 24);
    return th_done("test_saver");
}
