/* menu/app/hang.c -- see hang.h. BSD-3-Clause.
 * The V-blank hook's stack, as measured (Mednafen, a forced spin in the popup's key path, the hook called by SGL's
 * V-blank handler, LIBSGL.A): at om_hang_vbl's entry @(96,r15) is the interrupted PC (the CPU pushed SR, then PC:
 * SH7604 section 4.3.2), @(56,r15) the interrupted PR (the handler's own push, its GBR 0x060FFC00 one word below) and
 * r15 + 104 the interrupted stack; the screen read PC 060176A4 (inside the spin), PR 06017668 and SP 060FF9C8, each the
 * savestate's own value. SGL's code is the same on the console, so the offsets are too. */
#include "sgl.h"
#include "satiator.h"
#include "audio.h"
#include "hang.h"
#include "stall.h"
#include "text.h"
#ifdef OM_HANG_ON

volatile uint32_t om_hang_beat;
volatile uint8_t om_hang_tag;
volatile uint8_t om_hang_clr;     /* the hook cleared a shown stall: om_hang_reclear writes it again */
static om_stall s_st;

void om_hang_check(const uint32_t *sp);
__asm__(
    "    .section .text.om_hang_vbl,\"ax\",@progbits\n"
    "    .align 2\n"
    "    .global _om_hang_vbl\n"
    "_om_hang_vbl:\n"
    "    mov.l   1f,r0\n"
    "    jmp     @r0\n"
    "    mov     r15,r4\n"
    "    .align 2\n"
    "1:  .long   _om_hang_check\n"
    "    .text\n");

/* one NBG0 row: label and n hex words (n 0: a blank row) -- cold */
static __attribute__((cold, noinline, optimize("Os"))) void row(int y, const char *label, const uint32_t *v, int n)
{
    char l[41];
    om_stall_hex(l, n ? label : "", v, n);
    om_text_put(0, y, l, 40);
}

uint32_t om_exc[4];                  /* an exception: vector, PC, SR, PR (the stubs below write it) */

/* NBG0 row 2: the stall (sp: the hook's stack, see above), or blank (sp 0); row 3 an exception's registers */
static __attribute__((cold, noinline, optimize("Os"))) void show(const uint32_t *sp)
{
    uint32_t v[4];
    if (sp) { v[0] = sp[24]; v[1] = sp[14]; v[2] = (uint32_t)(sp + 26); v[3] = om_hang_tag; }
    row(2, "HANG", v, sp ? 4 : 0);                              /* PC, PR, SP, the phase */
    if (om_exc[0]) row(3, "EXC", om_exc, 4);
    /* row 4, at the show and at the clear, then kept (satiator.h s_diag): timeouts, the last one's CR1 word, the
     * longest completed wait in polls (~1.4 us each, HWF-54), commands completed -- a stall that timed out
     * (SAT_HIRQ_WAIT_CAP polls, ~28 s) against one that was only slow, and (ok moving) whether the clear ran */
    v[0] = s_diag.tmo; v[1] = s_diag.tmo_cmd; v[2] = s_diag.max_polls; v[3] = s_diag.ok;
    row(4, "SAT", v, 4);
    om_hang_clr = !sp;
}

__attribute__((cold, noinline, optimize("Os"))) void om_hang_reclear(void)
{
    om_hang_clr = 0;
    row(2, "", 0, 0);
}

__attribute__((optimize("Os"))) void om_hang_check(const uint32_t *sp)
{
    int ev;
    om_audio_vblank();
    ev = om_stall_step(&s_st, om_hang_beat, OM_STALL_VB);
    if (ev != OM_STALL_NONE) show(ev == OM_STALL_SHOW ? sp : 0);
}

/* --- the CPU exceptions ------------------------------------------------------------------------------------------
 * The CPU pushed SR, then PC (@R15 = PC, @(4,R15) = SR); address-error and illegal-instruction exceptions leave the
 * interrupt mask as it was (SH7604 section 4.1.2), so the V-blank hook goes on. Each stub pushes r0 and loads its
 * vector; the common code stores vector, PC, SR and PR in om_exc and stops there, so the frame
 * beat stops too and the watchdog shows them with its own lines (no A+B+C+Start then: the reset button). */
__asm__(
    "    .section .text.om_exc_stubs,\"ax\",@progbits\n"
    "    .align 2\n"
    "_om_exc4:  mov.l r0,@-r15\n    bra _om_exc_common\n    mov #4,r0\n"
    "_om_exc6:  mov.l r0,@-r15\n    bra _om_exc_common\n    mov #6,r0\n"
    "_om_exc9:  mov.l r0,@-r15\n    bra _om_exc_common\n    mov #9,r0\n"
    "_om_exc10: mov.l r0,@-r15\n    bra _om_exc_common\n    mov #10,r0\n"
    "_om_exc_common:\n"
    "    mov.l   1f,r1\n"
    "    mov.l   r0,@r1\n"
    "    mov.l   @(4,r15),r0\n"
    "    mov.l   r0,@(4,r1)\n"
    "    mov.l   @(8,r15),r0\n"
    "    mov.l   r0,@(8,r1)\n"
    "    sts     pr,r0\n"
    "    mov.l   r0,@(12,r1)\n"
    "    mov     r15,r0\n"
    "    add     #12,r0\n"
    "    mov.l   r0,@(16,r1)\n"
    "2:  bra     2b\n"
    "    nop\n"
    "    .align 2\n"
    "1:  .long   _om_exc\n"
    "    .section .data.om_exc_tab,\"aw\",@progbits\n"
    "    .align 2\n"
    "    .global _om_exc_tab\n"
    "_om_exc_tab: .long _om_exc4, _om_exc6, _om_exc9, _om_exc10\n"
    "    .text\n");
extern uint32_t om_exc_tab[4];
static const uint8_t k_vec[4] = { 4, 6, 9, 10 };

/* the four vectors and om_exc_tab trade places: once installs ours (om_exc_tab then holds the BIOS's), twice undoes */
__attribute__((cold, optimize("Os"))) void om_exc_swap(void)
{
    uint32_t *t, i, x;
    __asm__ volatile ("stc vbr,%0" : "=r"(t));
    for (i = 0; i < 4u; i++) { x = t[k_vec[i]]; t[k_vec[i]] = om_exc_tab[i]; om_exc_tab[i] = x; }
}
#endif
