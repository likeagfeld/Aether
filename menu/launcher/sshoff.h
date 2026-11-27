/* menu/launcher/sshoff.h -- the SMPC SSHOFF command (slave SH-2 off), issued directly, bare metal (no SGL).
 * Linked into LAUNCHER.BIN (its own guarantee before the boot, lseq.c) and into the menu (slaveoff.h step 4, when SGL
 * refused). HWF-78. BSD-3-Clause.
 *
 *  1. An INTBACK still waiting for continue or break (SR bit 5, HWF-14) is broken first: IREG0 bit 6
 *     (SMPC_Manual.txt:2007-2010). SF may read 1 while the SMPC waits (:1736-1738), so the break is not held behind
 *     the SF wait.
 *  2. No command in flight: SF clear, bounded (the sequential-issue rule, SMPC_Manual.txt:1004-1007).
 *  3. The issue window: never within 300 us of V-BLANK-IN ("issuing commands for 300 us from V-BLANK-IN is
 *     prohibited", SMPC_Manual.txt:885-893). om_intback_wait (smpcwait.h, HWF-14) gives V-BLANK-IN + 7 lines, inside
 *     V-BLANK; tried for up to OM_SSHOFF_WINDOWS fields. Nothing long runs after it: SF is only read once more
 *     (review M2; the same order as igrstub.c sysres()).
 *  4. Type B (SMPC_Manual.txt:798, Figure 2.2; DualCPU_Guide.txt:290-305 InitSlaveCPU): SF = 1, COMREG = 03H
 *     (SSHOFF, master only: SMPC_Manual.txt:718-721, :1026-1027), then SF clear, bounded.
 *  5. OREG31 must read 03H, SSHOFF's result parameter (SMPC_Manual.txt:1119-1135): the command the SMPC ran.
 * SSHOFF on a slave that is already off is what games do at start-up after a plain BIOS boot (Gex's main sends SSHOFF
 * then SSHON; docs/research/gex-freeze-rootcause.md §4), so a second SSHOFF is harmless. Returns 0, or a code below
 * or one of smpcwait.h's OM_SMPC_E_VBLANK / _DELAY / _WINDOW (no window found: nothing was issued). Never hangs. */
#ifndef OM_SSHOFF_H
#define OM_SSHOFF_H
#include <stdint.h>
#define OM_SMPC_SSHOFF      0x03u
#define OM_SSHOFF_WINDOWS   3         /* fields tried for the issue window */
#define OM_SSHOFF_SPINS     1000000u  /* SF polls per wait (as smpcpad.c SF_SPINS) */
#define OM_SSHOFF_E_BUSY    -63       /* SF stayed set (= smpcpad.h OM_SMPC_E_BUSY) */
#define OM_SSHOFF_E_OREG    -65       /* SF cleared but OREG31 is not 03H: the SMPC did not run SSHOFF */
int om_smpc_sshoff(void);

#ifdef SSHOFF_HOST_TEST               /* menu/host-tests/test_sshoff.c supplies the registers */
uint8_t om_sshoff_rd(uint32_t off);
void om_sshoff_wr(uint32_t off, uint8_t v);
uint16_t om_sshoff_tvstat(void *ctx);
#endif
#endif
