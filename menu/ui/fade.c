/* menu/ui/fade.c -- see fade.h. BSD-3-Clause. */
#include "sgl.h"
#include "anim.h"
#include "fade.h"
#include "text.h"

static void set(int v) { slColOffsetA((Sint16)v, (Sint16)v, (Sint16)v); }
static int16_t s_veil[3];                             /* om_fade_veil's offset now on (R, G, B; 0: none) */

void om_fade_veil(uint16_t mask, int r, int g, int b)
{
    if (r == s_veil[0] && g == s_veil[1] && b == s_veil[2]) return;
    s_veil[0] = (int16_t)r; s_veil[1] = (int16_t)g; s_veil[2] = (int16_t)b;
    slColOffsetA((Sint16)r, (Sint16)g, (Sint16)b);
    if (r | g | b) slColOffsetOn(mask); else slColOffsetOff(mask);
}

void om_fade_clear(uint16_t mask)
{
    s_veil[0] = s_veil[1] = s_veil[2] = 0;
    set(0);
    slColOffsetOff(mask);
}

void om_fade_black(uint16_t mask)
{
    set(-255);
    slColOffsetOn(mask);
}

void om_fade(uint16_t mask, int from, int to, uint16_t ms, uint8_t anim, om_fade_each each, void *ctx)
{
    uint32_t n = om_anim_frames(ms, anim, 0, om_tv_is_pal() ? 5000u : 5994u), i;
    set(from);
    slColOffsetOn(mask);
    for (i = 1; i <= n; i++) {                                   /* one division a frame: not a hot loop */
        set(from + (to - from) * (int)i / (int)n);
        if (each) each(ctx);
        slSynch();
    }
    set(to);
    if (to == 0) slColOffsetOff(mask);
    s_veil[0] = s_veil[1] = s_veil[2] = 0;
}

void om_fade_out(uint16_t mask, uint16_t ms, uint8_t anim, om_fade_each each, void *ctx)
{
    om_fade(mask, s_veil[2], -255, ms, anim, each, ctx);   /* from the veil's deepest channel (blue backdrops) */
}

void om_fade_in(uint16_t mask, uint16_t ms, uint8_t anim, om_fade_each each, void *ctx)
{
    om_fade(mask, -255, 0, ms, anim, each, ctx);
}
