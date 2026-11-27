/* menu/src/safearea.c -- the overscan (safe-area) levels (from M3.5 Task 4) and the transform.
 * BSD-3-Clause. */
#include "safearea.h"
static const uint8_t k_m[OM_OVS_COUNT] = { 0, 8, 16, 24 };
static const char *const k_key[OM_OVS_COUNT] = { "off", "8", "16", "24" };
static const char *const k_label[OM_OVS_COUNT] = { "Off", "8 px", "16 px", "24 px" };
int om_safe_margin(uint8_t level) { return level < OM_OVS_COUNT ? k_m[level] : 8; }
const char *om_safe_key(uint8_t level) { return k_key[level < OM_OVS_COUNT ? level : OM_OVS_STD]; }
const char *om_safe_label(uint8_t level) { return k_label[level < OM_OVS_COUNT ? level : OM_OVS_STD]; }

om_safe g_om_safe = { OM_OVS_STD, 1, OM_SAFE_DESIGN };

static int fdiv(int a, int b) { int q = a / b; return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q; }

static int map(int e, int m, int span_t, int span_d)
{   /* round half up: m + floor((2 (e - 8) span_t + span_d) / (2 span_d)) */
    return m + fdiv(2 * (e - OM_SAFE_DESIGN) * span_t + span_d, 2 * span_d);
}

static int clamp(int v, int lim) { return v < 0 ? 0 : v > lim ? lim : v; }

void om_safe_set(om_safe *t, uint8_t level)
{
    t->level = level < OM_OVS_COUNT ? level : OM_OVS_STD;
    t->m = (int16_t)om_safe_margin(t->level);
    t->ident = (uint8_t)(t->m == OM_SAFE_DESIGN);
}

int om_safe_xu(const om_safe *t, int e)
{
    return t->ident ? e : map(e, t->m, 320 - 2 * t->m, 320 - 2 * OM_SAFE_DESIGN);
}

int om_safe_yu(const om_safe *t, int e)
{
    return t->ident ? e : map(e, t->m, 224 - 2 * t->m, 224 - 2 * OM_SAFE_DESIGN);
}

int om_safe_x(const om_safe *t, int e) { return clamp(om_safe_xu(t, e), 320); }
int om_safe_y(const om_safe *t, int e) { return clamp(om_safe_yu(t, e), 224); }

static void rect(const om_safe *t, int x0, int y0, int x1, int y1, int out[4], int (*fx)(const om_safe *, int),
                 int (*fy)(const om_safe *, int))
{
    out[0] = fx(t, x0);
    out[1] = fy(t, y0);
    out[2] = fx(t, x1 + 1) - 1;
    out[3] = fy(t, y1 + 1) - 1;
    if (out[2] < out[0]) out[2] = out[0];
    if (out[3] < out[1]) out[3] = out[1];
}

void om_safe_rect(const om_safe *t, int x0, int y0, int x1, int y1, int out[4])
{
    rect(t, x0, y0, x1, y1, out, om_safe_x, om_safe_y);
}

void om_safe_rect_u(const om_safe *t, int x0, int y0, int x1, int y1, int out[4])
{
    rect(t, x0, y0, x1, y1, out, om_safe_xu, om_safe_yu);
}
