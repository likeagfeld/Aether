/* menu/launcher/igr.c -- arming the in-game reset (M6 Task 20a; see igr.h for the chain, task-20a-report.md for
 * the BIOS evidence). Three pieces:
 *  - om_igr_install/uninstall (launcher, before/after the loadcd_boot loop): the BIOS copy vector 0x06000238;
 *  - om_igr_w (launcher .text, LWRAM): what the BIOS calls through that vector. The BIOS calls it only from
 *    loadcd_boot's last step (the save of 0x060002A0/0x06000380 before the warm re-init: descriptor {8, 0x06000D00,
 *    0x060002A0} then {0x10, 0x06000D20, 0x06000380}, ROM 0x19E8 on v1.00-v1.03), with r7 = the vector itself for
 *    the second call. It keeps r0-r7 and pr, arms once on the first descriptor, then tail-jumps to the BIOS routine;
 *  - the hook (section .igrhook, linked at OM_IGR_STAGE): the cell at STAGE+0 is what 0x06002270 points at, so the
 *    security code's `mov.l @r2,r2; jsr @r2` lands on the hook. The hook copies the stub image to OM_IGR_STUB,
 *    then the guard image to OM_IGR_GUARD (igrguard.c; the BIOS re-init that would restore 0x060003C0 has already
 *    run), checks the sum of both against the one om_igr_w stored (after sealing the staged stub, om_igr_seal), sets
 *    0x0600026C = the guard, puts 0x06002270 back, and tail-jumps
 *    (jmp, pr untouched) to the BIOS read at *(0x06000284), which may overwrite the staging area. Writes go through
 *    the cached addresses: the SH-2 cache is write-through and updates hit lines, so no purge is needed.
 * BSD-3-Clause. */
#include <stdint.h>
#include <string.h>
#include "igr.h"

extern char __igr_lma[], __igr_hook_start[], __igr_hook_end[], __igr_stub_start[], __igr_stub_end[];
extern char __igr_guard_start[], __igr_guard_end[];
extern char om_igr_bios_execdmp[];                 /* igrstub.c: a long in the stub image (VMA in its home) */
extern uint32_t om_igr_sum;
void om_igr_w(void);

__attribute__((used)) static uint32_t s_orig238;   /* read by om_igr_w (asm) */
static int s_installed, s_armed;

static uint32_t stage_bytes(void)
{
    return (uint32_t)(__igr_hook_end - __igr_hook_start) + (uint32_t)(__igr_stub_end - __igr_stub_start)
           + (uint32_t)(__igr_guard_end - __igr_guard_start);
}

void om_igr_w_arm(const uint32_t *d)
{
    uint32_t *p;
    uint32_t sum = 0, i, n;
    if (s_armed || d[0] != 8u || d[1] != 0x06000D00u || d[2] != 0x060002A0u)
        return;
    s_armed = 1;
    if (om_igr_ip_check((const uint8_t *)OM_IGR_IP, stage_bytes()) != OM_IGR_OK)
        return;                                                    /* the IP the BIOS loaded: re-checked */
    memcpy((void *)OM_IGR_STAGE, __igr_lma, stage_bytes());
    p = (uint32_t *)(OM_IGR_STAGE + (uint32_t)(__igr_hook_end - __igr_hook_start));   /* the staged stub image */
    n = (uint32_t)(__igr_stub_end - __igr_stub_start) / 4u;
    /* a lid exit goes to the BIOS's own SYS_EXECDMP (igrstub.c): 0x0600026C as the BIOS set it -- the warm re-init
     * after this restores the same ROM value, and nothing of ours has written it yet in this boot */
    p[(uint32_t)(om_igr_bios_execdmp - __igr_stub_start) / 4u] =
        om_igr_bios_execdmp_of(*(volatile uint32_t *)OM_IGR_EXECDMP);
    (void)om_igr_seal(p, n);                                       /* what SYS_EXECDMP's guard checks (igrguard.c) */
    n += (uint32_t)(__igr_guard_end - __igr_guard_start) / 4u;      /* the hook sums stub + guard as it copies them */
    for (i = 0; i < n; i++)
        sum += p[i];
    om_igr_sum = sum;                                              /* VMA in the staging area */
    *(volatile uint32_t *)OM_IGR_SEC_CELL = (uint32_t)__igr_hook_start;   /* the cell, STAGE + 0 */
}

int om_igr_install(void)
{
    int rc = om_igr_ip_check((const uint8_t *)OM_IGR_IP, stage_bytes());
    if (rc != OM_IGR_OK)
        return rc;
    s_armed = 0;
    s_orig238 = *(volatile uint32_t *)OM_IGR_COPYVEC;
    *(volatile uint32_t *)OM_IGR_COPYVEC = (uint32_t)om_igr_w;
    s_installed = 1;
    return OM_IGR_OK;
}

void om_igr_uninstall(void)
{
    if (s_installed)
        *(volatile uint32_t *)OM_IGR_COPYVEC = s_orig238;
    s_installed = 0;
}

__asm__(
    "    .text\n"
    "    .global _om_igr_w\n"
    "    .align 2\n"
    "_om_igr_w:\n"                     /* r0 = descriptor; keep r0-r7 (r7 = this vector for the 2nd call) */
    "    mov.l   r0,@-r15\n"
    "    mov.l   r1,@-r15\n"
    "    mov.l   r2,@-r15\n"
    "    mov.l   r3,@-r15\n"
    "    mov.l   r4,@-r15\n"
    "    mov.l   r5,@-r15\n"
    "    mov.l   r6,@-r15\n"
    "    mov.l   r7,@-r15\n"
    "    sts.l   pr,@-r15\n"
    "    mov.l   1f,r1\n"
    "    jsr     @r1\n"
    "    mov     r0,r4\n"
    "    lds.l   @r15+,pr\n"
    "    mov.l   @r15+,r7\n"
    "    mov.l   @r15+,r6\n"
    "    mov.l   @r15+,r5\n"
    "    mov.l   @r15+,r4\n"
    "    mov.l   @r15+,r3\n"
    "    mov.l   @r15+,r2\n"
    "    mov.l   @r15+,r1\n"
    "    mov.l   @r15+,r0\n"
    "    mov.l   2f,r1\n"                 /* the BIOS routine clobbers r1 itself (ROM 0x2C0) */
    "    mov.l   @r1,r1\n"
    "    jmp     @r1\n"
    "    nop\n"
    "    .align 2\n"
    "1:  .long   _om_igr_w_arm\n"
    "2:  .long   _s_orig238\n"
    "\n"
    "    .section .igrhook,\"ax\",@progbits\n"
    "    .global ___igr_cell\n"
    "    .global _om_igr_sum\n"
    "    .align 2\n"
    "___igr_cell:\n"
    "    .long   _om_igr_hook\n"             /* STAGE + 0: *(0x06002270) = &cell, *cell = hook */
    "_om_igr_sum:\n"
    "    .long   0\n"
    "_om_igr_hook:\n"
    "    mov.l   r5,@-r15\n"                 /* r4-r7 reach the BIOS read as the security code left them (M2) */
    "    mov.l   10f,r1\n"                   /* the stub image, staged right after this section */
    "    mov.l   11f,r2\n"                   /* OM_IGR_STUB */
    "    mov.l   12f,r3\n"                   /* longs */
    "    mov     #0,r5\n"
    "4:  mov.l   @r1+,r0\n"
    "    add     r0,r5\n"
    "    mov.l   r0,@r2\n"
    "    dt      r3\n"
    "    bf.s    4b\n"
    "    add     #4,r2\n"
    "    mov.l   14f,r2\n"                   /* the guard image, staged right after the stub image (r1) */
    "    mov.l   18f,r3\n"
    "6:  mov.l   @r1+,r0\n"
    "    add     r0,r5\n"
    "    mov.l   r0,@r2\n"
    "    dt      r3\n"
    "    bf.s    6b\n"
    "    add     #4,r2\n"
    "    mov.l   13f,r1\n"
    "    mov.l   @r1,r0\n"
    "    cmp/eq  r0,r5\n"
    "    bf      5f\n"                       /* staging damaged: leave SYS_EXECDMP to the BIOS CD player */
    "    mov.l   14f,r0\n"
    "    mov.l   15f,r1\n"
    "    mov.l   r0,@r1\n"                   /* *(0x0600026C) = the guard; it enters the stub while intact */
    "5:  mov.l   16f,r1\n"
    "    mov.l   17f,r0\n"
    "    mov.l   r0,@r1\n"                   /* *(0x06002270) = 0x06000284 again */
    "    mov.l   @r0,r0\n"
    "    mov.l   @r15+,r5\n"
    "    jmp     @r0\n"                      /* the BIOS read; returns to the security code */
    "    nop\n"
    "    .align 2\n"
    "10: .long   ___igr_hook_end\n"
    "11: .long   ___igr_stub_start\n"
    "12: .long   ___igr_stub_longs\n"
    "13: .long   _om_igr_sum\n"
    "14: .long   _om_igr_guard\n"
    "15: .long   0x0600026C\n"
    "16: .long   0x06002270\n"
    "17: .long   0x06000284\n"
    "18: .long   ___igr_guard_longs\n"
    "    .text\n");
