/* menu/host-tests/test_frcgap.c -- the SD write gap's time bound (frcgap.h).
 * BSD-3-Clause. */
#include "th.h"
#include "frcgap.h"

#define US10K_DIV8   (10000u * 8u * 1000u / 26874u)      /* om_ut_frc_us(10000) at clock/8: uitext.c frc_us, review M3 */
#define US10K_DIV128 (10000u * 128u * 1000u / 26874u)    /* ... at clock/128 */

/* polls until the gap ends with the FRC advancing `step` ticks per poll (wrapping), capped at `cap` */
static uint32_t run(uint32_t us10k, uint16_t step, uint32_t cap)
{
    om_frcgap g;
    uint16_t frc = 0xFFF0u;                /* wraps early */
    uint32_t n;
    om_frcgap_start(&g, frc, us10k);
    for (n = 1; n <= cap; n++) {
        frc = (uint16_t)(frc + step);
        if (om_frcgap_step(&g, frc)) return n;
    }
    return 0;
}

int main(void)
{
    uint32_t n;
    CHECK_INT(US10K_DIV8, 2976); CHECK_INT(US10K_DIV128, 47629);   /* the figures frcgap.h quotes */
    /* N2: 3 ticks per poll at clock/8 is 0.89 us; 40 ms takes 134,409 ticks = 44,803 polls, not "never" */
    n = run(US10K_DIV8, 3, 1000000u);
    CHECK(n >= 44800u && n <= 44810u);
    /* 10 ticks per poll (the measured ~3 us loop): 13,441 polls, not ~20,000 (the floored ~60 ms) */
    n = run(US10K_DIV8, 10, 1000000u);
    CHECK(n >= 13440u && n <= 13442u);
    /* clock/128: 1 tick = 4.76 us; 40 ms = 8,399 ticks */
    n = run(US10K_DIV128, 1, 1000000u);
    CHECK(n >= 8398u && n <= 8400u);
    /* N1: a frozen FRC ends at the poll cap */
    CHECK_INT(run(US10K_DIV8, 0, 1000000u), OM_FRCGAP_POLLS);
    /* a step over 2 ms (the FRC stepping back = a near-full wrap) is dropped, the gap goes on */
    {
        om_frcgap g;
        om_frcgap_start(&g, 1000, US10K_DIV8);
        CHECK_INT(om_frcgap_step(&g, 999), 0);           /* 65,535 ticks = 19.5 ms: dropped */
        CHECK_INT(g.ticks, 0);
        CHECK_INT(om_frcgap_step(&g, 999 + 6721), 0);    /* 6,721 ticks = 2.0002 ms: dropped */
        CHECK_INT(g.ticks, 0);
        CHECK_INT(om_frcgap_step(&g, 999 + 6721 + 6720), 0);   /* 6,720 ticks = 1.9999 ms: kept */
        CHECK_INT(g.ticks, 6720);
    }
    return th_done("test_frcgap");
}
