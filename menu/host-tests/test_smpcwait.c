/* menu/host-tests/test_smpcwait.c -- INTBACK issue timing: per SMPC_Manual.txt:885-905
 * and Figure 2.5, INTBACK may only be issued after 300 us following V-BLANK-IN and before V-BLANK-OUT. The launcher
 * waits for the V-BLANK-IN edge, then counts HBLANK-IN edges (OM_INTBACK_HBLANKS), staying inside V-BLANK. This
 * simulates TVSTAT on a 1 us-per-poll clock with an NTSC-like line (63 us, HBLANK 11 us) and 263 lines per field
 * of which 224 are displayed (TechBulletins.txt:1095), and checks the wait in microseconds. */
#include "th.h"
#include "smpcwait.h"

typedef struct {
    uint32_t t;           /* microseconds since the simulation began (one per poll) */
    uint32_t line_us, hb_us, lines, active, vb_in;   /* layout */
    int hb_dead;          /* HBLANK bit never set */
    int vb_dead;          /* VBLANK bit never set */
    uint32_t vb_lines;    /* V-BLANK length in lines (0 = lines - active) */
    uint32_t last_vb_in;  /* time of the last V-BLANK-IN edge seen */
    int prev_vb;
} sim;

static uint16_t tv(void *ctx)
{
    sim *s = (sim *)ctx;
    uint32_t t = s->t++, field = s->line_us * s->lines, in = t % field, line = in / s->line_us;
    uint32_t vbl = s->vb_lines ? s->vb_lines : s->lines - s->active;
    int vb = !s->vb_dead && line >= s->active && line < s->active + vbl;
    int hb = !s->hb_dead && (in % s->line_us) >= s->line_us - s->hb_us;
    if (vb && !s->prev_vb) s->last_vb_in = t;
    s->prev_vb = vb;
    return (uint16_t)((vb ? 0x0008u : 0) | (hb ? 0x0004u : 0));      /* TVSTAT bit3 VBLANK, bit2 HBLANK (HWF-10) */
}

static sim ntsc(uint32_t start)
{
    sim s;
    memset(&s, 0, sizeof s);
    s.line_us = 63; s.hb_us = 11; s.lines = 263; s.active = 224; s.t = start;
    return s;
}

int main(void)
{
    sim s;
    uint32_t start;
    int rc;
    /* 1. from many start phases: returns 0, at least 300 us after the V-BLANK-IN edge, still inside V-BLANK */
    for (start = 0; start < 63u * 263u; start += 997u) {
        s = ntsc(start);
        rc = om_intback_wait(tv, &s);
        CHECK_INT(rc, 0);
        CHECK(s.t - 1 - s.last_vb_in >= 300);                    /* last poll is the one that saw the 7th edge */
        CHECK((tv(&s) & 0x0008u) != 0);                           /* the next poll is still V-BLANK */
        CHECK(s.t - s.last_vb_in < 63u * 39u);                    /* well before V-BLANK-OUT */
    }
    /* 2. called in the middle of V-BLANK: waits for the NEXT V-BLANK-IN, never issues on a stale one */
    s = ntsc(63u * 230u);
    CHECK_INT(om_intback_wait(tv, &s), 0);
    CHECK(s.last_vb_in >= 63u * 263u);
    CHECK(s.t - 1 - s.last_vb_in >= 300);
    /* 3. no HBLANK edges at all: never issued early -- V-BLANK-OUT ends this field's window, and with a V-BLANK
     *    longer than the poll cap the delay gives up at its own cap */
    s = ntsc(0); s.hb_dead = 1;
    CHECK_INT(om_intback_wait(tv, &s), OM_SMPC_E_WINDOW);
    s = ntsc(0); s.hb_dead = 1; s.line_us = 10000; s.active = 1; s.vb_lines = 262;
    CHECK_INT(om_intback_wait(tv, &s), OM_SMPC_E_DELAY);
    /* 4. V-BLANK never comes: the edge wait gives up */
    s = ntsc(0); s.vb_dead = 1;
    CHECK_INT(om_intback_wait(tv, &s), OM_SMPC_E_VBLANK);
    /* 5. V-BLANK shorter than the delay (3 lines): V-BLANK-OUT arrives first, so no INTBACK this field */
    s = ntsc(0); s.vb_lines = 3;
    CHECK_INT(om_intback_wait(tv, &s), OM_SMPC_E_WINDOW);
    /* 6. the line count covers 300 us at the shortest documented line (see smpcwait.h) */
    CHECK((OM_INTBACK_HBLANKS - 1) * OM_LINE_NS_MIN >= 300000u);
    return th_done("test_smpcwait");
}
