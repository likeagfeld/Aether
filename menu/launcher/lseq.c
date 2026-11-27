/* menu/launcher/lseq.c -- see lseq.h. New code; behaviour reference satiator-menu@75bf6e5 main.c:144-201 and
 * init.c:16-39. Step order mirrors main.c:160-178 exactly:
 *   1 s_mode(s_api)  2 s_chdir  3 image2desc (main.c:160)
 *   4 interrupt reset + cart soft-reset hook, in API mode, before s_emulate (main.c:169-171)
 *   5 VDP1 snapshot restore (main.c:173)  6 s_emulate (main.c:175)  7 present -> absent -> present (main.c:176-177)
 *   8 s_mode(s_cdrom) (main.c:178)  9 region-free boot (jhboot.c; main.c:179)
 * On any failure from step 6 on: s_emulate("") (and s_mode(s_api) once in CD-ROM mode), as main.c:181-182.
 *
 * Interrupt reset mirrors the official menu's own start-up reset (init.c:17-25, 35) through the BIOS services
 * (SEGA_SYS.H:109-128), with the ranges ST-162-R1 p.13 §2.2 allows: SYS_SETSINT takes vectors 0..7FH and writes
 * VBR + 4n; SYS_SETUINT takes only 40H..4DH and 50H..5FH (HWF-26). Because SYS_SETSINT registers relative to the
 * current VBR, step 4 switches VBR to the value found at entry (the BIOS's: the menu never loads VBR, see lvec.h),
 * resets, runs the cart hook there, and only then switches back to the launcher's table. The upstream menu does the
 * reset at its start (init.c) and the hook at launch (main.c:169-171); here both happen at launch, in that order.
 * The BIOS gets the machine in om_boot_handover, just before the first loadcd_boot call: VBR = entry value and
 * SR I3-I0 = 7, the level the official menu runs at from init.c:37-38 on (nothing in main.c or jhloader.c changes
 * it before bios_loadcd_boot, jhloader.c:132).
 *
 * unless the hand-off sets OM_HO_NO_IGR, step 9 arms the in-game reset (igr.h): the game's own
 * A+B+C+Start exit returns to Aether through a stub that stops the Satiator's emulation and resets the console --
 * the sequence M6 Task 1's igr=e spike proved on hardware (progress.md "HARDWARE, igr=e round 2"). Task 1's
 * SPIKE.INI variants (igr=d/e/f) are retired from this file; their record is docs/spikes/S5-igr-resident.md and
 * git history.
 *
 * the hand-off block's `loader` picks step 9's boot -- JHL, om_boot_disc (above, unchanged) or CWX,
 * om_cwx_boot (cwxboot.h: Pseudo Saturn Kai's self-loader) -- and its `igr` byte (om_handoff_fill2
 * folds OM_HO_NO_IGR into it) says whether to arm the in-game reset, under either loader.
 *
 * Z held while step 1 shows lists the internal backup RAM (bramtest.h), read only, before s_mode; the L1
 * harness's OM_HO_TEST_BRAM runs the same listing with its emulator round trip.
 *
 * after the save swap, still in API mode, the game's CHEATS.PAK record becomes Kai's
 * plan (lcheat.h, chkai.h), shown on row 13 and armed by the CWX boot; OM_HO_CHEATS runs the cheat editor instead.
 *
 * after step 3, still in API mode, the per-game-save swap (pgsrun.h) -- the journal resumed first, then this
 * launch's plan; its status line is row 14. A failure stops the launch at step 3 with the op and the reason; nothing
 * of the swap runs inside the in-game reset. BSD-3-Clause. */
#include <string.h>
#include "sega_sys.h"
#include "satiator.h"
#include "cdparse.h"
#include "handoff.h"
#include "jhboot.h"
#include "cdblk.h"
#include "lseq.h"
#include "lvec.h"
#include "omlayout.h"
#include "vdp2txt.h"
#include "igr.h"
#include "cwxboot.h"
#include "settings.h"
#include "smpcpad.h"
#include "bramtest.h"
#include <stdlib.h>
#include "lgap.h"
#include "pgsrun.h"
#include "pgsbak.h"
#include "sshoff.h"
#include "msgfmt.h"
#include "lcheat.h"
#include "ldesc.h"
#include "lgfx.h"
#include "yield.h"
#include "lscreen.h"
#include "lsaves.h"
#include "lorb.h"
#include "lorbpage.h"
#ifdef OM_L1_HARNESS
#include "l1gate.h"
#endif

#define SWAP_FRAMES 600                                           /* each half of the disc-swap wait: 10 s (HWF-11) */
#define FRT_TIER (*(volatile uint8_t *)0xFFFFFE10u)                /* SH7604_Hardware.txt:12775 */

static uint32_t s_arg[76];                                        /* 4-aligned names for s_* (satiator.c:114-125) */
static volatile int s_step;
static om_chk_plan s_chk;                                         /* this launch's cheats */
static char s_desc[12] = "out.desc";                              /* step 6's file (ldesc.h: out<cur>.desc for a set) */
static char s_dir[256];                                           /* ...and its folder (ldesc.h), re-entered after the swap */
static uint8_t s_ret;                                             /* om_run_return: the menu disc, back from a tool */

static const char *al(const char *s)
{
    size_t n = strlen(s);
    if (n > 255) n = 255;
    memcpy(s_arg, s, n);
    ((char *)s_arg)[n] = 0;
    return (const char *)s_arg;
}

static void set_sr_mask(uint32_t level)
{
    uint32_t sr;
    __asm__ volatile ("stc sr,%0" : "=r"(sr));
    sr = (sr & ~0xF0u) | ((level & 0xFu) << 4);                   /* I3-I0 = bits 7-4 */
    __asm__ volatile ("ldc %0,sr" : : "r"(sr) : "memory");
}

int om_lseq_step(void) { return s_step; }

#ifdef OM_L1_HARNESS
/* l1gate.h: the slave-SH-2 gate's two halt points, L1 harness launcher only (sbuild L1_ONLY_SYMS). */
#define L1G ((volatile uint32_t *)OM_CT(OM_L1G_BASE))
void om_l1g_pre(void)
{
    uint32_t f;
    if (L1G[0] != OM_L1G_REQ) return;
    L1G[1] = OM_L1G_PRE;
    om_txt_puts(0, 27, "L1 SLAVE GATE: phase 1 (menu side)", 1);
    for (f = 0; f < OM_L1G_FRAMES; f++)
        om_txt_wait_vblank();
}
void om_l1g_halt(void)
{
    if (L1G[0] != OM_L1G_REQ) return;
    L1G[1] = OM_L1G_HALT;
    om_txt_puts(0, 27, "L1 SLAVE GATE: halted before step 9", 1);
    for (;;)
        om_txt_wait_vblank();
}
#endif

void om_restore_vdp1(void)
{
    volatile uint16_t *dst = (volatile uint16_t *)OM_CT(OM_VDP1_VRAM);   /* HWF-12, HWF-23 */
    volatile uint16_t *src = (volatile uint16_t *)OM_CT(OM_LW_SNAPSHOT);
    uint32_t i;
    for (i = 0; i < OM_VDP1_BYTES / 2; i++)
        dst[i] = src[i];
}

void om_reset_interrupts(void)                                    /* caller has VBR at its entry value */
{
    uint32_t i;
    for (i = 0; i < 0x80; i++)
        SYS_SETSINT(i, 0);                                        /* upstream bios_set_sh2_interrupt(0..7F) */
    for (i = 0x40; i < 0x60; i++)
        if (i != 0x4E && i != 0x4F)                               /* ST-162-R1 p.13: not 4EH/4FH */
            SYS_SETUINT(i, 0);                                    /* upstream bios_set_scu_interrupt(40..5F) */
    SYS_CHGSCUIM(0, 0);
    SYS_CHGSCUIM(0xFFFFFFFFu, 0xFFFFFFFFu);
    FRT_TIER = 0x01;      /* init.c:35 "stop the FRT" (TIER = 0); bit 0 must be written 1 (SH7604_Hardware.txt:12925) */
}

void om_boot_handover(void)
{
    om_vec_to_entry();
    set_sr_mask(7);
}

/* the launch's steps on the loading screen's look (lgfx.h) while it is up: what is happening, in words; else the
 * text screen's numbered rows */
static const char *const k_say[10] = { "", "Preparing", "Preparing", "Reading the disc image", "Starting the game",
                                       "Starting the game", "Starting the game", "Changing the disc",
                                       "Starting the game", "Starting the game" };
/* The launch's eye between two whole Satiator calls (yield.h; hardware 2026-11-27, Aether 0.1: "slow or doesn't even
 * start until the game has started loading a bit, then slowly starts and then jumps to full speed right at the last
 * half second" -- the pump ran only at the steps and the swap's progress, and the swap's commands, ~29 ms each on the
 * user's card, ran back to back between them; test_lgpace.c). Here in LWRAM, so a stale pointer never calls into
 * .lui: each checks the screens are up and .lui is still in High Work RAM, and never re-enters the pump (its own reads
 * are libsatiator calls). Set while the launch's loading screen is up (screens_on), cleared at step 4 */
static uint8_t s_pacing;
static void pace(void)
{   /* a file open for write (yield.h om_yield_wr: the swap's writes, the descriptor): a frame only, no LOADBG read */
    if (!om_lg_on || !om_lui_ok || s_pacing) return;
    s_pacing = 1;
    if (om_yield_wr) om_lg_pump(0);
    else om_lg_pace();
    s_pacing = 0;
}
static void pace_draw(void)
{   /* inside a directory listing: a frame, no SD command */
    if (!om_lg_on || !om_lui_ok || s_pacing) return;
    s_pacing = 1;
    om_lg_pump(0);
    s_pacing = 0;
}
static void pace_on(int on)
{
    om_yield_fn = on ? pace : 0;
    om_yield_draw_fn = on ? pace_draw : 0;
}

static void step(int n, const char *what)
{
    s_step = n;
    if (om_lg_on) {
        om_ls_line(0, s_ret ? "Returning to Aether" : k_say[n < 10 ? n : 9], om_lg_c.txt);
        pace();
        return;
    }
    om_txt_puts(0, 3 + n, "step", 0);
    om_txt_putn(5, 3 + n, n, 0);
    om_txt_puts(8, 3 + n, what, 0);
}

/* the game's name for the title: its image's file name without the extension */
static void title_of(const char *image, char *out, uint32_t cap)
{
    uint32_t n;
    if (strrchr(image, '/')) image = strrchr(image, '/') + 1;     /* a set's image is "<disc folder>/<file>" */
    n = (uint32_t)strlen(image);
    const char *dot = strrchr(image, '.');
    if (dot && dot != image) n = (uint32_t)(dot - image);
    if (n >= cap) n = cap - 1;
    memcpy(out, image, n);
    out[n] = 0;
}

/* a notice for ~3 s: the screens' warning lines, else rows y, y + 1 of the text screen */
static void notice(const char *s1, const char *s2, int y)
{
    int f;
    if (om_lg_on) {
        om_ls_line(2, s1, om_lg_c.err);
        om_ls_line(3, s2, om_lg_c.err);
    } else {
        om_txt_puts(0, y, s1, 2);
        if (s2) om_txt_puts(0, y + 1, s2, 2);
    }
    for (f = 0; f < 180; f++) {
        om_lgap();                                                /* one V-blank each, bounded (lgap.h) */
        if (om_lg_on) om_lg_pump(1);
    }
    if (om_lg_on) {
        om_ls_line(2, "", 0);
        om_ls_line(3, "", 0);
    }
}

static int fail(om_lerr *e, int s, int rc, const char *what, const char *detail)
{
    e->step = s; e->rc = rc; e->what = what; e->detail = detail;
    return rc ? rc : -1;
}

static int z_held(void)
{   /* one pad field (up to 3 if a read found no window): the press already held when step 1 appears */
    int t;
    for (t = 0; t < 3; t++) {
        uint16_t p = om_smpc_pad();
        if (om_smpc_pad_ok())
            return (p & OM_PAD_Z) != 0;
    }
    return 0;
}

static int wait_present(int want)
{
    int t, p;
    for (t = 0; ; t++) {
        p = om_cd_present();                                      /* < 0 (no periodic status yet) = keep waiting */
        if (p >= 0 && p == want) return 0;
        if (t > SWAP_FRAMES) return OM_CD_E_WAIT;
        om_txt_wait_vblank();
        if (om_lg_on) om_lg_pump(0);                                            /* the eye moves on with what it has read */
    }
}

/* the slave SH-2 off before anything else, whoever jumped here -- the menu (which also tries,
 * launchj.c) or the error screen's exit A. A game must get the halted slave a plain BIOS boot gives it; the BIOS boot
 * itself does not stop a running slave (docs/research/gex-freeze-rootcause.md §4 p3, measured on Mednafen). A failure
 * is shown on row 3 and the launch goes on: nothing else depends on it. The launcher's own pad reads (Z at step 1,
 * the error screen) use its own INTBACK driver (smpcpad.c) and are unaffected. */
static void slave_off(void)
{
    int rc = om_smpc_sshoff();
#ifdef OM_L1_HARNESS
    ((volatile uint32_t *)OM_CT(OM_L1G_BASE))[2] = (uint32_t)rc;
    ((volatile uint32_t *)OM_CT(OM_L1G_BASE))[3] = OM_L1G_LSSH;
#endif
    if (rc) {
        char line[41];
        om_msg_rc(line, sizeof line, "SLAVE CPU NOT STOPPED", "", rc);
        om_txt_puts(0, 3, line, 2);
    }
}

static void pgs_say(const char *s)
{
    if (om_lg_on) {
        om_ls_line(1, s, om_lg_c.acc2);
        pace();
        return;
    }
    om_txt_puts(0, 14, "                                        ", 0);
    om_txt_puts(1, 14, s, 1);
}

/* the long jobs' heartbeat (hardware 2026-11-24: the import and the first save swap after Saves on sat for minutes with
 * nothing moving): the SD write gap (lgap.c, between 2 KB chunks) also turns a spinner at column 0 of row 22 */
static uint32_t s_spin;
static uint8_t s_imp_orb;                                         /* the import's pages are on the orbiting logo */
static void gap_beat(void)
{
    static const char spin[4][2] = { "|", "/", "-", "\\" };
    pace();                                                       /* the eye first (its share of reads: lgfx.h), so */
    om_lgap();                                                    /* the V-blank rest still precedes the next chunk */
    (void)om_lclock_us();                                         /* the swap's clock sees every FRC wrap */
    if (s_imp_orb) om_orb_tick();
    if (om_lg_on) return;                                         /* the eye is the heartbeat */
    om_txt_puts(0, 22, spin[s_spin++ & 3u], 0);
}
/* the save swap's screen: step n of N and its name, a bar over the steps, the file written now */
static void swap_prog(const om_pgs_prog *p)
{
    char l[41];
    uint32_t k;
    if (!p->steps) return;
    if (om_lg_on) {                                               /* "Save swap: step 3 of 9", the bar, the file */
        char *o = l;
        memcpy(o, "Save swap: step ", 16); o += 16;
        if (p->step >= 10u) *o++ = (char)('0' + p->step / 10u % 10u);
        *o++ = (char)('0' + p->step % 10u);
        memcpy(o, " of ", 4); o += 4;
        if (p->steps >= 10u) *o++ = (char)('0' + p->steps / 10u % 10u);
        *o++ = (char)('0' + p->steps % 10u);
        *o = 0;
        om_ls_line(1, l, om_lg_c.acc2);
        om_ls_line(2, p->file, om_lg_c.dim);
        om_ls_bar(p->step, p->steps);
        pace();
        return;
    }
    memset(l, ' ', 40); l[40] = 0;
    memcpy(l, "STEP", 4);
    for (k = 0; k < 2; k++) l[6 - k] = (char)('0' + (k ? p->step / 10u % 10u : p->step % 10u));
    memcpy(l + 8, "OF", 2);
    for (k = 0; k < 2; k++) l[12 - k] = (char)('0' + (k ? p->steps / 10u % 10u : p->steps % 10u));
    memcpy(l + 14, p->stage, strlen(p->stage) < 16 ? strlen(p->stage) : 16);
    l[38] = 0;
    om_txt_puts(1, 17, l, 0);
    om_pgs_bar(l, 28, p->step, p->steps);
    om_txt_puts(1, 18, l, 1);
    memset(l, ' ', 40);
    memcpy(l, "NOW", 3);
    memcpy(l + 4, p->file, strlen(p->file));
    l[38] = 0;
    om_txt_puts(1, 19, l, 0);
    om_pgs_bar(l, 28, p->file_done, p->file_total);
    om_txt_puts(1, 20, p->file_total ? l : "                                       ", 1);
}

/* the swap's timing for a hardware session: "SWAP 2941MS ROOT 85000US REL 3000US" on the page's last line (row 21) */
static char *put_dec(char *o, uint32_t v)
{
    char d[10];
    int k = 0;
    do { d[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k) *o++ = d[--k];
    return o;
}
static void swap_time(const om_pgs_result *r)
{
    char l[48], *o = l;
    if (!r->t_ms && !r->t_root_us && !r->t_rel_us) return;
    memcpy(o, "SWAP ", 5); o = put_dec(o + 5, r->t_ms);
    memcpy(o, "MS ROOT ", 8); o = put_dec(o + 8, r->t_root_us);
    memcpy(o, "US REL ", 7); o = put_dec(o + 7, r->t_rel_us);
    memcpy(o, "US", 3);
    if (om_lg_on) om_ls_line(3, l, om_lg_c.dim);
    else om_txt_puts(1, 21, l, 0);
}

/* the save swap. The arena comes from the heap (launcher.ld counts it in the floor) and goes back before
 * step 4, after the last backup-library call (bram.h review M9: no BUP call once the library's buffer is freed). */
static int save_swap(const om_handoff *h, om_lerr *e)
{
    static om_pgs_result r;
    static char what[41], date[16];
    om_pgs_env env;
    uint8_t *raw = (uint8_t *)malloc(OM_PGS_ARENA + 4u);
    int rc;
    if (!raw)
        return fail(e, 3, OM_E_FULL, "SAVES: NO MEMORY FOR THE SWAP", "");
    memset(&env, 0, sizeof env);
    om_pgs_arena_split(&env, (uint8_t *)(((uintptr_t)raw + 3u) & ~(uintptr_t)3u));
    env.gap = gap_beat;
    env.say = pgs_say;
    env.prog = swap_prog;
    env.nonce = om_lnonce;                                         /* fix round 1 (R3) */
    /* the always-on BRAM backup inside om_pgs_run, right after the library's Init (pgsbak.h); it never
     * stops the launch. user decision: the Kai/Rings import runs only on request (om_run_import). */
    om_smpc_rtc(date);                                             /* "" when the clock was never set */
    env.backup = 1;
    env.date = date;
    env.rel = 1;                                                   /* names from /aether (pgsrun.h): faster */
    env.now_us = om_lclock_us;                                     /* the run's timing (swap_time) */
    om_lclock_slow(1);
    rc = om_pgs_run(&env, h->savetool ? 0 : h->saves, h->slot, h->gid, &r);   /* a save tool: the general set */
    om_lclock_slow(0);
    /* the game folder back, whatever the swap returned: s_emulate("out.desc") (step 6) is named from it. The very
     * call step 2 made; done before anything else, so no path below (an error screen, the cheats, a notice) runs
     * from /aether */
    {   /* always: the swap may have left /aether current even when it named its files absolutely (pgsrun.h) */
        int c = s_chdir(al(s_dir[0] ? s_dir : h->dir));
        if (c < 0 && !rc) {                        /* a swap that failed reports its own failure first (below) */
            free(raw);
            return fail(e, 3, c, "Cannot re-enter game folder", s_dir[0] ? s_dir : h->dir);
        }
    }
    free(raw);
    if (om_lg_on) {
        om_ls_line(1, "", 0);
        om_ls_line(2, "", 0);
        om_ls_bar(0, 0);
    } else {
        int y;
        for (y = 17; y < 23; y++)                                  /* the swap's progress rows */
            om_txt_puts(0, y, "                                        ", 0);
    }
    if (r.bak_notice[0])                                           /* one line, ~3 s */
        notice(r.bak_notice, 0, 19);
    if (!rc) {
        if (r.notice[0]) {                                         /* a notice, ~3 s;
                                                                    * up to two rows (the torn-save line is longer) */
            static char row[41];
            size_t n = strlen(r.notice);
            memcpy(row, r.notice, n < 40 ? n : 40);
            row[n < 40 ? n : 40] = 0;
            notice(row, n > 40 ? r.notice + 40 : 0, 15);
        }
        swap_time(&r);
        return 0;
    }
    strcpy(what, r.undone ? "SAVE SWAP UNDONE, SAVES AS BEFORE: " : "SAVE SWAP STOPPED: ");
    strncat(what, om_pgs_op_name(r.op), sizeof what - 1 - strlen(what));
    return fail(e, 3, rc, what, r.line);
}

/* user decision 2026-11-21: the import is optional, run from Settings: the menu hands off
 * with OM_HO_IMPORT and no game. In API mode, the copy-only import (pgsbak.h) shows its progress on row 14 and its
 * result on rows 16-21; any button then resets the console (SMPC SYSRES, gotcha #27: the way back to Aether, as
 * after a game). No BRAM call, no swap, no backup, no disc. The IGR path never comes here (it is a SYSRES). */
/* the import's live screen (hardware 2026-11-24: "IMPORT: MK-81042" for minutes with nothing moving): game n of N and
 * a bar over the games, the saves handled so far, the save now with its own bar, and a spinner that moves on every
 * piece read, compared or written. B held over two pad reads (taken at most once per game, save or 16 pieces, so the
 * import does not wait on the pad) cancels at the next safe point between files (pgsbak.h). */
static uint8_t s_imp_cancel, s_imp_bdown;
static uint32_t s_imp_lastbeat;
static char s_imp_lastfile[20], s_imp_lastgame[24];
static void put_num(char *o, uint32_t v, int w)       /* right-aligned in w columns */
{
    int k = w;
    while (k--) o[k] = ' ';
    k = w - 1;
    do { o[k--] = (char)('0' + v % 10u); v /= 10u; } while (v && k >= 0);
}
static void imp_prog(const om_pgs_prog *p)
{
    static const char spin[4] = { '|', '/', '-', '\\' };
    char l[41];
    int sample = strcmp(p->file, s_imp_lastfile) || strcmp(p->game, s_imp_lastgame) || p->beat - s_imp_lastbeat >= 16u;
    if (s_imp_orb) {
        om_orbp_import_prog(p, s_imp_cancel);
        goto pad;
    }
    memset(l, ' ', 40); l[40] = 0;
    memcpy(l, "GAME", 4);
    put_num(l + 5, p->games_done < p->games_total ? p->games_done + 1 : p->games_done, 3);
    memcpy(l + 9, "OF", 2);
    put_num(l + 12, p->games_total, 3);
    memcpy(l + 17, p->game, strlen(p->game) < 21 ? strlen(p->game) : 21);
    l[38] = 0;                                        /* columns 1-38 */
    om_txt_puts(1, 15, l, 0);
    om_pgs_bar(l, 28, p->games_done, p->games_total);
    om_txt_puts(1, 16, l, 1);
    memset(l, ' ', 40);
    memcpy(l, "SAVES HANDLED", 13);
    put_num(l + 14, p->saves_done, 5);
    l[19] = 0;
    om_txt_puts(1, 17, l, 0);
    memset(l, ' ', 40);
    memcpy(l, "NOW", 3);
    memcpy(l + 4, p->file, strlen(p->file));
    put_num(l + 24, p->file_done / 1024u, 5);
    l[29] = '/';
    put_num(l + 30, (p->file_total + 1023u) / 1024u, 5);
    memcpy(l + 35, " KB", 3);
    l[38] = 0;
    om_txt_puts(1, 19, l, 0);
    om_pgs_bar(l, 28, p->file_done, p->file_total);
    om_txt_puts(1, 20, l, 1);
    memset(l, ' ', 40);
    l[0] = spin[p->beat & 3u];
    memcpy(l + 2, s_imp_cancel ? "CANCELING..." : "WORKING  (HOLD B TO CANCEL)", s_imp_cancel ? 12 : 27);
    l[38] = 0;
    om_txt_puts(1, 22, l, s_imp_cancel ? 2 : 0);
pad:
    if (sample && !s_imp_cancel) {
        uint16_t pad = om_smpc_pad();                 /* one field at most */
        int b = om_smpc_pad_ok() && (pad & OM_PAD_B);
        if (b && s_imp_bdown) s_imp_cancel = 1;
        s_imp_bdown = (uint8_t)b;
        s_imp_lastbeat = p->beat;
        strcpy(s_imp_lastfile, p->file);
        strcpy(s_imp_lastgame, p->game);
    }
}
static int imp_cancel(void) { return s_imp_cancel; }
static void imp_say(const char *s) { if (!s_imp_orb) pgs_say(s); }   /* the pages name the game themselves */
static void put_row(int row, const char *label, uint32_t v)
{   /* the result: the label, then the count right-aligned in columns 31-35 (hardware 2026-11-24: numbers glued to
     * labels of different lengths read as shifted against them) */
    char t[41];
    size_t n = strlen(label);
    memset(t, ' ', 40);
    memcpy(t, label, n < 30 ? n : 30);
    put_num(t + 30, v, 5);
    t[35] = 0;
    om_txt_puts(1, row, t, 0);
}

static int om_run_import(const om_handoff *h, om_lerr *e)
{
    static om_pgs_imp r;
    static char note[OM_PGS_NOTICE];
    om_pgs_env env;
    uint8_t *raw;
    int rc, t, y;
    step(1, "s_mode(s_api)");
    if ((rc = s_mode(s_api)) != 0) return fail(e, 1, rc, "Satiator API mode failed", "");
    if (!(raw = (uint8_t *)malloc(OM_PGS_ARENA + 4u))) return fail(e, 1, OM_E_FULL, "IMPORT: NO MEMORY", "");
    om_lclock_slow(1);                                /* the comet's clock: an FRC wrap every ~312 ms (a reset ends it) */
    if (om_lui_ok) {                                  /* the menu's last eye frame behind, still: the card is the import's */
        om_lg_start(h, 1);
        om_lg_quiet();
        if (!(s_imp_orb = om_orbp_import_begin() == 0)) {
            om_lg_stop();                             /* no logo: the text screen, as before */
            om_txt_init();
            om_lg_used = 0;
        }
    }
    memset(&env, 0, sizeof env);
    om_pgs_arena_split(&env, (uint8_t *)(((uintptr_t)raw + 3u) & ~(uintptr_t)3u));
    env.gap = gap_beat;
    env.say = imp_say;
    env.prog = imp_prog;
    env.cancel = imp_cancel;
    s_imp_cancel = s_imp_bdown = 0;
    s_imp_lastbeat = 0;
    s_imp_lastfile[0] = s_imp_lastgame[0] = 0;
    if (!s_imp_orb) {
        om_txt_puts(1, 10, "IMPORT SAVES FROM KAI / RINGS (COPIES)", 1);
        om_txt_puts(1, 11, "KAI AND RINGS FOLDERS ARE NOT CHANGED.", 0);
    }
    om_pgs_import_sel(&env, h->gid, &r, note);   /* the games ticked in the menu's picker */
    free(raw);
    if (s_imp_orb) {
        om_orbp_import_result(&r, note, om_pgs_import_first_skip());
        goto wait;
    }
    for (y = 13; y < 26; y++)
        om_txt_puts(1, y, "                                      ", 0);
    om_txt_puts(1, 13, r.canceled ? "IMPORT CANCELED, COPIES MADE ARE KEPT" : r.rc ? "IMPORT STOPPED -- RUN IT AGAIN"
                                   : "IMPORT DONE", r.rc && !r.canceled ? 2 : 1);
    if (r.rc && !r.canceled) om_txt_puts(1, 14, note, 2);
    if (r.canceled) om_txt_puts(1, 14, "RUN IT AGAIN TO FINISH (NOTHING TWICE)", 0);
    put_row(15, "COPIED FROM KAI", r.kai);
    put_row(16, "COPIED FROM RINGS", r.rings);
    put_row(17, "ALREADY ON THE CARD", r.present);
    put_row(18, "CONFLICTS (YOURS KEPT)", r.conflicts);
    put_row(19, "OLDER COPIES TO QUARANTINE", r.older);
    put_row(20, "SKIPPED (SEE IMPORT.TXT)", r.skipped);
    put_row(21, "CART-ONLY (IN CART/)", r.cart);
    put_row(22, "KEPT AS ALTERNATES", r.alternates);              /* Rings slots 10-19, all full */
    if (r.skipped) {                                                /* 24c: the first one named; all in IMPORT.TXT */
        static char sk[41];
        const char *f = om_pgs_import_first_skip();
        size_t n = strlen(f);
        if (n > 23) f += n - 23;                                    /* the end of the path: the file's name */
        strcpy(sk, "FIRST SKIPPED: ");
        strncat(sk, f, 23);
        om_txt_puts(1, 23, sk, 2);
    }
    om_txt_puts(1, 25, "PRESS A BUTTON TO RETURN TO AETHER", 1);
wait:
    for (t = 0; t < 30; t++) {                                  /* a button still held from the menu: let it go */
        (void)om_smpc_pad();
        if (s_imp_orb) om_orb_tick();
    }
    for (;;) {
        uint16_t p = om_smpc_pad();
        if (s_imp_orb) om_orb_tick();
        if (om_smpc_pad_ok() && (p & (OM_PAD_A | OM_PAD_B | OM_PAD_C))) break;
    }
    om_lclock_slow(0);                                          /* the comet's clock back to the launch's */
    return om_go_home(h, e);                                    /* the menu again, Settings open (lseq.h) */
}

/* the loading screen goes on: its eye frame, the game's name and cover (lgfx.h) */
static void screens_on(const om_handoff *h, int cwx)
{
    static char t[64];
    if (!om_lui_ok) return;
    if (s_ret) {                                  /* back from a tool: its screens stay up, a page of their own */
        if (om_lg_on) {
            om_ls_page("RETURNING TO", "Aether", 0);
            om_ls_status("Keep the power on", om_lg_c.dim);
        }
        step(1, "s_mode(s_api)");
        return;
    }
    title_of(h->image, t, sizeof t);
    om_lg_start(h, 1);
    om_ls_page("NOW LOADING", t, 0);
    om_ls_meta(0, cwx ? "Loader: CWX" : "Loader: JHL", om_lg_c.dim);
    om_ls_status("Keep the power on", om_lg_c.dim);
    pace_on(om_lg_on);                            /* the eye moves through every Satiator call to step 4 (pace) */
    step(1, "s_mode(s_api)");
}

/* before step 9 zeroes or loads over HWRAM: a CWX launch first leaves its failure page's logo and faces in VRAM (lorb.h),
 * from .lui while it is still here */
static void screens_stop(int cwx)
{
    pace_on(0);
    if (!om_lg_on) return;
    if (cwx && om_lui_ok && om_orbp_stash(om_orbp_bg(OM_ORB_COMPACT)) == 0) om_orb_armed = 1;
    om_lg_stop();
}

#ifdef OM_L1_HARNESS
/* the L1 harness's import (OM_HO_IMPORT with no card): the pages with made-up progress, then the result for ~6 s; the
 * harness then goes on as a CWX launch that fails at step 9 (l1_fail9), for the step-9 page */
static void l1_import_demo(const om_handoff *h)
{
    static om_pgs_prog p;
    static om_pgs_imp r;
    static const char *const games[3] = { "MK-81009[U]", "T-4507G[J]", "MK-81307[U]" };
    uint32_t f;
    om_lg_start(h, 0);
    if (!(s_imp_orb = om_orbp_import_begin() == 0)) return;
    memset(&p, 0, sizeof p);
    p.games_total = 12;
    for (f = 0; f < 420u; f++) {
        p.games_done = f / 36u;
        p.saves_done = f / 5u;
        strcpy(p.game, games[(f / 36u) % 3u]);
        strcpy(p.file, (f / 18u) & 1u ? "PANZER_01.BUP" : "CONFIG.BUP");
        p.file_total = 32768u;
        p.file_done = (f % 18u) * 1900u;
        p.beat = f;
        imp_prog(&p);
        if (f == 360u) s_imp_cancel = 1;              /* the cancel's line, as B held would show it */
        om_lgap();
    }
    memset(&r, 0, sizeof r);
    r.kai = 16; r.rings = 97; r.present = 4; r.conflicts = 1; r.older = 2; r.skipped = 1; r.alternates = 3;
    om_orbp_import_result(&r, "", "KAI/SAVES/MK-81009/PANZER_99.BUP");
    for (f = 0; f < 360u; f++) { om_lgap(); om_orb_tick(); }
    s_imp_orb = s_imp_cancel = 0;
}
static om_boot_trace s_l1tr;
static int l1_fail9(void)
{   /* CWX's own wipe of High Work RAM (cwxboot.c stage 2: .lui and its fonts are gone), then a read that never ends */
    volatile uint32_t *p;
    for (p = (volatile uint32_t *)0x06004000u; p < (volatile uint32_t *)0x06100000u; p++) *p = 0;
    memset(&s_l1tr, 0, sizeof s_l1tr);
    memcpy(s_l1tr.head, "SEGA SEGASATURN SEGA TP T-159   ", 32);
    s_l1tr.boot_rc = OM_CD_E_SECTORS;
    s_l1tr.rd_max = 0x12; s_l1tr.rd_status = 0x23; s_l1tr.rd_fad = 0x2A0;
    return OM_CD_E_SECTORS;
}
#endif

int om_run_launch(const om_handoff *h, om_lerr *e)
{
    const char *dir = h->dir;                                     /* the image(s): ldesc.h */
    const char *region = om_region_string(h->area_code);
    om_boot_trace tr;
    satiator_cart_header_t *cart;
    uint8_t area = h->area_code;
    int rc, test = (h->flags & OM_HO_TEST_NO_SATIATOR) != 0, igr = h->igr != 0, cwx = h->loader == OM_LDR_CWX;
    memset(e, 0, sizeof *e);
    om_orb_armed = 0;
#ifdef OM_L1_HARNESS
    om_l1g_pre();
#endif
    slave_off();
#ifdef OM_L1_HARNESS
    if (test && (h->flags & OM_HO_IMPORT)) l1_import_demo(h);
#endif
    if ((h->flags & OM_HO_IMPORT) && !test)
        return om_run_import(h, e);
    if (h->flags & OM_HO_SAVEMGR) {                                /* Manage saves */
#ifndef OM_L1_HARNESS
        if (!test)
#endif
            return om_run_saves(h, e);                             /* L1 harness: its demo, no SD */
    }
    if (h->flags & OM_HO_TIDY)                                     /* gone in 0.1: an older menu asked for it */
        return fail(e, 0, OM_E_FIELD, "Tidy saves is not in this version", "copy Aether's files again");
    if (h->flags & OM_HO_CHEATS) {                                 /* the cheat editor */
#ifndef OM_L1_HARNESS
        if (!test)
#endif
            return om_run_cheats(h, e);                            /* L1 harness: its scripted demo, no SD */
    }
    if (!region)
        return fail(e, 0, area, "Unknown SMPC area code", "see HWF-15");
#ifdef OM_L1_HARNESS
    if (h->flags & OM_HO_TEST_BRAM) {                              /* l1_lbtest (emulator) only: review I1 */
        step(1, "BRAM self-test (L1)");
        om_bram_selftest(1);
    }
#endif
    if (!test) {
        step(1, "s_mode(s_api)");
        if (!s_ret && z_held()) {                                            /* the BRAM listing */
            if (!om_txt_ready) om_txt_init();                      /* on the text screen, before the page */
            om_bram_selftest(0);
        }
        if ((rc = s_mode(s_api)) != 0) return fail(e, 1, rc, "Satiator API mode failed", "");
        screens_on(h, cwx);
        step(2, "s_chdir");
        if ((rc = s_chdir(al(dir))) < 0) return fail(e, 2, rc, "Cannot enter game folder", dir);
        step(3, "image2desc");
        {
            int alone;
            if ((rc = om_ldesc_write(h, s_desc, s_dir, &alone)) != 0)
                return fail(e, 3, rc, "Disc load failed!", cdparse_error_string ? cdparse_error_string : "Unknown error");
            if (alone) {                                       /* a set's disc that does not load: no lid swap */
                static char l[41] = "DISC 0 OF THE SET DID NOT LOAD: NO SWAP";
                l[5] = (char)('0' + alone);
                notice(l, 0, 15);
            }
        }
        if (!s_ret && (rc = save_swap(h, e)) != 0)                   /* the menu disc: no swap, no cheats */
            return rc;
        if (om_lui_ok && !s_ret) om_lch_load(h, &s_chk, 20, 13);                  /* never stops the launch */
    }
#ifdef OM_L1_HARNESS
    else if (!s_ret && !memcmp((const void *)OM_LG_FILE, "OMSATLBG", 8)) {   /* the harness put LOADBG.BIN in RAM */
        static const char *const st[3] = { "ARCHIVE", "VERIFY", "LOAD" };
        om_pgs_prog p;
        uint32_t f;
        screens_on(h, cwx);
        memset(&p, 0, sizeof p);                                   /* the swap's page, shown as a swap would drive it */
        p.steps = 9;
        for (f = 0; f < 240u; f++) {
            p.step = 2u + f / 60u;
            strcpy(p.stage, st[(f / 60u) % 3u]);
            strcpy(p.file, (f / 30u) & 1u ? "EPISODE_4.BUP" : "CONFIG.BUP");
            swap_prog(&p);
            gap_beat();
        }
        om_ls_line(1, "", 0);
        om_ls_line(2, "", 0);
        om_ls_bar(0, 0);
    }
    if (test && cwx) {                     /* M6 Task 27b, emulator tier: Kai's whole path with one 0x01 code (l1_lbtest) */
        om_ch_file *f = (om_ch_file *)malloc(sizeof *f);          /* heap: the launcher's .bss is its floor */
        if (f) {
            memset(f, 0, sizeof *f);
            om_chk_default_master(f);                              /* 06000914:C305 at 0x06002580 */
            f->ncheats = 1; f->ncodes = 1;
            strcpy(f->cheat[0].name, "L1");
            f->cheat[0].flags = OM_CH_ON; f->cheat[0].count = 1;
            f->code[0].addr = OM_L1_CHEAT_WORD; f->code[0].value = 0xC0DE; f->code[0].cmd = 0x01;
            om_chk_plan_make(f, &s_chk);
            free(f);
        }
        om_txt_puts(20, 13, s_chk.status == OM_CHS_ARMED ? "CHEATS: L1 ARMED" : "CHEATS: L1 FAILED", 1);
    }
#endif
    step(4, "reset interrupts, cart hook");
    om_vec_to_entry();                                             /* SYS_SETSINT writes VBR + 4n (HWF-26) */
    om_reset_interrupts();
    if (!test) {
        cart = s_find_cartridge();                                 /* main.c:169-171, API mode, before s_emulate */
        if (cart && cart->header_version >= 1)
            cart->install_soft_reset_hook();
    }
    om_vec_install();                                              /* back on the launcher's table */
    pace_on(0);                                                    /* the pacer reads: no more from here */
    if (om_lg_on) om_lg_quiet();                                   /* no SD read once the disc is being emulated */
    step(5, "restore VDP1 snapshot");
    om_restore_vdp1();
    if (!test) {
        step(6, "s_emulate");
        if ((rc = s_emulate(al(s_desc))) < 0) { s_emulate(al("")); return fail(e, 6, rc, "s_emulate failed", ""); }
        step(7, "waiting for disc swap");
        if (wait_present(0)) { s_emulate(al("")); return fail(e, 7, OM_CD_E_WAIT, "Disc never left", "timeout 10 s"); }
        if (wait_present(1)) { s_emulate(al("")); return fail(e, 7, OM_CD_E_WAIT, "Disc never arrived", "timeout 10 s"); }
        screens_stop(cwx);
        step(8, "s_mode(s_cdrom)");
        if ((rc = s_mode(s_cdrom)) != 0) {
            s_mode(s_api);
            s_emulate(al(""));
            return fail(e, 8, rc, "CD mode switch failed", "");
        }
    }
#ifdef OM_L1_HARNESS
    om_l1g_halt();
#endif
    screens_stop(cwx);
    om_lui_ok = 0;                                                 /* .lui is gone from here on (lgfx.h) */
    step(9, cwx ? "boot (CWX)" : "boot (JHL)");                    /* M6 Task 17 */
    if (!om_lg_used)                                               /* else the eye's last frame stays up */
        om_txt_puts(0, 13, igr ? "IN GAME RESET: ON" : "IN GAME RESET: OFF", igr ? 1 : 0);   /* M6 Task 20a */
    /* upstream fades out here (fadeout(0x20)). CWX keeps the screen on through its reads: with TVMD DISP = 0 the VDP2
     * is "in the blank condition during the display interval" (VDP2_Manual.txt:1288-1289), so TVSTAT's VBLANK bit
     * does not toggle and every om_txt_wait_vblank runs to its poll cap -- measured on Mednafen this session: the CWX
     * self-boot sat 15 s black with the display off, 4.5 s with it on (HWF-76 (c)). The CWX hand-off's video reset
     * blanks it. */
    if (!cwx)
        om_txt_display(0);
#ifdef OM_L1_HARNESS
    if (test && (h->flags & OM_HO_IMPORT))
        rc = l1_fail9();
    else
#endif
    if (cwx)
        rc = om_cwx_boot(&tr, igr, s_chk.status == OM_CHS_ARMED ? &s_chk : 0);                                /* returns only on failure (cwxboot.h) */
    else
        rc = om_boot_disc(region, &tr, igr);                       /* om_boot_handover runs inside, before loadcd_boot */
    set_sr_mask(15);                                               /* back to the launcher's state (all masked) */
    om_vec_install();
    om_txt_display(1);
    if (!test) {
        om_cd_spin_cap(200000u);                                   /* stop the read's play and reset the selector */
        om_cd_stream_stop();                                       /* before the API switch (HWF-56; relaunch.c) */
        om_cd_spin_cap(0);
        s_mode(s_api);                                             /* failed: restore order (main.c:181-182) */
        s_emulate(al(""));
    }
    e->trace = cwx ? om_cwx_last() : om_boot_last();
#ifdef OM_L1_HARNESS
    if (test && (h->flags & OM_HO_IMPORT)) e->trace = &s_l1tr;
#endif
    return fail(e, 9, rc, "Boot failed!", cwx ? om_cwx_boot_strerror(rc) : om_boot_strerror(rc));
}

int om_run_return(const om_handoff *menu, om_lerr *e)
{
    int rc;
    s_ret = 1;
    memset(&s_chk, 0, sizeof s_chk);                              /* no cheats armed for the menu disc */
    rc = om_run_launch(menu, e);                                  /* returns only on failure */
    s_ret = 0;
    return rc;
}
