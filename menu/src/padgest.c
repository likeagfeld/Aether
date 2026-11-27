/* menu/src/padgest.c -- see padgest.h. BSD-3-Clause. */
#include "padgest.h"

void om_cgest_init(om_cgest *g, uint16_t held)
{
    g->prev = held;
    g->armed = 0;
    g->spoiled = (uint8_t)(held != 0);           /* held at entry: not a press, wait for all buttons up */
}

int om_cgest_step(om_cgest *g, uint16_t held, uint16_t c_mask)
{
    uint16_t newly = (uint16_t)(held & ~g->prev);
    int fire = 0;
    g->prev = held;
    if (!held) {                                  /* everything up: fire if C alone was just released */
        fire = g->armed && !g->spoiled;
        g->armed = 0;
        g->spoiled = 0;
        return fire;
    }
    if (held & ~c_mask)
        g->spoiled = 1;                           /* another button joined: never a reset this time */
    else if ((newly & c_mask) && !g->spoiled)
        g->armed = 1;
    return 0;
}
