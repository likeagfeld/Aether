/* menu/src/stall.c -- see stall.h. BSD-3-Clause. */
#include "stall.h"

__attribute__((optimize("Os"))) int om_stall_step(om_stall *s, uint32_t beat, uint32_t limit)
{
    if (beat != s->beat) {
        s->beat = beat;
        s->n = 0;
        if (s->shown) { s->shown = 0; return OM_STALL_CLEAR; }
        return OM_STALL_NONE;
    }
    if (s->n < limit && ++s->n == limit) {
        s->shown = 1;
        return OM_STALL_SHOW;
    }
    return OM_STALL_NONE;
}

__attribute__((cold, optimize("Os"))) void om_stall_hex(char *out, const char *label, const uint32_t *v, int n)
{
    int i, b;
    while (*label) *out++ = *label++;
    for (i = 0; i < n; i++) {
        *out++ = ' ';
        for (b = 28; b >= 0; b -= 4) *out++ = "0123456789ABCDEF"[(v[i] >> b) & 15u];
    }
    *out = 0;
}
