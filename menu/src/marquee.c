/* menu/src/marquee.c -- see marquee.h. BSD-3-Clause. */
#include "marquee.h"

int om_marquee_mode(int text_w, int box_w, int cap)
{
    if (text_w <= box_w) return OM_MQ_FITS;
    return text_w <= cap ? OM_MQ_SCROLL : OM_MQ_FADE;
}

int32_t om_marquee_period(int text_w, int box_w) { return text_w > box_w ? text_w + OM_MQ_GAP : 0; }

static uint32_t hold_frames(uint32_t fps_x100) { return (fps_x100 + 50u) / 100u; }          /* 1.0 s */

static uint32_t travel_frames(int32_t d, uint32_t fps_x100)
{
    return ((uint32_t)d * fps_x100 + OM_MQ_SPEED * 100u - 1u) / (OM_MQ_SPEED * 100u);
}

uint32_t om_marquee_cycle(int text_w, int box_w, uint32_t fps_x100)
{
    int32_t d = om_marquee_period(text_w, box_w);
    return d ? hold_frames(fps_x100) + travel_frames(d, fps_x100) : 0u;
}

int32_t om_marquee_off(uint32_t t, int text_w, int box_w, uint32_t fps_x100)
{
    int32_t d = om_marquee_period(text_w, box_w), p, s;
    uint32_t hold, tr;
    if (!d)
        return 0;
    hold = hold_frames(fps_x100);
    tr = travel_frames(d, fps_x100);
    t %= hold + tr;
    if (t < hold)
        return 0;
    p = (int32_t)(((t - hold) << 12) / tr);                       /* Q12 progress through the travel */
    s = (int32_t)(((int64_t)p * p * (3 * 4096 - 2 * p)) >> 24);  /* smoothstep 3p^2 - 2p^3, Q12 */
    return (int32_t)(((int64_t)d * s) >> 12) % d;
}
