/* menu/host-tests/test_padkind.c -- M6 Task 19. BSD-3-Clause. */
#include "th.h"
#include "padkind.h"
#include "gamesdb.h"

int main(void)
{
    CHECK_INT(om_pad_conflict(OM_PAD_3D_ANALOG, OM_CF_3DPAD), OM_PADKIND_TRUSTED);
    CHECK_INT(om_pad_conflict(OM_PAD_STD, OM_CF_3DPAD), 0);
    CHECK_INT(om_pad_conflict(OM_PAD_3D_DIGITAL, 0), 0);
    CHECK_INT(om_pad_conflict(OM_PAD_UNKNOWN, OM_CF_3DPAD), 0);           /* unknown never blocks */
    CHECK_INT(om_pad_kind(0xFF, 0), OM_PAD_NONE);                        /* SGL's PER_ID_NotConnect */
    /* HWF-77 (measured, relayed): standard pad and the 3D pad's digital mode share one id (0x02) -- SGL cannot
     * tell them apart, so this is the only kind om_pad_kind can return for it. */
    CHECK_INT(om_pad_kind(0x02, 2), OM_PAD_STD);
    CHECK_INT(om_pad_kind(0x16, 6), OM_PAD_3D_ANALOG);                   /* the 3D pad's analog mode */
    CHECK_INT(om_pad_kind(0x15, 5), OM_PAD_UNKNOWN);                     /* SL_DEF.H's generic PER_ID_StnAnalog: not
                                                                            * the 3D pad (one byte short of 0x16) */
    CHECK_INT(om_pad_kind(0x21, 3), OM_PAD_UNKNOWN);                     /* a pointing device: not a hazard this
                                                                            * table knows about */
    /* the actual hazard: a trusted 3D-analog pad on a Golden-Axe-like compat row conflicts; anything else does not */
    CHECK_INT(om_pad_conflict(om_pad_kind(0x16, 6), OM_CF_3DPAD | OM_CF_STDPAD), 1);
    CHECK_INT(om_pad_conflict(om_pad_kind(0x02, 2), OM_CF_3DPAD | OM_CF_STDPAD), 0);   /* std pad: never warns */
    CHECK_INT(om_pad_conflict(om_pad_kind(0x16, 6), 0), 0);              /* no compat flag: never warns */
    return th_done("test_padkind");
}
