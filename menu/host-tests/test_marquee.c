/* menu/host-tests/test_marquee.c -- the marquee's timing and offset maths (controller addendum 3): no motion when the
 * text fits; 1.0 s hold; ~30 px/s average with eased ends; a continuous wrap at text width + 24 px. BSD-3-Clause. */
#include "th.h"
#include "marquee.h"

int main(void)
{
    const uint32_t fps = 5994;
    const int tw = 200, bw = 132;                      /* a 200 px title in the 133 px row-title box */
    int32_t d = om_marquee_period(tw, bw), prev, off, maxstep = 0, step;
    uint32_t cyc = om_marquee_cycle(tw, bw, fps), t, hold = 60, travel;

    CHECK_INT(om_marquee_period(100, 132), 0);         /* fits: no marquee */
    CHECK_INT(om_marquee_period(132, 132), 0);
    CHECK_INT(om_marquee_cycle(100, 132, fps), 0);
    CHECK_INT(om_marquee_off(500, 100, 132, fps), 0);
    CHECK_INT(d, 224);                                 /* 200 + the 24 px gap */
    travel = cyc - hold;
    CHECK_INT(travel, (224u * 5994u + 2999u) / 3000u); /* 30 px/s average: 448 frames, ~7.5 s */
    for (t = 0; t < hold; t++)
        CHECK_INT(om_marquee_off(t, tw, bw, fps), 0);  /* 1.0 s hold at the start */
    prev = 0;
    for (t = hold; t < cyc; t++) {
        off = om_marquee_off(t, tw, bw, fps);
        CHECK(off >= prev);                            /* monotonic through the travel */
        CHECK(off >= 0 && off < d);
        step = off - prev;
        if (step > maxstep) maxstep = step;
        prev = off;
    }
    CHECK(prev > d - 3);                               /* arrives (eased) at the next copy's start */
    CHECK(maxstep >= 1 && maxstep <= 2);               /* peak 1.5 x 0.5 px/frame: never a jump */
    CHECK_INT(om_marquee_off(hold + 1, tw, bw, fps) <= 1, 1);   /* eased start: creeps off the hold */
    CHECK_INT(om_marquee_off(cyc, tw, bw, fps), 0);    /* wraps: the cycle restarts with its hold */
    CHECK_INT(om_marquee_off(cyc + hold / 2, tw, bw, fps), 0);
    CHECK_INT(om_marquee_off(cyc + hold + travel / 2, tw, bw, fps), om_marquee_off(hold + travel / 2, tw, bw, fps));
    /* average speed over the middle half is between the average (30) and the smoothstep peak (45) px/s */
    {
        int32_t a = om_marquee_off(hold + travel / 4, tw, bw, fps), b = om_marquee_off(hold + 3 * travel / 4, tw, bw, fps);
        int32_t pxps = (b - a) * 5994 / (int32_t)(travel / 2) / 100;
        CHECK(pxps >= 30 && pxps <= 45);
    }
    CHECK_INT(om_marquee_cycle(tw, bw, 5000), 50u + (224u * 5000u + 2999u) / 3000u);   /* PAL: 50 Hz */
    return th_done("test_marquee");
}
