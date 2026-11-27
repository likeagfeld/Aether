/* menu/host-tests/test_padfall.c -- Ruling on the re-review's last Important item: if TVSTAT's HBLANK
 * bit does not toggle during V-BLANK, the primary INTBACK window (7 HBLANK-IN edges after V-BLANK-IN) fails every
 * field and the pad would read 0 for ever. After OM_PAD_FALLBACK_AFTER failing fields in a row, om_pad_read also
 * accepts a fixed delay measured in polls (calibrated as polls per line in active display, 8 lines), still refusing
 * once V-BLANK-OUT arrives, and still trying the primary timing every field.
 *
 * Simulation: one poll = 1 us; NTSC-like 63 us lines (HBLANK the last 11 us), 263 lines per field, 224 displayed
 * (TechBulletins.txt:1095). The fake SMPC accepts INTBACK only inside the documented window (>= 300 us after
 * V-BLANK-IN, still in V-BLANK; SMPC_Manual.txt:885-905) and then reports button A pressed. */
#include "th.h"
#include "smpcwait.h"

typedef struct {
    uint32_t t, line_us, hb_us, lines, active;
    int hb_dead_vblank;   /* HBLANK bit stuck at 0 during V-BLANK lines only */
    int hb_dead;          /* HBLANK bit stuck at 0 everywhere */
    uint32_t last_vb_in;
    int prev_vb, in_vb;
    int early;            /* INTBACK attempted outside the window */
    int issued;
} sim;

static uint16_t tv(void *ctx)
{
    sim *s = (sim *)ctx;
    uint32_t t = s->t++, field = s->line_us * s->lines, in = t % field, line = in / s->line_us;
    int vb = line >= s->active;
    int hb = (in % s->line_us) >= s->line_us - s->hb_us;
    if (s->hb_dead || (vb && s->hb_dead_vblank)) hb = 0;
    if (vb && !s->prev_vb) s->last_vb_in = t;
    s->prev_vb = vb;
    s->in_vb = vb;
    return (uint16_t)((vb ? OM_TV_VBLANK : 0) | (hb ? OM_TV_HBLANK : 0));
}

static int fake_intback(void *ctx, uint16_t *pressed)
{
    sim *s = (sim *)ctx;
    if (!s->in_vb || s->t - s->last_vb_in < 300) { s->early++; return -1; }
    s->issued++;
    *pressed = (uint16_t)(1u << 10);                              /* A (SGL PER_DGT_TA) */
    return 0;
}

static sim mk(void)
{
    sim s;
    memset(&s, 0, sizeof s);
    s.line_us = 63; s.hb_us = 11; s.lines = 263; s.active = 224; s.t = 5000;
    return s;
}

int main(void)
{
    sim s;
    om_padtime pt;
    om_padio io;
    int f, first_a = -1;
    uint16_t p;

    /* calibration in active display: 63 polls per line */
    s = mk();
    CHECK_INT(om_poll_calibrate(tv, &s), 63);
    s = mk(); s.hb_dead = 1;
    CHECK_INT(om_poll_calibrate(tv, &s), 0);                      /* no HBLANK at all: calibration fails */

    /* 1. live HBLANK: the fallback never engages, A is read every field */
    s = mk();
    om_padtime_init(&pt, om_poll_calibrate(tv, &s));
    io.tvstat = tv; io.intback = fake_intback; io.ctx = &s;
    for (f = 0; f < 40; f++) {
        p = om_pad_read(&pt, &io);
        CHECK(p == (1u << 10));
        CHECK_INT(pt.fallback, 0);
    }
    CHECK_INT(s.early, 0);

    /* 2. HBLANK dead during V-BLANK: 8 failing fields, then the fallback engages and A is detected */
    s = mk(); s.hb_dead_vblank = 1;
    om_padtime_init(&pt, om_poll_calibrate(tv, &s));
    CHECK_INT(pt.ppl, 63);
    io.ctx = &s;
    for (f = 0; f < 20; f++) {
        p = om_pad_read(&pt, &io);
        if (p && first_a < 0) first_a = f;
        if (f < OM_PAD_FALLBACK_AFTER - 1) { CHECK_INT(p, 0); CHECK_INT(pt.fallback, 0); }
    }
    CHECK_INT(first_a, OM_PAD_FALLBACK_AFTER);                    /* the field after the 8th failure */
    CHECK_INT(pt.fallback, 1);
    CHECK_INT(pt.last_rc, OM_SMPC_E_WINDOW);
    CHECK_INT(s.early, 0);                                        /* never outside the documented window */
    CHECK(s.issued > 0);

    /* 3. HBLANK comes back: the primary timing wins again and the fallback switches off */
    s.hb_dead_vblank = 0;
    p = om_pad_read(&pt, &io);
    CHECK(p == (1u << 10));
    CHECK_INT(pt.fallback, 0);
    CHECK_INT(pt.fails, 0);

    /* 4. HBLANK dead everywhere: calibration fails, the fixed poll count is used; it may be too long for V-BLANK
     *    at 1 poll/us, but it must never issue early */
    s = mk(); s.hb_dead = 1;
    om_padtime_init(&pt, om_poll_calibrate(tv, &s));
    CHECK_INT(pt.ppl, 0);
    io.ctx = &s;
    for (f = 0; f < 20; f++)
        (void)om_pad_read(&pt, &io);
    CHECK_INT(pt.fallback, 1);
    CHECK_INT(s.early, 0);
    CHECK(om_pad_fallback_polls(&pt) == OM_FALLBACK_POLLS_FIXED);
    CHECK(om_pad_fallback_polls(&(om_padtime){ .ppl = 63 }) == 8u * 63u);
    return th_done("test_padfall");
}
