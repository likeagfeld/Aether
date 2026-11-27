/* menu/src/slaveoff.h -- stop the slave SH-2 before the jump to LAUNCHER.BIN (pure sequencer; the SGL and
 * SMPC calls come in through om_slvoff_ops, bound by menu/app/launchj.c and faked by test_slaveoff.c). BSD-3-Clause.
 *
 * Why a sequence: slSlaveOffWait() is slRequestCommand(SMPC_SSHOFF, SMPC_WAIT) (SL_DEF.H:1557), and "if an interrupt
 * back has been issued, execution of this function fails" (per ST-238-R1 slSlaveOffWait remarks). The menu's
 * slGetStatus() at boot keeps SGL's INTBACK on every frame (HWF-40), so the bare call always failed and the slave kept
 * running into the game (HWF-78; docs/research/gex-freeze-rootcause.md §4, measured on Mednafen: rc -1, SlaveSH2On 1).
 *  1. slIntBackCancel() ("clears the settings that were made by calling slGetPeripheral and slGetStatus", per
 *     ST-238-R1 slIntBackCancel), retried a frame apart: it fails only while SGL's SMPC semaphore is held.
 *  2. OM_SLV_SETTLE frames: the INTBACK already in flight ends by the next V-BLANK-IN at the latest (per
 *     SMPC_Manual.txt:2032-2034, termination condition 2), and SGL clears its "issued" bit at the next frame's send.
 *  3. slSlaveOffWait(), its return code checked, retried a frame apart.
 *  4. If SGL never accepted it: the direct SMPC SSHOFF (command 03H, a Type B command issued from the master only;
 *     SMPC_Manual.txt:718-721, :798, :1026-1027; DualCPU_Guide.txt:290-305), menu/launcher/sshoff.h.
 * Every step is bounded. Nothing here waits on the slave itself; a failure is returned, never spun on. */
#ifndef OM_SLAVEOFF_H
#define OM_SLAVEOFF_H

#define OM_SLV_OK_SGL      0         /* slSlaveOffWait accepted SSHOFF */
#define OM_SLV_OK_DIRECT   1         /* SGL refused it every time; the direct SSHOFF was accepted */
#define OM_SLV_E_FAILED  -70         /* neither path stopped the slave: the caller reports it and carries on */

#define OM_SLV_CANCEL_TRIES 4        /* slIntBackCancel attempts, one frame apart */
#define OM_SLV_SETTLE       2        /* frames between the cancel and the SSHOFF */
#define OM_SLV_OFF_TRIES    3        /* slSlaveOffWait attempts, one frame apart */

typedef struct {
    int  (*cancel)(void *ctx);       /* slIntBackCancel(): 0 = OK (SL_DEF.H OK), anything else = NG */
    void (*frame)(void *ctx);        /* slSynch(): one frame, interrupts on */
    int  (*offwait)(void *ctx);      /* slSlaveOffWait(): 0 = OK */
    int  (*direct)(void *ctx);       /* direct SSHOFF: 0 = accepted, < 0 = why not (sshoff.h) */
    void *ctx;
} om_slvoff_ops;

typedef struct {
    int result;                      /* OM_SLV_* */
    int cancel_rc, cancel_tries;     /* the last slIntBackCancel rc and how many calls */
    int off_rc, off_tries;           /* the last slSlaveOffWait rc and how many calls */
    int direct_rc;                   /* the direct SSHOFF's rc; 1 = not needed (not called) */
} om_slvoff_res;

int om_slave_off_seq(const om_slvoff_ops *ops, om_slvoff_res *r);   /* returns r->result */
#endif
