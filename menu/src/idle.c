/* menu/src/idle.c -- see idle.h. BSD-3-Clause. */
#include "idle.h"

uint32_t om_idle_vblanks(uint8_t minutes, uint32_t fps_x100) { return (uint32_t)minutes * 60u * fps_x100 / 100u; }
void om_idle_init(om_idle *i, uint32_t timeout, uint32_t now) { i->timeout = timeout; i->last = now; i->active = 0; }

void om_idle_set(om_idle *i, uint32_t timeout, uint32_t now)
{
    i->timeout = timeout;
    i->last = now;
    if (!timeout) i->active = 0;
}

int om_idle_step(om_idle *i, uint32_t now, int input)
{
    if (i->active) {
        if (!input) return OM_IDLE_RUN;
        i->active = 0;
        i->last = now;
        return OM_IDLE_WAKE;
    }
    if (input) { i->last = now; return OM_IDLE_RUN; }
    if (i->timeout && now - i->last >= i->timeout) { i->active = 1; return OM_IDLE_START; }
    return OM_IDLE_RUN;
}
