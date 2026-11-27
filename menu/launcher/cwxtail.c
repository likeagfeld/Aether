/* menu/launcher/cwxtail.c -- the last code of a CWX boot (M6 Task 17; cwxboot.h). One output section, .cwxtail,
 * linked at OM_CWX_TAIL (0x002FF000, the top 4 KB of Low Work RAM, launcher.ld) and copied there by cwxboot.c after
 * every fallible step. Two entry points, both self-contained (no stack below 0x06002000 is assumed, no call outside
 * the section but the two BIOS/IP calls; sbuild.py check_cwx audits every literal and branch of the linked bytes):
 *
 * _om_cwx_go (jumped to once, never returns): the equivalent of Pseudo Saturn Kai's bootstub + ipprog (cafe-alpha
 *   and Pseudo Saturn Team, GPL-2.0; bootstub/bootstub.c "CWX HRAM/LRAM loader", bootstub/bootstub_ipprog.S, itself
 *   from Theo Berkau's Pseudo Saturn crt0.s; re-implemented here, not copied -- docs/research/m6-research.md §2.2
 *   rows 20-22, §2.6 item 3):
 *    1. SP = 0x06002000 (ipprog "fix stack"; inside the Master stack area ST-040-R4 §4.3 STACK-M names);
 *    2. zero Low Work RAM from 0x00200000 up to this section (row 20: bootstub's LRAM clear; the launcher itself goes);
 *    3. an LWRAM program (exec != data): copy it from its HWRAM temporary copy and zero that copy (bootstub);
 *    4. zero 0x06000290 and 0x060002B0 ("Setup a few variables or the ip will die": unverified, from
 *       bootstub_ipprog.S -- empirical in Kai, not documented in the DTS set; HWF-76 (a));
 *    5. SYS_CHGSCUIM(0xFFFFFFFF, 0xFFFF7FFF) through the BIOS vector 0x06000344 (ipprog; SEGA_SYS.H SYS_CHGSCUIM);
 *    6. purge the cache: CCR (0xFFFFFE92) <- 0x11, SEGA's CSH_AllClr store (HWF-13, as hop.c; SH7604_Hardware.txt
 *       §8.2 CP bit) -- row 22: Kai has no purge here; the IP and the program were written through the cache;
 *    7. jsr 0x06002100, the IP code after the System ID; an endless loop if it ever returns.
 *
 * _om_cwx_hook (the resident chain: the in-game reset, then Kai's cheat block, M6 Task 27b): cwxboot.c points the IP's
 *   security-code cell 0x06002270 at ___cwx_cell when it arms anything. The security code calls through that cell
 *   for the BIOS's "read first program" vector 0x06000284 after it has unpacked the SEGA logo (igr.h, Task 20a's
 *   measurement in the retail IP), so the stub's home 0x06002800 -- the logo's packed data -- is dead by then.
 *   Under CWX the 1st Read File is already in RAM, so the JHL staging area 0x06009800 may hold the game: the
 *   image is staged here instead, at ___cwx_img (HWF-76 (b): nothing writes the LWRAM top before the hook). The hook copies ___cwx_res.len bytes from ___cwx_img to
 *   ___cwx_res.dst, zeroing the staged copy as it goes (the LWRAM clear, finished), checks the sum, sets
 *   SYS_EXECDMP (0x0600026C) = ___cwx_res.execdmp when it is non-zero, puts 0x06000284 back in the cell and
 *   tail-jumps (jmp, pr untouched) to the BIOS routine, which returns to the security code. r5/r6 are kept, r4 and
 *   r7 untouched (as the JHL hook in igr.c, which keeps what the security code passes to the BIOS read).
 *   M6 Task 27b: then ___cwx_chk.len bytes of Kai's cheat block (chkai.h) from ___cwx_chimg (right after the stub
 *   image) to ___cwx_chk.dst, the same way; only when its sum matches does the master address get the trapa
 *   (___cwx_chk.master <- 16-bit ___cwx_chk.trapa), last, so no trapa can reach a half-copied block, and that line is
 *   purged. Vector (trapa & 15) already points at the block (cwxboot.c, SYS_SETSINT at the hand-off). With the cheats
 *   off ___cwx_chk.len is 0 and the hook writes exactly what it wrote before Task 27b.
 * BSD-3-Clause. */

__asm__(
    "    .section .cwxtail,\"ax\",@progbits\n"
    "    .global _om_cwx_go\n"
    "    .global _om_cwx_hook\n"
    "    .global ___cwx_cell\n"
    "    .global ___cwx_par\n"
    "    .global ___cwx_res\n"
    "    .align 2\n"
    "_om_cwx_go:\n"
    "    mov.l   20f,r15\n"                  /* 1. SP = 0x06002000 */
    "    mov.l   21f,r1\n"                   /* 2. 0x00200000 .. ___cwx_tail_start */
    "    mov.l   22f,r2\n"
    "    mov     #0,r0\n"
    "1:  mov.l   r0,@r1\n"
    "    add     #4,r1\n"
    "    cmp/hs  r2,r1\n"
    "    bf      1b\n"
    "    mov.l   23f,r3\n"                   /* 3. ___cwx_par = {exec, data, bytes (a multiple of 4)} */
    "    mov.l   @r3,r4\n"
    "    mov.l   @(4,r3),r5\n"
    "    mov.l   @(8,r3),r6\n"
    "    cmp/eq  r4,r5\n"
    "    bt      3f\n"                       /* read in place (HWRAM program), or no 1st Read File */
    "    tst     r6,r6\n"
    "    bt      3f\n"
    "2:  mov.l   @r5,r0\n"
    "    mov.l   r0,@r4\n"
    "    mov     #0,r0\n"
    "    mov.l   r0,@r5\n"
    "    add     #4,r5\n"
    "    add     #-4,r6\n"
    "    cmp/pl  r6\n"
    "    bt.s    2b\n"
    "    add     #4,r4\n"
    "3:  mov     #0,r0\n"                    /* 4. the two IP work words */
    "    mov.l   24f,r1\n"
    "    mov.l   r0,@r1\n"
    "    mov.l   25f,r1\n"
    "    mov.l   r0,@r1\n"
    "    mov.l   26f,r0\n"                   /* 5. SYS_CHGSCUIM(-1, 0xFFFF7FFF) */
    "    mov.l   @r0,r0\n"
    "    mov.l   27f,r5\n"
    "    jsr     @r0\n"
    "    mov     #-1,r4\n"
    "    mov.l   28f,r1\n"                   /* 6. CCR <- 0x11 (CP | CE) */
    "    mov     #17,r0\n"
    "    mov.b   r0,@r1\n"
    "    mov.l   29f,r0\n"                   /* 7. the IP */
    "    jsr     @r0\n"
    "    nop\n"
    "4:  bra     4b\n"
    "    nop\n"
    "    .align 2\n"
    "20: .long   0x06002000\n"
    "21: .long   0x00200000\n"
    "22: .long   ___cwx_tail_start\n"
    "23: .long   ___cwx_par\n"
    "24: .long   0x06000290\n"
    "25: .long   0x060002B0\n"
    "26: .long   0x06000344\n"
    "27: .long   0xFFFF7FFF\n"
    "28: .long   0xFFFFFE92\n"
    "29: .long   0x06002100\n"
    "\n"
    "_om_cwx_hook:\n"
    "    mov.l   r5,@-r15\n"
    "    mov.l   r6,@-r15\n"
    "    mov.l   30f,r6\n"                 /* ___cwx_res = {dst, len (bytes, a multiple of 4), sum, execdmp} */
    "    mov.l   @(4,r6),r3\n"
    "    tst     r3,r3\n"
    "    bt      7f\n"                     /* no in-game reset: on to the cheats */
    "    mov.l   @r6,r2\n"
    "    mov.l   31f,r1\n"                 /* the staged image */
    "    shlr2   r3\n"
    "    mov     #0,r5\n"
    "5:  mov.l   @r1,r0\n"
    "    add     r0,r5\n"
    "    mov.l   r0,@r2\n"
    "    mov     #0,r0\n"
    "    mov.l   r0,@r1\n"
    "    add     #4,r1\n"
    "    dt      r3\n"
    "    bf.s    5b\n"
    "    add     #4,r2\n"
    "    mov.l   @(8,r6),r0\n"
    "    cmp/eq  r0,r5\n"
    "    bf      7f\n"                     /* staging damaged: leave SYS_EXECDMP to the BIOS */
    "    mov.l   @(12,r6),r0\n"
    "    tst     r0,r0\n"
    "    bt      7f\n"
    "    mov.l   32f,r1\n"
    "    mov.l   r0,@r1\n"                 /* *(0x0600026C) = the resident's SYS_EXECDMP entry */
    "7:  mov.l   35f,r6\n"                 /* ___cwx_chk = {dst, len, sum, master, trapa}: Kai's cheat block */
    "    mov.l   @(4,r6),r3\n"
    "    tst     r3,r3\n"
    "    bt      6f\n"
    "    mov.l   @r6,r2\n"
    "    mov.l   36f,r1\n"                 /* the staged cheat block, after the stub image */
    "    shlr2   r3\n"
    "    mov     #0,r5\n"
    "8:  mov.l   @r1,r0\n"
    "    add     r0,r5\n"
    "    mov.l   r0,@r2\n"
    "    mov     #0,r0\n"
    "    mov.l   r0,@r1\n"
    "    add     #4,r1\n"
    "    dt      r3\n"
    "    bf.s    8b\n"
    "    add     #4,r2\n"
    "    mov.l   @(8,r6),r0\n"
    "    cmp/eq  r0,r5\n"
    "    bf      6f\n"                     /* staging damaged: no trapa, the cheats stay inert */
    "    mov.l   @(12,r6),r1\n"            /* the master address */
    "    mov.l   @(16,r6),r0\n"            /* the trapa (C30x) */
    "    mov.w   r0,@r1\n"                 /* written last: the block is whole by now */
    "    mov.l   37f,r0\n"
    "    or      r1,r0\n"
    "    mov     #-16,r2\n"
    "    and     r2,r0\n"
    "    mov.l   r0,@r0\n"                 /* associative purge of its line (SH7604_Hardware.txt 8.4.7) */
    "6:  mov.l   33f,r1\n"
    "    mov.l   34f,r0\n"
    "    mov.l   r0,@r1\n"                 /* *(0x06002270) = 0x06000284 again */
    "    mov.l   @r0,r0\n"
    "    mov.l   @r15+,r6\n"
    "    mov.l   @r15+,r5\n"
    "    jmp     @r0\n"                    /* the BIOS read; returns to the security code */
    "    nop\n"
    "    .align 2\n"
    "30: .long   ___cwx_res\n"
    "31: .long   ___cwx_img\n"
    "32: .long   0x0600026C\n"
    "33: .long   0x06002270\n"
    "34: .long   0x06000284\n"
    "35: .long   ___cwx_chk\n"
    "36: .long   ___cwx_chimg\n"
    "37: .long   0x40000000\n"
    "___cwx_cell:\n"
    "    .long   _om_cwx_hook\n"             /* *(0x06002270) = &cell, *cell = hook */
    "___cwx_par:\n"
    "    .long   0, 0, 0\n"
    "___cwx_res:\n"
    "    .long   0, 0, 0, 0\n"
    "    .global ___cwx_chk\n"
    "___cwx_chk:\n"
    "    .long   0, 0, 0, 0, 0\n"
    "    .text\n");
