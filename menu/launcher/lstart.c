/* menu/launcher/lstart.c -- LAUNCHER.BIN entry (0x00200000). The menu jumps here with every interrupt level masked
 * (menu/app/launchj.c). The stack goes to LWRAM just below the hand-off block (__stack_top = 0x0027F000, HWF-12).
 * Before main: .bss zeroed, then the screens' code (.lui, launcher.ld) copied to its High Work RAM home and the cache
 * purged -- its load image is the heap's first bytes, so this runs before any malloc. BSD-3-Clause. */
#include <stdint.h>
extern char _bss_start[], _bss_end[];
extern uint32_t __lui_start[], __lui_lma[], __lui_longs[];
void lmain(void);
volatile int om_lui_ok;                /* lgfx.h: .lui is in High Work RAM, intact */

void lstart_c(void)
{
    char *p;
    volatile uint32_t *d = (volatile uint32_t *)((uintptr_t)__lui_start | 0x20000000u);   /* cache-through, HWF-12 */
    const uint32_t *s = __lui_lma;
    uint32_t i, n = (uint32_t)(uintptr_t)__lui_longs;
    for (p = _bss_start; p < _bss_end; p++)
        *p = 0;
    for (i = 0; i < n; i++)
        d[i] = s[i];
    *(volatile uint8_t *)0xFFFFFE92u = 0x11;   /* CCR <- CP | CE: no stale line for the code just copied (HWF-13) */
    om_lui_ok = 1;
    lmain();
    for (;;)
        ;
}

__asm__(
    "    .section .lstart,\"ax\",@progbits\n"
    "    .global _lstart\n"
    "    .align 2\n"
    "_lstart:\n"
    "    mov.l   1f, r15\n"
    "    mov.l   2f, r0\n"
    "    jmp     @r0\n"
    "    nop\n"
    "    .align 2\n"
    "1:  .long   __stack_top\n"
    "2:  .long   _lstart_c\n"
    "    .text\n");
