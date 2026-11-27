/* menu/src/anim.c -- see anim.h. Values passed to a tween stay within +-2^19 so Q12 products fit in 32 bits.
 * BSD-3-Clause. */
#include <string.h>
#include "anim.h"

int32_t om_ease_out(int32_t p)
{
    int32_t q, q2, q3;
    if (p <= 0) return 0;
    if (p >= OM_Q) return OM_Q;
    q = OM_Q - p;
    q2 = (q * q) >> 12;
    q3 = (q2 * q) >> 12;
    return OM_Q - q3;                                  /* 1 - (1 - p)^3 */
}

void om_tween_jump(om_tween *tw, int32_t v)
{
    tw->from = tw->to = v;
    tw->t = tw->dur = 0;
}

int32_t om_tween_value(const om_tween *tw)
{
    int32_t p;
    if (tw->t >= tw->dur)
        return tw->to;
    p = om_ease_out((int32_t)(((uint32_t)tw->t << 12) / tw->dur));
    return tw->from + (((tw->to - tw->from) * p) >> 12);
}

void om_tween_to(om_tween *tw, int32_t to, uint16_t dur)
{
    if (to == tw->to)
        return;                                        /* already there or heading there: keep the glide */
    tw->from = om_tween_value(tw);
    tw->to = to;
    tw->t = 0;
    tw->dur = dur;
    if (!dur)
        tw->from = to;
}

void om_tween_run(om_tween *tw, int32_t from, int32_t to, uint16_t dur)
{
    tw->from = dur ? from : to;
    tw->to = to;
    tw->t = 0;
    tw->dur = dur;
}

void om_tween_step(om_tween *tw)
{
    if (tw->t < tw->dur)
        tw->t++;
}

int om_tween_active(const om_tween *tw) { return tw->t < tw->dur; }

uint16_t om_anim_frames(uint16_t ms, uint8_t mode, int held, uint32_t fps_x100)
{
    uint32_t f;
    if (mode == OM_ANIM_OFF)
        return 0;
    f = ((uint32_t)ms * fps_x100 + 50000u) / 100000u;
    if (held)
        f /= 2u;
    return (uint16_t)(f < 2u ? 2u : f);
}

int32_t om_anim_dist(int32_t px, uint8_t mode)
{
    if (mode == OM_ANIM_OFF) return 0;
    if (mode == OM_ANIM_REDUCED) return px / 2;
    return px;
}

static uint32_t mix5(int32_t ca, int32_t cb, int32_t t)
{
    return (uint32_t)(ca + (((cb - ca) * t + 128) >> 8));
}

uint16_t om_c555_mix(uint16_t a, uint16_t b, uint32_t t256)
{   /* constant shifts only: SH-2 has no barrel shifter, a variable shift is a libgcc call */
    int32_t t = t256 > 256u ? 256 : (int32_t)t256;
    return (uint16_t)((a & 0x8000u) | mix5(a & 31, b & 31, t) | (mix5((a >> 5) & 31, (b >> 5) & 31, t) << 5) |
                      (mix5((a >> 10) & 31, (b >> 10) & 31, t) << 10));
}

uint16_t om_c555_cc(uint16_t top, uint16_t second, uint8_t r)
{
    uint32_t sh, out = 0;
    if (r > 31u) r = 31u;
    for (sh = 0; sh < 15; sh += 5) {
        uint32_t ct = (top >> sh) & 31u, cs = (second >> sh) & 31u;
        out |= ((ct * (31u - r) + cs * (r + 1u)) >> 5) << sh;
    }
    return (uint16_t)out;
}

uint16_t om_c555_rgb(uint32_t rgb)
{
    return (uint16_t)(0x8000u | (((rgb >> 3) & 0x1Fu) << 10) | (((rgb >> 11) & 0x1Fu) << 5) | ((rgb >> 19) & 0x1Fu));
}

int om_msg_is_error(const char *msg) { return strstr(msg, " rc ") != NULL; }

void om_toast_init(om_toast *t, uint8_t mode, uint16_t dur_frames)
{
    memset(t, 0, sizeof *t);
    t->mode = mode;
    t->dur = mode == OM_ANIM_OFF ? 0 : dur_frames;
    om_tween_jump(&t->slide, 0);
}

static void hide(om_toast *t)
{
    t->visible = t->sticky = 0;
    om_tween_to(&t->slide, 0, t->dur);
}

void om_toast_show(om_toast *t, const char *msg)
{
    if (!msg[0]) {
        hide(t);
        return;
    }
    strncpy(t->msg, msg, OM_TOAST_MSG - 1);
    t->msg[OM_TOAST_MSG - 1] = 0;
    t->sticky = (uint8_t)om_msg_is_error(t->msg);
    t->visible = 1;
    t->age = 0;
    om_tween_to(&t->slide, OM_Q, t->dur);
}

void om_toast_input(om_toast *t)
{
    if (t->visible && t->sticky)
        hide(t);
}

void om_toast_tick(om_toast *t)
{
    om_tween_step(&t->slide);
    if (t->visible && !t->sticky && ++t->age >= OM_TOAST_HOLD)
        hide(t);
}

int32_t om_toast_progress(const om_toast *t) { return om_tween_value(&t->slide); }

int om_toast_offer(om_toast *t, const char *msg, uint32_t serial, int automatic)
{
    if (serial == t->serial)
        return 0;
    if (automatic && t->visible && t->sticky)
        return 0;                                      /* wait: a visible rc error is replaced only by input */
    t->serial = serial;
    om_toast_show(t, msg);
    return 1;
}
