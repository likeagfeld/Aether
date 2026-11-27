/* menu/app/launchj.c -- the jump into LAUNCHER.BIN (Saturn only). BSD-3-Clause.
 * 1. Stop the slave SH-2: HWRAM is forfeit after the jump (spec §7.1 step 4; HWF-16), and a plain BIOS boot hands a
 *    game a halted slave. slSlaveOffWait() alone always failed under the menu's persistent
 *    INTBACK and the slave ran on into the game; om_slave_off_seq (slaveoff.h) cancels the INTBACK first, checks every
 *    return code and falls back to the direct SMPC SSHOFF (sshoff.h). The result is kept in om_slvoff_last (read from
 *    a savestate by the L1 gate, tools/omsat/slavegate.py); a failure does not stop the launch -- LAUNCHER.BIN issues
 *    its own SSHOFF and shows a status line if that fails too (lseq.c).
 * 2. Mask every interrupt level, as libsatiator's s_reset_to_satiator does before its jump (satiator.c:398-403).
 * 3. Purge the cache before executing the image that was just written (SGL slCashPurge, ST-238-R1).
 * The body was written through the cache-through alias (OM_CT, HWF-12), so memory already holds it. */
#include "sgl.h"
#include "hang.h"
#include "launch.h"
#include "omlayout.h"
#include "slaveoff.h"
#include "sshoff.h"

om_slvoff_res om_slvoff_last;

static void mask_all(void)
{
    Uint32 sr;
    __asm__ volatile ("stc sr,%0" : "=r"(sr));
    sr |= 0xF0;
    __asm__ volatile ("ldc %0,sr" : : "r"(sr) : "memory");
}

static int op_cancel(void *ctx) { (void)ctx; return slIntBackCancel() == OK ? 0 : -1; }
static void op_frame(void *ctx) { (void)ctx; slSynch(); }
static int op_offwait(void *ctx) { (void)ctx; return slSlaveOffWait() == OK ? 0 : -1; }
static int op_direct(void *ctx)
{   /* SGL's V-blank handler must not issue an SMPC command over ours: interrupts stay masked from here to the jump */
    (void)ctx;
    mask_all();
    return om_smpc_sshoff();
}

void om_launch_jump(void)
{
    static const om_slvoff_ops ops = { op_cancel, op_frame, op_offwait, op_direct, 0 };
    om_slave_off_seq(&ops, &om_slvoff_last);
    mask_all();
    OM_EXC_RESTORE();                                /* the BIOS's exception vectors, as a game expects them */
    slCashPurge();
    ((void (*)(void))OM_LAUNCHER_LOAD)();
    for (;;)
        ;
}
