/* menu/launcher/igrguard.c -- SYS_EXECDMP's guard (Astal, hardware 2026-11-23: A+B+C+Start at the title froze with no
 * in-game-reset colour, because the game GFS_Loads LOG/APT/BPT/TIT.BIN to 0x06002000, over the stub, and SYS_EXECDMP
 * still pointed into it). 0x0600026C points here, at OM_IGR_GUARD (0x060003C0, the BIOS copyright text, HWF-80),
 * never straight at the stub. The guard sums the stub home 0x06002800-0x06002D48; the arming code sealed the staged
 * image (om_igr_seal, igrcheck.c) so an intact stub sums to OM_IGR_SEAL.
 *  - intact: tail-jump to the stub (om_igr_entry, which masks interrupts and returns the console to Aether);
 *  - anything else (an overlay, a cleared .bss, one changed long, IGR never armed): mask interrupts and reset the
 *    console with SMPC SYSRES (command 0DH: SF = 1, then COMREG; HWF-53), re-issued every ~0.1 s (0x100000 dt/bf
 *    rounds at 28.6 MHz) in case an SMPC command of the game's was still in flight -- the console reset that
 *    In Game Reset = Off gave under CWX (B1, HWF-80), now the fallback under both loaders.
 * One output section (.igrguard, launcher.ld), exactly 64 bytes, position-dependent only through its two literals;
 * tools/sbuild.py check_igr_guard runs the linked bytes on a small SH-2 model (intact / overwritten / zeroed stub).
 * Under IGR_HOST_TEST the same decision is a C function over fake memory (menu/host-tests/test_igrguard.c).
 * BSD-3-Clause. */
#include <stdint.h>
#include "igr.h"

#ifdef IGR_HOST_TEST
uint32_t igr_g_rd32(uint32_t a);
void igr_g_wr8(uint32_t a, uint8_t v);
void igr_g_enter(void);

void om_igr_guard(void)
{
    uint32_t a, s = 0;
    for (a = OM_IGR_STUB; a < OM_IGR_STUB_END; a += 4u)
        s += igr_g_rd32(a);
    if (s == OM_IGR_SEAL) {
        igr_g_enter();
        return;
    }
    for (;;) {
        igr_g_wr8(0x20100063u, 1u);            /* SMPC SF */
        igr_g_wr8(0x2010001Fu, 0x0Du);         /* SMPC COMREG = SYSRES */
    }
}
#else
/* OM_IGR_SEAL is 0xFFFFFFA5 = cmp/eq #-91; the count is (OM_IGR_STUB_END - OM_IGR_STUB) / 4 = 0x152 longs; COMREG
 * 0x2010001F = SF 0x20100063 - 0x44. The jmp's delay slot is the reset path's first instruction (stc sr,r0), which
 * the stub path executes harmlessly; COMREG is written in the bra's delay slot. */
__asm__(
    "    .section .igrguard,\"ax\",@progbits\n"
    "    .global _om_igr_guard\n"
    "    .align 2\n"
    "_om_igr_guard:\n"
    "    mov.l   3f,r1\n"                    /* the stub home */
    "    mov.w   4f,r3\n"                    /* its longs */
    "    mov     #0,r0\n"
    "1:  mov.l   @r1+,r2\n"
    "    dt      r3\n"
    "    bf.s    1b\n"
    "    add     r2,r0\n"
    "    cmp/eq  #-91,r0\n"                  /* == OM_IGR_SEAL */
    "    bf.s    5f\n"
    "    mov.l   3f,r1\n"
    "    jmp     @r1\n"                      /* intact: the stub */
    "5:  stc     sr,r0\n"                    /* damaged: mask every interrupt level */
    "    or      #0xF0,r0\n"
    "    ldc     r0,sr\n"
    "    mov.l   6f,r1\n"                    /* SMPC SF */
    "    mov     r1,r2\n"
    "    add     #-68,r2\n"                  /* SMPC COMREG */
    "    mov     #1,r3\n"
    "    mov     #13,r4\n"                   /* SYSRES */
    "7:  mov     #16,r0\n"
    "    shll16  r0\n"
    "8:  dt      r0\n"
    "    bf      8b\n"
    "    mov.b   r3,@r1\n"                   /* SF = 1 */
    "    bra     7b\n"
    "    mov.b   r4,@r2\n"                   /* COMREG = 0DH */
    "4:  .word   0x0152\n"
    "    .align 2\n"
    "3:  .long   0x06002800\n"
    "6:  .long   0x20100063\n"
    "    .text\n");
#endif
