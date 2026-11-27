/* menu/src/artsched.c -- see artsched.h. BSD-3-Clause. */
#include "artsched.h"

void om_artsched_init(om_artsched *s, uint16_t debounce_frames)
{
    s->want = s->shown = s->loading_item = OM_ART_NONE;
    s->still = 0;
    s->debounce = debounce_frames ? debounce_frames : 1;
    s->loading = 0;
    s->front = 0;
}

void om_artsched_select(om_artsched *s, uint32_t item)
{
    if (item != s->want) { s->want = item; s->still = 1; }
    else if (s->still < 0xFFFF) s->still++;
}

int om_artsched_should_start(om_artsched *s)
{
    if (s->loading || s->want == s->shown || s->want == OM_ART_NONE || s->still < s->debounce)
        return 0;
    s->loading = 1;
    s->loading_item = s->want;
    return 1;
}

int om_artsched_stale(const om_artsched *s) { return s->loading && s->loading_item != s->want; }
void om_artsched_abort(om_artsched *s) { s->loading = 0; s->loading_item = OM_ART_NONE; }
uint8_t om_artsched_back(const om_artsched *s) { return (uint8_t)(s->front ^ 1u); }

void om_artsched_done(om_artsched *s, int ok)
{
    if (ok) s->front ^= 1u;
    s->shown = s->loading_item;
    s->loading = 0;
    s->loading_item = OM_ART_NONE;
}

uint16_t om_debounce_frames(uint32_t hz_x100, uint32_t ms)
{
    return (uint16_t)((ms * hz_x100 + 99999u) / 100000u);   /* ceil(ms * Hz / 1000) */
}
