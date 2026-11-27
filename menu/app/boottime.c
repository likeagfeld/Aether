/* menu/app/boottime.c -- see boottime.h. BSD-3-Clause. */
#include "boottime.h"
#include "text.h"
#include "uitext.h"
#include "vdp2theme.h"

static om_boot_timing s_t;
static uint16_t s_frc[OM_BT_N];
static om_boot_hook s_hook;
static int s_diag;

const om_boot_timing *om_boot_timing_get(void) { return &s_t; }

void om_boot_begin(om_boot_hook hook)
{
    uint32_t i;
    for (i = 0; i < OM_BT_N; i++) s_t.vbl[i] = s_t.us[i] = 0;
    s_t.done = s_t.total_vbl = s_t.ui_shown = 0;
    s_t.start_vbl = om_v2_vblanks();
    s_hook = hook;
}

void om_boot_step_begin(int step)
{
    if (step < 0 || step >= OM_BT_N) return;
    if (s_hook) s_hook(step, 0);
    s_t.vbl[step] = om_v2_vblanks();             /* the start, until the end turns it into a duration */
    s_frc[step] = om_ut_frc();
}

void om_boot_step_end(int step)
{
    uint32_t us, vb, wrap, floor_us;
    if (step < 0 || step >= OM_BT_N || (s_t.done & (1u << step))) return;
    /* FRT microseconds plus whole FRT wraps until at least the V-blanks seen minus one frame (folders.c ph_since) */
    us = om_ut_frc_us((uint16_t)(om_ut_frc() - s_frc[step]));
    vb = om_v2_vblanks() - s_t.vbl[step];
    wrap = om_ut_frc_us(0xFFFFu) + 1u;
    floor_us = vb > 1u ? (vb - 1u) * (om_tv_is_pal() ? 20000u : 16683u) : 0u;   /* the TV's frame */
    while (us < floor_us) us += wrap;
    s_t.vbl[step] = vb;
    s_t.us[step] = us;
    s_t.done |= 1u << step;
    if (step == OM_BT_RASTER) s_t.total_vbl = om_v2_vblanks() - s_t.start_vbl;
    if (s_hook) s_hook(step, 1);
}

uint32_t om_boot_done_count(void)
{
    uint32_t n = 0, d = s_t.done;
    while (d) { n += d & 1u; d >>= 1; }
    return n;
}

void om_boot_set_shown(void) { s_t.ui_shown = 1; }

void om_boot_diag_set(int on) { s_diag = on; }
#ifdef OMSAT_FAKECARD
int om_boot_diag(void) { return s_diag; }
#endif
