/* menu/src/frcgap.c -- see frcgap.h. BSD-3-Clause. */
#include "frcgap.h"

void om_frcgap_start(om_frcgap *g, uint16_t frc, uint32_t us10k)
{
    g->ticks = 0;
    g->us10k = us10k;
    g->polls = 0;
    g->last = frc;
}

int om_frcgap_step(om_frcgap *g, uint16_t frc)
{
    uint32_t d = (uint16_t)(frc - g->last);
    g->last = frc;
    if (d * g->us10k < OM_FRCGAP_STEP_US * 10000u)        /* <= 65,535 x 47,629: fits 32 bits */
        g->ticks += d;
    return ++g->polls >= OM_FRCGAP_POLLS                   /* ticks x us10k stays under 4e8 + 2e7 */
        || g->ticks * g->us10k >= OM_FRCGAP_MAX_US * 10000u;
}
