/* menu/launcher/smpcwait.c -- see smpcwait.h. Pure C (host-tested by menu/host-tests/test_smpcwait.c and
 * test_padfall.c). BSD-3-Clause. */
#include <string.h>
#include "smpcwait.h"
#include "smpcpad.h"

int om_intback_wait2(uint16_t (*tvstat)(void *), void *ctx, uint32_t fb_polls, int *via)
{
    uint32_t t;
    int edges = 0;
    uint16_t s, prev;
    *via = 0;
    for (t = 0; tvstat(ctx) & OM_TV_VBLANK; t++)          /* already in V-BLANK: its start is unknown, wait it out */
        if (t >= OM_SMPCWAIT_POLLS) return OM_SMPC_E_VBLANK;
    for (t = 0; !(tvstat(ctx) & OM_TV_VBLANK); t++)       /* the V-BLANK-IN edge */
        if (t >= OM_SMPCWAIT_POLLS) return OM_SMPC_E_VBLANK;
    prev = (uint16_t)(tvstat(ctx) & OM_TV_HBLANK);
    for (t = 1; ; t++) {
        if (t >= OM_SMPCWAIT_POLLS) return OM_SMPC_E_DELAY;
        s = tvstat(ctx);
        if (!(s & OM_TV_VBLANK)) return OM_SMPC_E_WINDOW;
        if ((s & OM_TV_HBLANK) && !prev && ++edges >= OM_INTBACK_HBLANKS) { *via = OM_VIA_PRIMARY; return 0; }
        prev = (uint16_t)(s & OM_TV_HBLANK);
        if (fb_polls && t >= fb_polls) { *via = OM_VIA_FALLBACK; return 0; }
    }
}

int om_intback_wait(uint16_t (*tvstat)(void *), void *ctx)
{
    int via;
    return om_intback_wait2(tvstat, ctx, 0, &via);
}

uint32_t om_poll_calibrate(uint16_t (*tvstat)(void *), void *ctx)
{
    uint32_t t, polls = 0;
    int edges = 0;
    uint16_t s, prev;
    for (t = 0; !(tvstat(ctx) & OM_TV_VBLANK); t++)       /* into V-BLANK ... */
        if (t >= OM_SMPCWAIT_POLLS) return 0;
    for (t = 0; tvstat(ctx) & OM_TV_VBLANK; t++)          /* ... and out: the start of active display */
        if (t >= OM_SMPCWAIT_POLLS) return 0;
    prev = (uint16_t)(tvstat(ctx) & OM_TV_HBLANK);
    for (t = 0; ; t++) {                                  /* the first HBLANK-IN edge */
        if (t >= OM_SMPCWAIT_POLLS) return 0;
        s = tvstat(ctx);
        if (s & OM_TV_VBLANK) return 0;
        if ((s & OM_TV_HBLANK) && !prev) break;
        prev = (uint16_t)(s & OM_TV_HBLANK);
    }
    prev = OM_TV_HBLANK;
    for (t = 0; edges < OM_CAL_LINES; t++) {              /* OM_CAL_LINES whole lines, one tvstat() per poll */
        if (t >= OM_SMPCWAIT_POLLS) return 0;
        s = tvstat(ctx);
        polls++;
        if (s & OM_TV_VBLANK) return 0;
        if ((s & OM_TV_HBLANK) && !prev) edges++;
        prev = (uint16_t)(s & OM_TV_HBLANK);
    }
    return (polls + OM_CAL_LINES / 2) / OM_CAL_LINES;
}

void om_padtime_init(om_padtime *pt, uint32_t ppl)
{
    pt->ppl = ppl;
    pt->fails = 0;
    pt->fallback = 0;
    pt->last_rc = 0;
}

uint32_t om_pad_fallback_polls(const om_padtime *pt)
{
    return pt->ppl ? OM_FALLBACK_LINES * pt->ppl : OM_FALLBACK_POLLS_FIXED;
}

uint16_t om_pad_read(om_padtime *pt, const om_padio *io)
{
    int via, rc;
    uint16_t pressed = 0;
    rc = om_intback_wait2(io->tvstat, io->ctx, pt->fallback ? om_pad_fallback_polls(pt) : 0, &via);
    if (rc == 0 && via == OM_VIA_PRIMARY) {
        pt->fails = 0;                                    /* primary works (again): fallback off */
        pt->fallback = 0;
    } else if (rc != 0) {
        pt->last_rc = rc;
        if (++pt->fails >= OM_PAD_FALLBACK_AFTER)
            pt->fallback = 1;
        return 0;
    }
    if (io->intback(io->ctx, &pressed) != 0)
        return 0;
    return pressed;
}

/* smpcpad.h om_smpc_rtc: SMPC_Manual.txt:1536-1580 -- OREG0 bit 7 STE, OREG1/2 year BCD, OREG3 low nibble
 * month (hex), OREG4 day, OREG5 hours, OREG6 minutes, OREG7 seconds (BCD) */
static int bcd(uint8_t v, char *o, uint32_t lo, uint32_t hi)
{
    uint32_t a = v >> 4, b = v & 15u, n = a * 10u + b;
    if (a > 9 || b > 9 || n < lo || n > hi) return -1;
    o[0] = (char)('0' + a);
    o[1] = (char)('0' + b);
    return 0;
}

int om_rtc_fmt(const uint8_t r[8], char out[16])
{
    char t[16];
    uint32_t m = r[3] & 15u;
    out[0] = 0;
    if (!(r[0] & 0x80u) || m < 1 || m > 12) return OM_SMPC_RTC_UNSET;
    if (bcd(r[1], t, 0, 99) || bcd(r[2], t + 2, 0, 99) || bcd(r[4], t + 6, 1, 31) || bcd(r[5], t + 9, 0, 23) ||
        bcd(r[6], t + 11, 0, 59) || bcd(r[7], t + 13, 0, 59))
        return OM_SMPC_RTC_UNSET;
    t[4] = (char)('0' + m / 10u);
    t[5] = (char)('0' + m % 10u);
    t[8] = '-';
    t[15] = 0;
    memcpy(out, t, 16);
    return 0;
}
