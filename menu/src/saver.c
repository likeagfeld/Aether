/* menu/src/saver.c -- see saver.h. BSD-3-Clause. */
#include "saver.h"

static const uint8_t k_cos[33] = {   /* 256 x cos(i x pi / 64), i = 0..32, rounded (255 stands for 256 at i = 0, 1) */
    255, 255, 255, 253, 251, 248, 245, 241, 237, 231, 226, 220, 213, 206, 198, 190, 181,
    172, 162, 153, 142, 132, 121, 109, 98, 86, 74, 62, 50, 38, 25, 13, 0 };

void om_saver_init(om_saver *s, int w, int h, int x0, int y0, int x1, int y1, uint32_t seed)
{
    int rw = x1 - x0 + 1 - w, rh = y1 - y0 + 1 - h;
    s->w = (int16_t)w; s->h = (int16_t)h;
    s->x0 = (int16_t)x0; s->y0 = (int16_t)y0; s->x1 = (int16_t)x1; s->y1 = (int16_t)y1;
    s->x = (int32_t)(x0 + (rw > 0 ? (int)(seed % (uint32_t)(rw + 1)) : 0)) * 256;
    s->y = (int32_t)(y0 + (rh > 0 ? (int)((seed >> 8) % (uint32_t)(rh + 1)) : 0)) * 256;
    s->vx = rw > 0 ? ((seed & 1u) ? OM_SAVER_VX : -OM_SAVER_VX) : 0;
    s->vy = rh > 0 ? ((seed & 2u) ? OM_SAVER_VY : -OM_SAVER_VY) : 0;
    s->t = 0;
    s->spin = 0;
}

void om_saver_step(om_saver *s, om_saver_pose *p)
{
    int32_t xmin = (int32_t)s->x0 * 256, ymin = (int32_t)s->y0 * 256;
    int32_t xmax = (int32_t)(s->x1 - s->w + 1) * 256, ymax = (int32_t)(s->y1 - s->h + 1) * 256;
    int hit = 0, dw = s->w, cx;
    if (xmax > xmin) {
        s->x += s->vx;
        if (s->x < xmin) { s->x = 2 * xmin - s->x; s->vx = -s->vx; hit = 1; }
        if (s->x > xmax) { s->x = 2 * xmax - s->x; s->vx = -s->vx; hit = 1; }
    } else s->x = xmin;
    if (ymax > ymin) {
        s->y += s->vy;
        if (s->y < ymin) { s->y = 2 * ymin - s->y; s->vy = -s->vy; hit = 1; }
        if (s->y > ymax) { s->y = 2 * ymax - s->y; s->vy = -s->vy; hit = 1; }
    } else s->y = ymin;
    s->t++;
    if (!s->spin && (hit || s->t >= OM_SAVER_SPIN_EVERY)) { s->spin = 1; s->t = 0; }
    if (s->spin) {
        uint32_t k = s->spin - 1u;                             /* 0 .. OM_SAVER_SPIN_LEN - 1 */
        dw = (s->w * k_cos[k <= 32u ? k : 64u - k] + 128) >> 8;
        if (dw < 2) dw = 2;
        if (++s->spin > OM_SAVER_SPIN_LEN) s->spin = 0;
    }
    cx = (int)(s->x >> 8);
    p->x0 = (int16_t)(cx + (s->w - dw) / 2);
    p->x1 = (int16_t)(p->x0 + dw - 1);
    p->y0 = (int16_t)(s->y >> 8);
    p->y1 = (int16_t)(p->y0 + s->h - 1);
}
