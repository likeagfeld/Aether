/* menu/launcher/igrstub.c -- the in-game reset stub (M6 Task 20a; see igr.h). It runs when the game calls
 * SYS_EXECDMP, long after the launcher, SGL and LWRAM are gone, so everything here is self-contained: one output
 * section (.igrstub) linked at OM_IGR_STUB (launcher.ld), no calls outside it, no static data, no library calls.
 * tools/sbuild.py check_igr audits the linked bytes (every literal and branch target). Sequence = the igr=e round 2
 * sequence proven on the user's console (progress.md "HARDWARE, igr=e round 2"), here as the raw CD-block commands
 * libsatiator sends for each call (menu/lib/satiator/satiator.c, public domain), in the same order:
 *   s_mode(s_api)    E000/0000/00C1/05E7 wait EFLS; 0100 (is_satiator_present: CR3 low byte 2); if present 0400/0001/
 *                    0000/040F (stop the CD "drive")
 *   s_emulate("")    9100/0/0/0 wait EHST, CR1 == 0, wait DRDY, two zero DATATRNS words; AD00/0/0/0 wait MPED; 9000
 *                    (get status; -6 FR_INVALID_NAME is expected and ignored, igr=e)
 *   disc gone        CD periodic status (CR1 high byte, CDC_ST_PERI set) OPEN or NODISC
 *   s_chdir("/")     9100/0/0/1 ..., one DATATRNS word '/' 00 00 00, two zero words; AC00/0/0/1 wait MPED; 9000
 *   s_mode(s_cdrom)  9300/0001/0/0 wait MPED
 *   SYSRES           SMPC 0DH (HWF-53): SF clear, set SF, COMREG = 0x0D, in the window after V-BLANK-IN
 * Fix round 1 (Session H1: IGR returned to Aether on every game, but 12-14 s after the press):
 *  - before the Satiator commands the CD block the game was using is quiesced, as cdblk.c om_cd_reset does before a
 *    boot (pending command, abort file, end transfer, pause, reset selectors);
 *  - every wait has a short cap of its own, inside one 2.5 s budget for everything before SYSRES (superseded by fix
 *    round 2 below), and a poll backstop in case V-blank never toggles (review M1);
 *  - each step paints the back screen its own color, so a slow step can be named on hardware. Work RAM cannot carry
 *    a breadcrumb to the next boot: the BIOS cold start zero-fills HWRAM from 0x06000B00 and all of LWRAM (ROM
 *    0x24C-0x26C, descriptors 0x318/0x320/0x328/0x330) and rewrites 0x06000000-0x06000AFF from ROM;
 *  - SYSRES is retried about once a second (review I2).
 * Fix round 2 (hardware 2026-11-25, Hulk, intermittent: yellow, green, then the BIOS CD player): the shared 2.5 s
 * budget cut every wait after it was spent to one frame, so when the Satiator was slow to stop the emulation,
 * s_chdir and s_mode(s_cdrom) were sent but not waited for and SYSRES landed while the Satiator was still switching.
 * There is no shared budget now: each Satiator step waits for its own answer (1 s for CMOK, 5 s for the flag, 5 s for
 * the disc to go; the H1 build that returned on every game waited 2 s and 10 s, the igr=e spike longer). Every
 * command is sent once, as in both proven sequences. Only the CD-block quiesce keeps its short caps.
 * The game may have left an INTBACK pending (it is broken, SMPC_Manual.txt:2007-2009) and the SCSP playing (master
 * volume 0 first, as the launcher's own exits silence the SCSP before SYSRES, HWF-53). No SD writes (progress.md:
 * IGR must not corrupt SD cards). BSD-3-Clause. */
#include <stdint.h>
#include "igr.h"

#ifdef IGR_HOST_TEST
uint16_t igr_t_rd16(uint32_t a);
void igr_t_wr16(uint32_t a, uint16_t v);
void igr_t_wr32(uint32_t a, uint32_t v);
uint8_t igr_t_rd8(uint32_t a);
void igr_t_wr8(uint32_t a, uint8_t v);
#define R16(a)      igr_t_rd16(a)
#define W16(a, v)   igr_t_wr16((a), (v))
#define W32(a, v)   igr_t_wr32((a), (v))
#define R8(a)       igr_t_rd8(a)
#define W8(a, v)    igr_t_wr8((a), (v))
#define IGR_TEXT
#else
#define R16(a)      (*(volatile uint16_t *)(a))
#define W16(a, v)   (*(volatile uint16_t *)(a) = (v))
#define W32(a, v)   (*(volatile uint32_t *)(a) = (v))
#define R8(a)       (*(volatile uint8_t *)(a))
#define W8(a, v)    (*(volatile uint8_t *)(a) = (v))
#define IGR_TEXT    __attribute__((section(".igrstub"), optimize("Os"), noinline))
#endif

#define HIRQ        0x25890008u        /* CD block (satiator.c) */
#define CR1         0x25890018u
#define CR2         0x2589001Cu
#define CR3         0x25890020u
#define CR4         0x25890024u
#define DATATRNS    0x25818000u
#define CMOK        0x0001u
#define DRDY        0x0002u
#define ESEL        0x0040u
#define EHST        0x0080u
#define EFLS        0x0200u
#define MPED        0x0800u
#define TVMD        0x25F80000u        /* VDP2: DISP = bit 15 */
#define TVSTAT      0x25F80004u        /* bit 3 VBLANK (HWF-10), bit 2 HBLANK */
#define BGON        0x25F80020u
#define BKTAU       0x25F800ACu        /* back screen table address, upper (BKCLMD = bit 15: 0 = one color) */
#define BKTAL       0x25F800AEu
#define CCCTL       0x25F800ECu
#define PRISA       0x25F800F0u        /* sprite priorities PRISA..PRISD: 0 = sprites not shown */
#define CLOFEN      0x25F80110u
#define BKCOLOR     0x25E7FFFEu        /* the last word of VDP2 VRAM: the back screen color */
#define SCSP_MVOL   0x25B00400u        /* SCSP common register: bits 3-0 master volume */
#define SMPC_IREG0  0x20100001u        /* HWF-14 */
#define SMPC_COMREG 0x2010001Fu
#define SMPC_SR     0x20100061u
#define SMPC_SF     0x20100063u

#define CAP_IDLE    10u                /* frames: a command the game left pending */
#define CAP_QUIET   10u                /* each CD-block quiesce command */
#define CAP_CMOK    60u                /* a Satiator command accepted */
#define CAP_FLAG    300u               /* its completion flag (EFLS/EHST/DRDY/MPED) */
#define CAP_GONE    300u               /* disc gone: igr=e measured 3-4 frames on hardware */
#define POLLS       60000u             /* poll backstop per frame of cap */
#define SMPC_FRAMES 10u
#define SYSRES_LINES 6u                /* >= 6 scan lines (~380 us) after V-BLANK-IN: "300 us after" (HWF-53) */
#define LINE_POLLS  0x100000u          /* backstop only: 6 lines are about 380 us */
#define SYSRES_TRIES 5u
#define SYSRES_WAIT 60u                /* frames between SYSRES tries: about 1 s */

/* step colors (RGB555: B<<10 | G<<5 | R), README_H1.txt names them */
#define C_WHITE     0x7FFFu            /* entered; quiescing the CD block */
#define C_RED       0x001Fu            /* Satiator API mode */
#define C_YELLOW    0x03FFu            /* stop the emulation */
#define C_GREEN     0x03E0u            /* waiting for the disc to go */
#define C_CYAN      0x7FE0u            /* chdir "/" */
#define C_BLUE      0x7C00u            /* CD-ROM mode */
#define C_MAGENTA   0x7C1Fu            /* SYSRES issued (stays only if the console does not reset) */

/* The stub's state lives at a fixed address in the ST-040 master-stack range (HWF-20), below the stub's own stack
 * (r15 = 0x06002000), instead of being passed to every helper: the stub must fit the 1,352 B logo area. */
typedef struct { uint32_t frames, fcap; uint16_t prev; } igr_ctx;
#ifdef IGR_HOST_TEST
igr_ctx igr_t_ctx;
#define CTX (&igr_t_ctx)
#else
#define CTX ((igr_ctx *)0x06001000u)
#endif

static IGR_TEXT void color(uint16_t c) { W16(BKCOLOR, c); }

static IGR_TEXT int tick(void)        /* 1 on a V-BLANK-IN edge since the last call; counts it */
{
    igr_ctx *c = CTX;
    uint16_t v = (uint16_t)(R16(TVSTAT) & 8u);
    int e = v && !c->prev;
    c->prev = v;
    if (e) c->frames++;
    return e;
}

/* kind 0: until HIRQ & mask; kind 1: until the CD periodic status says OPEN or NODISC (om_cd_present() == 0).
 * The cap, lim frames, is the step's own (no shared budget). */
static IGR_TEXT int wait_for(int kind, uint16_t mask, uint32_t lim)
{
    igr_ctx *c = CTX;
    uint32_t start = c->frames, n = 0, s;
    for (;;) {
        if (kind == 0) {
            if (R16(HIRQ) & mask) return 0;
        } else if (R16(HIRQ) & CMOK) {             /* no command pending: CR1 holds the periodic report */
            s = (uint32_t)(R16(CR1) >> 8);       /* PERI (0x20) set, code OPEN (6) or NODISC (7); REJECT 0xFF fails */
            if ((s & 0x2Eu) == 0x26u && s != 0xFFu)
                return 0;
        }
        (void)tick();
        if (c->frames - start >= lim || ++n > lim * POLLS)
            return -1;
    }
}

/* satiator.c exec_cmd: -1 = no CMOK, -2 = CMOK but not the wanted flag. The flag cap is CTX->fcap: CAP_QUIET while
 * quiescing the CD block, CAP_FLAG for the Satiator commands. */
static IGR_TEXT int exec(uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4, uint16_t wait)
{
    uint32_t fcap = CTX->fcap;
    W16(HIRQ, (uint16_t)~(CMOK | wait));
    W16(CR1, c1);
    W16(CR2, c2);
    W16(CR3, c3);
    W16(CR4, c4);
    if (wait_for(0, CMOK, fcap < CAP_CMOK ? fcap : CAP_CMOK)) return -1;
    if (wait && wait_for(0, wait, fcap)) return -2;
    return 0;
}

/* satiator.c buffer_write(buf, len) for len 0 or 1..4 (one word) */
static IGR_TEXT int buf_write(uint32_t word, uint16_t len)
{
    if (exec(0x9100u, 0, 0, len, EHST)) return -1;
    if (R16(CR1)) return -1;
    if (wait_for(0, DRDY, CAP_FLAG)) return -1;
    if (len) W32(DATATRNS, word);
    W32(DATATRNS, 0);                  /* "mandatory but mysterious" (satiator.c) */
    W32(DATATRNS, 0);
    return 0;
}

/* satiator.c simplecall(op, 0, 0, len) + get_check_stat */
static IGR_TEXT void call(uint16_t op, uint16_t len)
{
    if (exec((uint16_t)(op << 8), 0, 0, len, MPED) == 0)
        (void)exec(0x9000u, 0, 0, 0, 0);
}

static IGR_TEXT void smpc_idle(void)
{
    igr_ctx *c = CTX;
    uint32_t start = c->frames, n;
    for (n = 0; (R8(SMPC_SF) & 1u) && c->frames - start < SMPC_FRAMES && n < SMPC_FRAMES * POLLS; n++)
        (void)tick();
}

/* SMPC SYSRES (HWF-53), retried: SF clear (bounded), a pending game INTBACK broken (HWF-14: SR bit 5 = NPE), then
 * V-BLANK-IN + 6 lines, SF = 1, COMREG = 0x0D; if the console has not reset about 1 s later, again (review I2: a busy
 * SMPC must not leave the stub spinning). Lines are counted on TVSTAT's HBLANK bit (bit 2), not on VCNT: VCNT only
 * moves when the counters latch (EXLTEN), and on Mednafen 1.32.1 it never moved here, so the VCNT wait ran
 * its full poll cap -- about 4 s (measured). Every loop is bounded by polls as well as by V-blanks. */
static IGR_TEXT void sysres(void)
{
    igr_ctx *c = CTX;
    uint32_t t, n, k, start;
    uint16_t last, v;
    for (t = 0; t < SYSRES_TRIES; t++) {
        smpc_idle();
        if (R8(SMPC_SR) & 0x20u) {
            W8(SMPC_IREG0, 0x40u);
            smpc_idle();
        }
        for (n = 0; n < 20u * POLLS && !tick(); n++)   /* the next V-BLANK-IN */
            ;
        last = 0;                                       /* then 6 HBLANK-IN edges: 6 lines */
        for (n = 0, k = 0; n < LINE_POLLS && k < SYSRES_LINES; n++) {
            v = (uint16_t)(R16(TVSTAT) & 4u);
            if (v && !last) k++;
            last = v;
        }
        W8(SMPC_SF, 1u);
        W8(SMPC_COMREG, 0x0Du);        /* SYSRES */
        color(C_MAGENTA);
        for (start = c->frames, n = 0; c->frames - start < SYSRES_WAIT && n < SYSRES_WAIT * POLLS; n++)
            (void)tick();
    }
#ifndef IGR_HOST_TEST
    for (;;)                           /* five SYSRES did not reset: magenta stays; the user presses Reset */
        ;
#endif
}

/* Only the back screen: every NBG/RBG off, sprites at priority 0, no color calculation or offset, display on */
static IGR_TEXT void screen(void)
{
    W32(BKTAU, (BKCOLOR - 0x25E00000u) >> 1);  /* BKTAU:BKTAL as one long: word address 0x3FFFF, BKCLMD 0 */
    color(C_WHITE);
    W16(BGON, 0);
    W32(PRISA, 0);                     /* PRISA, PRISB */
    W32(PRISA + 4u, 0);                /* PRISC, PRISD */
    W16(CCCTL, 0);
    W16(CLOFEN, 0);
    W16(TVMD, (uint16_t)(R16(TVMD) | 0x8000u));
}

IGR_TEXT void om_igr_run(void)
{
    igr_ctx *c = CTX;
    c->frames = 0;
    c->fcap = CAP_QUIET;
    c->prev = (uint16_t)(R16(TVSTAT) & 8u);
    W16(SCSP_MVOL, (uint16_t)(R16(SCSP_MVOL) & ~0x000Fu));
    screen();
    (void)wait_for(0, CMOK, CAP_IDLE);                            /* a command the game left pending */
    (void)exec(0x7500u, 0, 0, 0, EFLS);                /* Abort File */
    (void)exec(0x0600u, 0, 0, 0, 0);                   /* End Data Transfer */
    (void)exec(0x11FFu, 0xFFFFu, 0x00FFu, 0xFFFFu, 0); /* Seek to FAD 0xFFFFFF = pause (CDDA) */
    (void)exec(0x48FCu, 0, 0, 0, ESEL);                /* Reset Selector, all */
    color(C_RED);
    c->fcap = CAP_FLAG;
    if (exec(0xE000u, 0x0000u, 0x00C1u, 0x05E7u, EFLS) != -1) {   /* -1: no Satiator answered */
        if (exec(0x0100u, 0, 0, 0, 0) == 0 && (R16(CR3) & 0xFFu) == 2u)
            (void)exec(0x0400u, 0x0001u, 0x0000u, 0x040Fu, 0);
        color(C_YELLOW);
        if (buf_write(0, 0) == 0)
            call(0xADu, 0);        /* s_emulate("") */
        color(C_GREEN);
        (void)wait_for(1, 0, CAP_GONE);
        color(C_CYAN);
        if (buf_write(0x2F000000u, 1) == 0)
            call(0xACu, 1);        /* s_chdir("/") */
        color(C_BLUE);
        (void)exec(0x9300u, 0x0001u, 0, 0, MPED);       /* s_mode(s_cdrom), its MPED waited for up to 5 s */
    }
    sysres();
}

#ifndef IGR_HOST_TEST
/* Entry, the value written to 0x0600026C. The game calls it with `jsr` (the guard enters with jmp: pr is still the
 * game's). A lid exit -- the CD block's DCHG set (HIRQ bit 5) -- goes, untouched, to the BIOS's own SYS_EXECDMP when
 * the launcher recorded it (JHL only; igr.h om_igr_bios_execdmp_of): the BIOS CD player, which boots the disc the
 * Satiator presents on the lid's close, exactly as with no in-game reset (Enemy Zero's disc prompt, hardware
 * 2026-11-25; the official Satiator menu's behaviour). Otherwise: mask every interrupt level, take a stack in the
 * ST-040 master-stack range below the IP (HWF-20; the game is finished), and run. r0-r2 are the caller's scratch. */
__asm__(
    "    .section .igrstub,\"ax\",@progbits\n"
    "    .global _om_igr_entry\n"
    "    .global _om_igr_bios_execdmp\n"
    "    .align 2\n"
    "_om_igr_entry:\n"
    "    mov.l   4f,r0\n"                    /* the BIOS's SYS_EXECDMP, 0 = none (CWX, or not recorded) */
    "    tst     r0,r0\n"
    "    bt      6f\n"
    "    mov.l   5f,r1\n"
    "    mov.w   @r1,r1\n"                   /* HIRQ */
    "    mov     #32,r2\n"
    "    tst     r2,r1\n"                    /* DCHG? */
    "    bt      6f\n"
    "    jmp     @r0\n"                      /* the lid: the BIOS, as if the reset were not armed */
    "    nop\n"
    "6:  stc     sr,r0\n"
    "    or      #0xF0,r0\n"
    "    ldc     r0,sr\n"
    "    mov.l   1f,r15\n"
    "    mov.l   2f,r0\n"
    "    jsr     @r0\n"
    "    nop\n"
    "3:  bra     3b\n"
    "    nop\n"
    "    .align 2\n"
    "1:  .long   0x06002000\n"
    "2:  .long   _om_igr_run\n"
    "5:  .long   0x25890008\n"
    "_om_igr_bios_execdmp:\n"
    "4:  .long   0\n"                        /* om_igr_w_arm writes it in the staged image, before the seal */
    "    .text\n");
#endif
