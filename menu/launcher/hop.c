/* menu/launcher/hop.c -- exit B of the launcher error screen: return to the official menu with S_BOOT_NO_AUTOLOAD
 * (spec §7.1 step 4). Same steps as libsatiator's s_reset_to_satiator (public domain, satiator.c:390-410), minus
 * the s_mode call, which the caller makes first. This code is in section .hop, which launcher.ld asserts lies above
 * 0x00201000. The BIOS call writes the official menu's first 4 KB over 0x00200000-0x00200FFF (the 4 KB size is
 * UNVERIFIED-ACCEPTED, from docs/research/research_satiator.md:55-56: upstream init.c's "the BIOS only reads 0x1000
 * bytes" and the Satiator serving menu.bin from offset 0x1000; HWF-19), where this launcher's
 * own .text begins, so nothing below 0x00201000 may run after that call. Hence: no function calls besides the two
 * computed jumps (gate: check_launcher wants 2 jsr and 0 jmp/bsrf/braf/bsr in .hop, every direct branch inside
 * .hop). Before the second jump the cache is purged inline (Ruling M2-R8): 0x00200000.. was just rewritten behind
 * the cache, which may still hold the launcher's own lines for those addresses. The store is SEGA's CSH_AllClr
 * (slCashPurge) sequence, measured by the controller in LIBSGL.A sglD00.o +0x5A: CCR (0xFFFFFE92) <- 0x11, CP|CE,
 * one byte store (CCR address and CP = bit 4 per SH7604_Hardware.txt:3385, 9570; HWF-13). SH7604 §8.4.6
 * (SH7604_Hardware.txt:9897-9909) warns that while CE = 1 the instructions prefetched around a CCR write still
 * come from the cache; that matters only for stale lines, and .hop's own lines (above 0x00201000) are not stale.
 * HWF-13, HWF-19, HWF-22. BSD-3-Clause. */
#include <stdint.h>
#include "satiator.h"
#include "vdp2txt.h"

__attribute__((section(".hop"), noinline, noreturn)) void om_hop_to_official(void)
{
    int (**get_mpeg_rom)(uint32_t, uint32_t, uint32_t) = (int (**)(uint32_t, uint32_t, uint32_t))0x06000298;
    volatile uint16_t *back = (volatile uint16_t *)(0x25E00000u + OM_TXT_BACK_OFF);
    uint32_t sr;
    __asm__ volatile ("stc sr,%0" : "=r"(sr));
    sr |= 0xF0;
    __asm__ volatile ("ldc %0,sr" : : "r"(sr));
    if ((*get_mpeg_rom)(2, 2, 0x200000) < 0)
        for (;;) *back = 0x001F;          /* red back screen: a visible failure that runs no overwritten code */
    __asm__ volatile ("mov.l   1f,r1\n\t"            /* CCR, HWF-13 */
                      "mov     #17,r0\n\t"           /* 0x11 = CP | CE */
                      "mov.b   r0,@r1\n\t"
                      "bra     2f\n\t"
                      "nop\n\t"
                      ".align  2\n"
                      "1:  .long   0xFFFFFE92\n"
                      "2:\n" : : : "r0", "r1", "memory");
    ((void (*)(uint32_t))0x200000)(S_BOOT_NO_AUTOLOAD);
    for (;;) *back = 0x7C00;              /* blue: the official menu returned, which it never should */
}
