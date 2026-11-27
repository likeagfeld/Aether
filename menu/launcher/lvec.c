/* menu/launcher/lvec.c -- see lvec.h. The table and its 128 stubs live in the LAUNCHER.BIN body in LWRAM (HWF-12),
 * which nothing the launch does overwrites. SH-2 facts, per SH7604_Hardware.txt:
 *  - vector n is fetched from VBR + 4n (Table 4.4, lines 3939-3947); VBR must be a multiple of 4 (§4.8.2,
 *    lines 4285-4288), so the table is 16-aligned;
 *  - on an address-error, instruction or interrupt exception the CPU saves SR, then PC, on the R15 stack (§4.1.2
 *    lines 3821-3824, §4.3.2 lines 4080-4081), leaving @R15 = PC and @(4,R15) = SR (Table 4.11, lines 4259-4275);
 *    address-error and instruction exceptions leave the SR mask unchanged (lines 3822-3824).
 * Each stub pushes r0 and loads its vector number, so the common code finds vec in r0, PC at @(4,r15), SR at
 * @(8,r15). It then masks every level (SR = 0xF0: I3-I0 = 1111), resets r15 to __stack_top (the faulting stack may
 * be the cause) and jumps to om_exc, which never returns. Gate: tools/sbuild.py check_vectab re-reads the linked
 * body and checks every table word and every stub. HWF-25. BSD-3-Clause. */
#include "lseq.h"
#include "lvec.h"
#include "vdp2txt.h"

__asm__(
    "    .section .text.omvec,\"ax\",@progbits\n"
    "    .align 4\n"
    "    .global _om_vec_stubs\n"
    "_om_vec_stubs:\n"
    "    .set omv_n, 0\n"
    "    .rept 128\n"
    "    mov.l   r0,@-r15\n"
    "    mov     #omv_n,r0\n"
    "    bra     _om_vec_common\n"
    "    nop\n"
    "    .set omv_n, omv_n + 1\n"
    "    .endr\n"
    "    .global _om_vec_common\n"
    "_om_vec_common:\n"
    "    mov     r0,r4\n"
    "    mov.l   @(4,r15),r5\n"
    "    mov.l   @(8,r15),r6\n"
    "    mov.l   1f,r0\n"
    "    ldc     r0,sr\n"
    "    mov.l   2f,r15\n"
    "    mov.l   3f,r0\n"
    "    jmp     @r0\n"
    "    nop\n"
    "    .align 2\n"
    "1:  .long   0x000000F0\n"
    "2:  .long   __stack_top\n"
    "3:  .long   _om_exc\n"
    "    .section .rodata.omvec,\"a\",@progbits\n"
    "    .align 4\n"
    "    .global _om_vectab\n"
    "_om_vectab:\n"
    "    .set omv_n, 0\n"
    "    .rept 128\n"
    "    .long   _om_vec_stubs + omv_n * 8\n"
    "    .set omv_n, omv_n + 1\n"
    "    .endr\n"
    "    .text\n");

static uint32_t s_entry_vbr;
static int s_saved;

void om_vec_install(void)
{
    uint32_t v;
    if (!s_saved) {
        __asm__ volatile ("stc vbr,%0" : "=r"(v));
        s_entry_vbr = v;
        s_saved = 1;
    }
    __asm__ volatile ("ldc %0,vbr" : : "r"((uint32_t)om_vectab) : "memory");
}

void om_vec_to_entry(void)
{
    __asm__ volatile ("ldc %0,vbr" : : "r"(s_entry_vbr) : "memory");
}

uint32_t om_vec_entry_vbr(void) { return s_entry_vbr; }

static char *cat(char *d, const char *s)
{
    while (*s) *d++ = *s++;
    *d = 0;
    return d;
}

void om_exc(uint32_t vec, uint32_t pc, uint32_t sr)
{
    static char what[48], detail[48];
    char h[9];
    om_lerr e;
    char *p;
    om_txt_display(1);
    p = cat(what, "EXC: ");
    p = cat(p, om_exc_name(vec));
    p = cat(p, " PC=");
    om_hex8(h, pc);
    cat(p, h);
    p = cat(detail, "SR=");
    om_hex8(h, sr);
    p = cat(p, h);
    p = cat(p, " VBR0=");
    om_hex8(h, s_entry_vbr);
    cat(p, h);
    e.step = om_lseq_step();
    e.rc = (int)vec;
    e.what = what;
    e.detail = detail;
    om_fail_screen(&e);
}
