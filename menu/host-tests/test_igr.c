/* menu/host-tests/test_igr.c -- the in-game reset stub (menu/launcher/igrstub.c, built with
 * IGR_HOST_TEST against the fake register bank below) quiesces the CD block, then sends exactly the igr=e round 2
 * sequence libsatiator sends (progress.md "HARDWARE, igr=e round 2"); every wait is bounded and the whole run before
 * SYSRES has its own cap (fix round 2: no shared budget; a slow Satiator stop no longer cuts CD-ROM mode short, Hulk
 * 2026-11-25); SYSRES is retried; each step paints its back-screen color; and the IP
 * check (igrcheck.c) arms only standard IPs whose security code calls through 0x06000284. BSD-3-Clause. */
#include "th.h"
#include "igr.h"

/* --- fake hardware ------------------------------------------------------------------------------------------ */
#define MAXLOG 128
static struct {
    int satiator;             /* 0: nothing answers (no CMOK ever) */
    int no_flags;             /* 1: CMOK but never the completion flag */
    int gone_after;           /* periodic NODISC after this many CR1 reads in the wait (-1: never) */
    int sr_pending;           /* SMPC SR bit 5 set (a game INTBACK still has data) */
    uint16_t hirq, cr[4], cr1_resp, scsp, tvmd, exten;
    int periodic, cr1_reads;
    uint32_t tv_reads;
    uint16_t cmd[MAXLOG][4], hclr[MAXLOG]; int ncmd;
    uint32_t dt[MAXLOG]; int ndt;
    uint8_t smpc_w[MAXLOG][2]; int nsmpc;     /* (register offset, value) */
    uint16_t colors[MAXLOG]; int ncolor;
    uint8_t sf;
    uint32_t tv_at_sysres[16]; int nsysres;
    uint16_t last_hclr, bgon, bktau, bktal;
    int pris_cleared;
    uint32_t slow[256];       /* frames before an op's completion flag (CMOK at once): a busy Satiator */
    uint16_t flag_pending; uint32_t flag_at; uint8_t flag_op;
    uint32_t cdrom_done;      /* TVSTAT reads when s_mode(s_cdrom)'s MPED was raised (0: never) */
} F;

static void reset_fake(int satiator, int gone_after, int sr_pending)
{
    memset(&F, 0, sizeof F);
    F.satiator = satiator; F.gone_after = gone_after; F.sr_pending = sr_pending;
    F.scsp = 0x020F;
    F.hirq = satiator ? 0x0001u : 0;          /* the game left the CD block idle (CMOK set) */
    F.tvmd = 0x0001; F.exten = 0x0200; F.bgon = 0x001F;
}

#define TV_HALF 40u                           /* TVSTAT reads per half frame: one frame = 80 reads */
#define FRAME (2u * TV_HALF)
static uint16_t tvstat(void)                  /* VBLANK (bit 3) by half frames; HBLANK (bit 2) toggles every read */
{
    F.tv_reads++;
    return (uint16_t)((((F.tv_reads / TV_HALF) & 1u) ? 8u : 0u) | ((F.tv_reads & 1u) ? 4u : 0u));
}
static uint32_t frames_now(void) { return F.tv_reads / FRAME; }

uint16_t igr_t_rd16(uint32_t a)
{
    switch (a) {
    case 0x25890008u:
        if (F.flag_pending && F.tv_reads >= F.flag_at) {          /* a slow command completes */
            F.hirq |= F.flag_pending;
            F.flag_pending = 0;
            if (F.flag_op == 0x93) F.cdrom_done = F.tv_reads;
        }
        return F.hirq;
    case 0x25890018u:
        if (F.periodic) {
            F.cr1_reads++;
            return (F.gone_after >= 0 && F.cr1_reads > F.gone_after)
                       ? 0x2700u : 0x2101u;                       /* PERI|NODISC : PERI|PAUSE */
        }
        return F.cr1_resp;
    case 0x2589001Cu: return F.cr[1];
    case 0x25890020u: return F.cr[2];
    case 0x25890024u: return F.cr[3];
    case 0x25F80000u: return F.tvmd;
    case 0x25F80004u: return tvstat();
    case 0x25B00400u: return F.scsp;
    }
    printf("FAIL unexpected rd16 %08x\n", a); exit(2);
}

void igr_t_wr16(uint32_t a, uint16_t v)
{
    uint16_t op;
    switch (a) {
    case 0x25890008u: F.hirq &= v; F.last_hclr = v; return;   /* write 0 clears a bit */
    case 0x25890018u: F.cr[0] = v; return;
    case 0x2589001Cu: F.cr[1] = v; return;
    case 0x25890020u: F.cr[2] = v; return;
    case 0x25890024u:
        F.cr[3] = v;
        if (F.ncmd < MAXLOG) {
            memcpy(F.cmd[F.ncmd], F.cr, sizeof F.cr);
            F.hclr[F.ncmd] = F.last_hclr;
            F.ncmd++;
        }
        F.periodic = 0;
        F.cr1_resp = 0;
        F.flag_pending = 0;                                     /* a new command replaces a slow one */
        if (!F.satiator) return;
        op = (uint16_t)(F.cr[0] >> 8);
        F.hirq |= 0x0001u;                                      /* CMOK */
        if (op == 0x01) F.cr[2] = 0x0002u;                      /* MPEG version 2: a Satiator */
        if (F.no_flags) return;
        {
            uint16_t fl = 0;
            if (op == 0xE0 || op == 0x75) fl = 0x0200u;         /* EFLS */
            if (op == 0x48) fl = 0x0040u;                       /* ESEL */
            if (op == 0x91) fl = 0x0080u | 0x0002u;             /* EHST, DRDY */
            if (op == 0xAC || op == 0xAD || op == 0x93) fl = 0x0800u;   /* MPED */
            if (fl && F.slow[op]) {
                F.flag_pending = fl; F.flag_op = (uint8_t)op; F.flag_at = F.tv_reads + F.slow[op] * 80u;
            } else {
                F.hirq |= fl;
                if (op == 0x93) F.cdrom_done = F.tv_reads;
            }
        }
        if (op == 0x90 && F.ncmd >= 2 && (F.cmd[F.ncmd - 2][0] >> 8) == 0xAD) {
            F.cr1_resp = 0x0600u;                               /* emulate("") -> -6 FR_INVALID_NAME */
            F.periodic = 1;                                     /* then the drive reports on its own */
            F.cr1_reads = 0;
        }
        return;
    case 0x25F80000u: F.tvmd = v; return;
    case 0x25F80020u: F.bgon = v; return;
    case 0x25F800ACu: F.bktau = v; return;
    case 0x25F800AEu: F.bktal = v; return;
    case 0x25F800ECu: case 0x25F800F0u: case 0x25F800F2u: case 0x25F800F4u: case 0x25F800F6u: case 0x25F80110u:
        if (v) { printf("FAIL nonzero write %04x to %08x\n", v, a); exit(2); }
        return;
    case 0x25E7FFFEu: if (F.ncolor < MAXLOG) F.colors[F.ncolor++] = v; return;
    case 0x25B00400u: F.scsp = v; return;
    }
    printf("FAIL unexpected wr16 %08x\n", a); exit(2);
}

void igr_t_wr32(uint32_t a, uint32_t v)
{
    if (a == 0x25F800ACu) { F.bktau = (uint16_t)(v >> 16); F.bktal = (uint16_t)v; return; }
    if (a == 0x25F800F0u || a == 0x25F800F4u) {
        if (v) { printf("FAIL nonzero sprite priority %08x\n", v); exit(2); }
        F.pris_cleared++;
        return;
    }
    if (a != 0x25818000u) { printf("FAIL unexpected wr32 %08x\n", a); exit(2); }
    if (F.ndt < MAXLOG) F.dt[F.ndt++] = v;
}

uint8_t igr_t_rd8(uint32_t a)
{
    if (a == 0x20100063u) { uint8_t s = F.sf; if (F.sf) F.sf--; return s ? 1 : 0; }  /* busy for a few reads */
    if (a == 0x20100061u) return F.sr_pending ? 0x20u : 0;
    printf("FAIL unexpected rd8 %08x\n", a); exit(2);
}

void igr_t_wr8(uint32_t a, uint8_t v)
{
    if (a != 0x20100001u && a != 0x2010001Fu && a != 0x20100063u) { printf("FAIL unexpected wr8 %08x\n", a); exit(2); }
    if (a == 0x20100001u && v == 0x40u) F.sr_pending = 0;
    if (a == 0x2010001Fu && v == 0x0Du && F.nsysres < 16) F.tv_at_sysres[F.nsysres++] = F.tv_reads;
    if (F.nsmpc < MAXLOG) { F.smpc_w[F.nsmpc][0] = (uint8_t)(a & 0xFF); F.smpc_w[F.nsmpc][1] = v; F.nsmpc++; }
}

/* --- checks ------------------------------------------------------------------------------------------------- */
static const uint16_t QUIET[][5] = {      /* CR1..CR4, the HIRQ clear word ~(CMOK | wait) */
    { 0x7500, 0, 0, 0, 0xFDFE },                  /* Abort File, wait EFLS */
    { 0x0600, 0, 0, 0, 0xFFFE },                  /* End Data Transfer */
    { 0x11FF, 0xFFFF, 0x00FF, 0xFFFF, 0xFFFE },   /* Seek FAD 0xFFFFFF: pause */
    { 0x48FC, 0, 0, 0, 0xFFBE },                  /* Reset Selector all, wait ESEL */
};
#define NQUIET 4
static const uint16_t WANT[][5] = {
    { 0xE000, 0x0000, 0x00C1, 0x05E7, 0xFDFE },   /* s_mode(s_api): wait EFLS */
    { 0x0100, 0, 0, 0, 0xFFFE },                  /* is_satiator_present */
    { 0x0400, 0x0001, 0x0000, 0x040F, 0xFFFE },   /* stop the CD "drive" */
    { 0x9100, 0, 0, 0, 0xFF7E },                  /* s_emulate(""): buffer_write, len 0, wait EHST */
    { 0xAD00, 0, 0, 0, 0xF7FE },                  /* c_emulate, wait MPED */
    { 0x9000, 0, 0, 0, 0xFFFE },                  /* get status */
    { 0x9100, 0, 0, 1, 0xFF7E },                  /* s_chdir("/"): buffer_write, len 1 */
    { 0xAC00, 0, 0, 1, 0xF7FE },                  /* c_chdir */
    { 0x9000, 0, 0, 0, 0xFFFE },
    { 0x9300, 0x0001, 0, 0, 0xF7FE },             /* s_mode(s_cdrom), wait MPED */
};
#define NWANT ((int)(sizeof WANT / sizeof WANT[0]))

static void check_cmds(const uint16_t (*w)[5], int from, int n)
{
    int i, k;
    for (i = 0; i < n && from + i < F.ncmd; i++) {
        for (k = 0; k < 4; k++) CHECK_INT(F.cmd[from + i][k], w[i][k]);
        CHECK_INT(F.hclr[from + i], w[i][4]);
    }
}

static void check_sequence(void)
{
    CHECK_INT(F.ncmd, NQUIET + NWANT);
    check_cmds(QUIET, 0, NQUIET);                 /* the CD block is quiesced before any Satiator command */
    check_cmds(WANT, NQUIET, NWANT);
    CHECK_INT(F.ndt, 5);                            /* "": two zero words; "/": '/',0,0,0 then two zero words */
    CHECK_INT(F.dt[0], 0); CHECK_INT(F.dt[1], 0);
    CHECK_INT(F.dt[2], 0x2F000000u); CHECK_INT(F.dt[3], 0); CHECK_INT(F.dt[4], 0);
}

static void check_screen(int full)
{
    static const uint16_t seq[] = { 0x7FFF, 0x001F, 0x03FF, 0x03E0, 0x7FE0, 0x7C00 };
    int i, n = full ? 6 : 2;
    CHECK_INT(F.bgon, 0);
    CHECK_INT(F.pris_cleared, 2);                                 /* PRISA-PRISD = 0: no sprites */
    CHECK_INT(F.bktau, 3); CHECK_INT(F.bktal, 0xFFFF);           /* back color at VRAM 0x7FFFE, one color */
    CHECK_INT(F.tvmd & 0x8000u, 0x8000u);
    CHECK_INT(F.tvmd & 0x7FFFu, 0x0001u);
    CHECK(F.ncolor >= n + 1);
    for (i = 0; i < n && i < F.ncolor; i++) CHECK_INT(F.colors[i], seq[i]);
    CHECK_INT(F.colors[F.ncolor - 1], 0x7C1F);                    /* magenta: SYSRES issued */
}

static void check_sysres(int want_break)
{
    int i, n = F.nsmpc, base = want_break ? 1 : 0;
    CHECK_INT(F.nsysres, 5);                        /* the fake never resets: five tries, then the stub gives up */
    CHECK_INT(n, base + 10);
    if (want_break) { CHECK_INT(F.smpc_w[0][0], 0x01); CHECK_INT(F.smpc_w[0][1], 0x40); }  /* IREG0 = break */
    for (i = base; i + 1 < n; i += 2) {
        CHECK_INT(F.smpc_w[i][0], 0x63); CHECK_INT(F.smpc_w[i][1], 1);          /* SF = 1 */
        CHECK_INT(F.smpc_w[i + 1][0], 0x1F); CHECK_INT(F.smpc_w[i + 1][1], 0x0D); /* COMREG = SYSRES */
    }
    for (i = 0; i < F.nsysres; i++) {
        uint32_t ph = F.tv_at_sysres[i] % FRAME;
        CHECK(ph >= TV_HALF + 11u);          /* inside V-blank, 6 HBLANK edges (2 reads each) past its start */
        if (i) CHECK((F.tv_at_sysres[i] - F.tv_at_sysres[i - 1]) / FRAME >= 59u);  /* about 1 s apart */
    }
    CHECK_INT(F.scsp & 0x000Fu, 0);                 /* master volume 0 */
    CHECK_INT(F.scsp & 0xFFF0u, 0x0200u);           /* the rest of the register kept */
}

static uint32_t frames_to_sysres(void) { return F.nsysres ? F.tv_at_sysres[0] / FRAME : 99999u; }

/* s_mode(s_cdrom) completed (its MPED raised) before the first SYSRES: the reset never lands while the Satiator is
 * still switching (Hulk, 2026-11-25: yellow, green, then the BIOS CD player) */
static void check_cdrom_before_sysres(void)
{
    CHECK(F.cdrom_done != 0);
    CHECK(F.nsysres > 0 && F.cdrom_done <= F.tv_at_sysres[0]);
}

static int count_op(uint8_t op)
{
    int i, n = 0;
    for (i = 0; i < F.ncmd; i++) n += (F.cmd[i][0] >> 8) == op;
    return n;
}

int main(void)
{
    uint8_t ip[0x1000];

    reset_fake(1, 5, 0);                            /* the hardware case: the disc leaves after 5 status reads */
    om_igr_run();
    check_sequence();
    check_screen(1);
    check_sysres(0);
    CHECK(frames_to_sysres() <= 5u);                /* nothing waits to a cap: SYSRES within a few frames */

    /* Hulk (2026-11-25, intermittent): a busy Satiator -- the stop's MPED after 200 frames, the disc gone 20 frames
     * later, CD-ROM mode's MPED after 200. Each Satiator step waits for its own answer, with no shared budget cutting
     * CD-ROM mode to a frame: SYSRES only after s_mode(s_cdrom) completed. */
    reset_fake(1, 20 * 80, 0);
    F.slow[0xAD] = 200; F.slow[0x93] = 200;
    om_igr_run();
    check_sequence();
    check_screen(1);
    check_sysres(0);
    check_cdrom_before_sysres();
    CHECK(frames_to_sysres() >= 420u && frames_to_sysres() <= 430u);

    reset_fake(1, -1, 1);                           /* the disc never reports gone; a game INTBACK was pending */
    F.sf = 3;
    om_igr_run();
    check_sequence();                               /* the stop is sent once, then chdir and CD-ROM mode */
    check_screen(1);
    check_cdrom_before_sysres();
    check_sysres(1);
    CHECK(frames_to_sysres() >= 300u && frames_to_sysres() <= 306u);   /* the disc-gone cap, 5 s, then on */

    reset_fake(1, 5, 0);                            /* CMOK but no completion flags: every flag wait to its cap */
    F.no_flags = 1;
    om_igr_run();
    CHECK(F.ncmd >= NQUIET + 3);
    CHECK_INT(count_op(0x93), 1);                   /* accepted (CMOK) but never done: not sent again */
    /* quiesce 20 + API 300 + stop (EHST 300) + disc gone 300 + chdir (EHST 300) + CD-ROM mode 300: bounded */
    CHECK(frames_to_sysres() >= 1515u && frames_to_sysres() <= 1540u);
    CHECK_INT(F.nsysres, 5);

    reset_fake(1, 5, 0);                            /* the game left a command pending that never completes */
    F.hirq = 0;
    F.no_flags = 1;
    om_igr_run();
    CHECK(frames_to_sysres() <= 1560u);
    CHECK_INT(F.nsysres, 5);

    reset_fake(0, -1, 0);                           /* no Satiator, no CD answer at all: still SYSRES */
    om_igr_run();
    CHECK_INT(F.ncmd, NQUIET + 1);
    CHECK_INT(F.cmd[NQUIET][0], 0xE000);
    check_screen(0);
    check_sysres(0);
    CHECK(frames_to_sysres() <= 115u);              /* idle 10 + 4 x 10 + E000 60 */

    /* the BIOS's own SYS_EXECDMP, recorded for a lid exit only when it points into the BIOS ROM */
    CHECK_INT(om_igr_bios_execdmp_of(0x0000186Cu), 0x0000186Cu);   /* BIOS 1.01 */
    CHECK_INT(om_igr_bios_execdmp_of(0x0007FFFEu), 0x0007FFFEu);
    CHECK_INT(om_igr_bios_execdmp_of(0), 0);
    CHECK_INT(om_igr_bios_execdmp_of(0x000003FEu), 0);              /* the vector table */
    CHECK_INT(om_igr_bios_execdmp_of(0x00080000u), 0);
    CHECK_INT(om_igr_bios_execdmp_of(OM_IGR_GUARD), 0);             /* ours, never re-entered */
    CHECK_INT(om_igr_bios_execdmp_of(OM_IGR_STUB), 0);

    /* IP check */
    memset(ip, 0, sizeof ip);
    memcpy(ip, "SEGA SEGASATURN ", 16);
    ip[0xE0] = 0x00; ip[0xE1] = 0x00; ip[0xE2] = 0x10; ip[0xE3] = 0x00;           /* IP size 0x1000 */
    ip[0x270] = 0x06; ip[0x271] = 0x00; ip[0x272] = 0x02; ip[0x273] = 0x84;       /* security code literal */
    CHECK_INT(om_igr_ip_check(ip, 0x400), OM_IGR_OK);
    CHECK_INT(om_igr_ip_check(ip, (OM_IGR_STAGE_END - OM_IGR_STAGE) + 1), OM_IGR_E_IPSIZE);
    ip[0xE2] = 0x0D; ip[0xE3] = 0x00;                                             /* 0xD00 < 0xD48: stub outside */
    CHECK_INT(om_igr_ip_check(ip, 0x400), OM_IGR_E_IPSIZE);
    ip[0xE2] = 0x78; ip[0xE3] = 0x00;                                             /* 0x7800: ends at the staging */
    CHECK_INT(om_igr_ip_check(ip, 0x400), OM_IGR_OK);
    ip[0xE2] = 0x80;                                                               /* 0x8000 (the maximum): overlaps */
    CHECK_INT(om_igr_ip_check(ip, 0x400), OM_IGR_E_IPSIZE);
    /* B4 (Girls in Motion Puzzle Vol. 1, IP size 0x8000): CWX stages the image in LWRAM (stage_bytes 0), so only the
     * stub's bounds apply and the in-game reset is armed */
    CHECK_INT(om_igr_ip_check(ip, 0), OM_IGR_OK);
    ip[0xE2] = 0x80; ip[0xE3] = 0x04;                                             /* past 8000H: never */
    CHECK_INT(om_igr_ip_check(ip, 0), OM_IGR_E_IPSIZE);
    ip[0xE2] = 0x0D; ip[0xE3] = 0x00;
    CHECK_INT(om_igr_ip_check(ip, 0), OM_IGR_E_IPSIZE);
    ip[0xE3] = 0x00;
    ip[0xE2] = 0x10;
    ip[0x273] = 0x88;
    CHECK_INT(om_igr_ip_check(ip, 0x400), OM_IGR_E_SECWORD);
    ip[0x273] = 0x84;
    ip[0] = 'X';
    CHECK_INT(om_igr_ip_check(ip, 0x400), OM_IGR_E_HEADER);
    CHECK_INT(om_igr_ip_check(0, 0x400), OM_IGR_E_HEADER);
    CHECK_STR(om_igr_strerror(OM_IGR_OK), "ON");
    CHECK_STR(om_igr_strerror(OM_IGR_E_SECWORD), "OFF (security code)");
    (void)frames_now;
    return th_done("test_igr");
}
