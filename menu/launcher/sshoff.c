/* menu/launcher/sshoff.c -- see sshoff.h. Register map: SMPC at 0x20100000 (cache-through), IREG0 +0x01, COMREG +0x1F,
 * OREG31 +0x5F, SR +0x61, SF +0x63 (HWF-14; smpcpad.c). TVSTAT 0x25F80004 (HWF-10). BSD-3-Clause. */
#include "sshoff.h"
#include "smpcwait.h"

#define O_IREG0  0x01u
#define O_COMREG 0x1Fu
#define O_OREG31 0x5Fu
#define O_SR     0x61u
#define O_SF     0x63u

#ifdef SSHOFF_HOST_TEST
#define RD(o)    om_sshoff_rd(o)
#define WR(o, v) om_sshoff_wr((o), (v))
#define TVSTAT   om_sshoff_tvstat
#else
#define RD(o)    (*(volatile uint8_t *)(0x20100000u + (o)))
#define WR(o, v) (*(volatile uint8_t *)(0x20100000u + (o)) = (uint8_t)(v))
static uint16_t tvstat(void *ctx)
{
    (void)ctx;
    return *(volatile uint16_t *)0x25F80004u;
}
#define TVSTAT   tvstat
#endif

static int sf_idle(void)
{
    uint32_t t;
    for (t = 0; (RD(O_SF) & 1u) && t < OM_SSHOFF_SPINS; t++)
        ;
    return (RD(O_SF) & 1u) ? OM_SSHOFF_E_BUSY : 0;
}

int om_smpc_sshoff(void)
{   /* the break first and every SF wait before the window, as igrstub.c sysres() does,
     * so nothing long runs between finding the window and writing COMREG */
    int rc = 0, w;
    if (RD(O_SR) & 0x20u)
        WR(O_IREG0, 0x40u);
    if ((rc = sf_idle()) != 0)
        return rc;
    for (w = 0; w < OM_SSHOFF_WINDOWS; w++)
        if ((rc = om_intback_wait(TVSTAT, 0)) == 0)
            break;
    if (rc)
        return rc;
    if (RD(O_SF) & 1u)
        return OM_SSHOFF_E_BUSY;
    WR(O_SF, 1u);
    WR(O_COMREG, OM_SMPC_SSHOFF);
    if ((rc = sf_idle()) != 0)
        return rc;
    return RD(O_OREG31) == OM_SMPC_SSHOFF ? 0 : OM_SSHOFF_E_OREG;
}
