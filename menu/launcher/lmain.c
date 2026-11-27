/* menu/launcher/lmain.c -- LAUNCHER.BIN main. Spec §7.1 launch steps 3-4: run the launch; on failure HWRAM is
 * forfeit, so show the launcher's own error screen with two exits. The first thing it does is point VBR at the
 * launcher's own vector table (lvec.c); an exception lands in om_fail_screen too, so the exits are
 * reachable from an "EXC:" screen as well. BSD-3-Clause. */
#include <string.h>
#include "jhboot.h"
#include "handoff.h"
#include "lseq.h"
#include "lvec.h"
#include "msgfmt.h"
#include "omlayout.h"
#include "padgest.h"
#include "relaunch.h"
#include "satiator.h"
#include "cdblk.h"
#include "lgap.h"
#include "settings.h"
#include "smpcpad.h"
#include "vdp2txt.h"
#include "lgfx.h"
#include "lscreen.h"
#include "lorb.h"

void om_hop_to_official(void);

static uint8_t s_area = 4;            /* exit A's region when the hand-off block was rejected (HWF-15: USA/CANADA) */

static void hex(char *o, uint32_t v, int digits)
{
    while (digits--)
        *o++ = "0123456789ABCDEF"[(v >> (4 * digits)) & 15u];
}

/* The step-9 (BIOS boot) evidence, rows 20-23: what was read from FAD 150 before the region patch, and the CD-block
 * results around the BIOS call (hardware launch "Bad disc header", 2026-11-19). put: the row's place on the screen. */
static void txt_row(int row, const char *l) { om_txt_puts(0, row, l, 0); }
static void show_boot_trace(const om_boot_trace *t, void (*put)(int row, const char *l))
{
    char l[41];
    int i;
    for (i = 0; i < 32; i++) {                                  /* row 20: the 32 header bytes as text */
        uint8_t c = t->head[i];
        l[i] = (char)(c >= 0x20 && c < 0x7F ? c : '.');
    }
    l[32] = 0;
    put(20, l);
    for (i = 0; i < 16; i++) hex(l + 2 * i, t->head[i], 2);    /* row 21: the first 16 as hex */
    l[32] = 0;
    put(21, l);
    memset(l, ' ', 40);                                         /* row 22: init rcs, auth rc/status, read, put */
    for (i = 0; i < 4; i++) hex(l + 3 * i, (uint32_t)t->init_rc[i], 2);
    hex(l + 12, (uint32_t)t->auth_rc, 2);  hex(l + 15, t->auth_status, 4);
    hex(l + 20, (uint32_t)t->read_rc, 4);  hex(l + 25, (uint32_t)t->put_rc, 4);
    l[40] = 0;
    put(22, l);
    memset(l, ' ', 40);                                         /* row 23: BIOS rc, loops, HIRQ before/after, CD status */
    hex(l, (uint32_t)t->boot_rc, 8); hex(l + 9, (uint32_t)t->loops, 4);
    hex(l + 14, t->hirq_in, 4); hex(l + 19, t->hirq, 4); hex(l + 24, t->peri, 4);
    l[28] = 0;
    put(23, l);
    if (t->boot_rc == OM_CD_E_SECTORS && !t->cdc) {
        /* row 26 (CWX, a read timed out): "N <most sectors seen> S <Get CD Status rc> <status> <FAD>" -- status
         * low nibble 0 BUSY, 1 PAUSE, 2 STANDBY, 3 PLAY, 4 SEEK, 6 OPEN, 7 NODISC (SEGA_CDC.H CdcStatus) */
        memset(l, ' ', 40);
        l[0] = 'N'; hex(l + 2, (uint32_t)t->rd_max, 4);
        l[7] = 'S'; hex(l + 9, (uint32_t)t->rd_cur_rc, 2); hex(l + 12, t->rd_status, 2); hex(l + 15, (uint32_t)t->rd_fad, 6);
        l[21] = 0;
        put(26, l);
        return;
    }
    if (!t->cdc)
        return;
    /* rows 26-27 (CWX, a LIBCD call failed): "C <code, decimal> B" + HIRQ CR1-CR4 just before that call, then
     * "E <longest EHST wait, hex> A" + HIRQ CR1-CR4 at its failure. code = step * 100 - CdcErr (cdblk.h): 401 =
     * GetdelSctData refused (CMDBUSY), 1xx CdSetCon, 2xx CdPlay, 3xx GetSctNum, 5xx DataReady, 6xx DataEnd. */
    for (i = 0; i < 2; i++) {
        const uint16_t *r = i ? t->cd_post : t->cd_pre;
        int k;
        memset(l, ' ', 40);
        l[0] = i ? 'E' : 'C';
        if (i) hex(l + 2, t->ehst_max, 6);
        l[9] = i ? 'A' : 'B';
        for (k = 0; k < 5; k++) hex(l + 11 + 5 * k, r[k], 4);
        l[35] = 0;
        if (!i) {                                               /* the code, in decimal, from column 2 */
            int32_t v = t->cdc < 0 ? -t->cdc : t->cdc, n = 0;
            char d[8];
            do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 6);
            k = 2;
            if (t->cdc < 0) l[k++] = '-';
            while (n) l[k++] = d[--n];
        }
        put(26 + i, l);
    }
}

/* the error page on the loading screen's look while its screens are up (any step before 9); else the text screen,
 * with the step-9 boot trace */
static void step_rc(char *o, const om_lerr *e)
{   /* "Step 9  ·  rc -43" */
    int n = e->step, rc = e->rc;
    memcpy(o, "Step ", 5); o += 5;
    if (n >= 10) *o++ = (char)('0' + n / 10 % 10);
    *o++ = (char)('0' + (n < 0 ? 0 : n % 10));
    strcpy(o, "  \xC2\xB7  rc ");
    o += strlen(o);
    if (rc < 0) { *o++ = '-'; rc = -rc; }
    {
        char d[12];
        int k = 0;
        do { d[k++] = (char)('0' + rc % 10); rc /= 10; } while (rc);
        while (k) *o++ = d[--k];
        *o = 0;
    }
}
static void show_error_lg(const om_lerr *e)
{
    char l[48];
    om_ls_page("LAUNCH FAILED", e->what ? e->what : "", 0);
    om_ls_meta(0, e->detail ? e->detail : "", om_lg_c.dim);
    step_rc(l, e);
    om_ls_meta(1, l, om_lg_c.dim);
    om_ls_line(0, "A: re-launch Aether", om_lg_c.txt);
    om_ls_line(1, "B or C: reset the console", om_lg_c.txt);
}

/* a CWX launch that failed at or after step 9 (High Work RAM, .lui with it, is gone): the orbiting logo over the eye's
 * last frame, from the VRAM stash (lorb.h), the message, then every trace row in the 8x8 font */
static uint8_t s_orb;
static void orb_row(int row, const char *l) { om_orb_mono(16, 130 + 9 * (row < 26 ? row - 20 : row - 22), l, om_lg_c.dim); }
static void show_error_orb(const om_lerr *e)
{
    char l[48];
    s_orb = 1;
    om_orb_scene(OM_ORB_COMPACT, om_orb_stash_bg());
    om_orb_text(om_orb_face(OM_ORB_LABEL), 16, 303, 80, "LAUNCH FAILED", om_lg_c.err, OM_ORB_CENTER | OM_ORB_TRACK);
    om_orb_text(om_orb_face(OM_ORB_ITEM), 16, 303, 97, e->what ? e->what : "", om_lg_c.txt, OM_ORB_CENTER);
    om_orb_text(om_orb_face(OM_ORB_SMALL), 16, 303, 110, e->detail ? e->detail : "", om_lg_c.dim, OM_ORB_CENTER);
    step_rc(l, e);
    om_orb_text(om_orb_face(OM_ORB_SMALL), 16, 303, 122, l, om_lg_c.dim, OM_ORB_CENTER);
    show_boot_trace(e->trace ? e->trace : om_boot_last(), orb_row);
    om_orb_text(om_orb_face(OM_ORB_SMALL), 16, 303, 198, "A: re-launch Aether  \xC2\xB7  B or C: reset the console",
                om_lg_c.txt, OM_ORB_CENTER);
}

static void text_screen(void)
{
    if (!om_txt_ready || om_lg_used) {
        if (om_lg_on && om_lui_ok) om_lg_stop();
        om_txt_init();
        om_lg_used = 0;
    }
    s_orb = 0;
}

static void say(const char *s, int pal)
{   /* the error screen's message line: row 26, or the screens' status line */
    if (s_orb) {
        om_orb_text(om_orb_face(OM_ORB_SMALL), 16, 303, 212, s, pal == 2 ? om_lg_c.err : om_lg_c.txt,
                    OM_ORB_CENTER | OM_ORB_SOLID);
        return;
    }
    if (om_lg_on && om_lui_ok) {
        om_ls_status(s, pal == 2 ? om_lg_c.err : om_lg_c.txt);
        return;
    }
    om_txt_puts(0, 26, "                                        ", 0);
    om_txt_puts(0, 26, s, pal);
}

static void show_error(const om_lerr *e)
{
    int y;
    if (om_lg_on && om_lui_ok && e->step < 9) {
        show_error_lg(e);
        return;
    }
    if (e->step >= 9 && om_orb_armed && om_lg_used && om_orb_stash_ok()) {   /* a CWX launch's stash, intact */
        show_error_orb(e);
        return;
    }
    text_screen();
    for (y = 16; y < 28; y++)
        om_txt_puts(0, y, "                                        ", 0);
    om_txt_puts(0, 16, "LAUNCH FAILED at step", 2);
    om_txt_putn(22, 16, e->step, 2);
    om_txt_puts(0, 17, e->what ? e->what : "", 2);
    om_txt_puts(0, 18, e->detail ? e->detail : "", 0);
    om_txt_puts(0, 19, "rc", 0);
    om_txt_putn(3, 19, e->rc, 0);
    if (e->step == 9)
        show_boot_trace(e->trace ? e->trace : om_boot_last(), txt_row);
    om_txt_puts(0, 24, "A: re-launch Aether (autoboot.iso)", 1);
    om_txt_puts(0, 25, "B or C: reset console", 1);
}

static int file_exists(const char *path)
{
    static uint32_t name[76], st[76];
    int i;
    for (i = 0; path[i] && i < 255; i++) ((char *)name)[i] = path[i];
    ((char *)name)[i] = 0;
    return s_stat((const char *)name, (s_stat_t *)st, (int)sizeof st - 4) >= 0;
}

/* exit A's ops (relaunch.h) */
static om_lerr *s_rl_err;
static void rl_cd_stop(void) { om_cd_spin_cap(200000u); om_cd_stream_stop(); om_cd_spin_cap(0); }
static int rl_mode(void) { return s_mode(s_api); }
static const char s_empty[4] __attribute__((aligned(4))) = "", s_root[4] __attribute__((aligned(4))) = "/";
static int rl_emu_off(void) { return s_emulate(s_empty); }    /* 4-aligned, as buffer_xfer wants */
static int rl_chdir(void) { return s_chdir(s_root); }
static om_handoff s_menu;                                     /* the menu disc's block: exit A's and a tool's way back */
static int rl_run(const char *image)
{
    om_launch_opts o;
    memset(&o, 0, sizeof o);
    o.loader = OM_LDR_JHL;                                    /* the menu disc: the BIOS boot, as before Task 17 */
    o.igr = 1;
    om_handoff_fill2(&s_menu, "/", image, s_area, 0, &o);
    return om_run_launch(&s_menu, s_rl_err);                    /* returns only on failure */
}
static void rl_say(const char *what, int rc)
{
    char line[41];
    int f;
    om_msg_rc(line, sizeof line, what, "", rc);
    say(rc ? line : what, rc ? 2 : 0);
    if (rc)
        for (f = 0; f < 90; f++) om_lgap();                   /* ~1.5 s to read it before the reset */
}

/* 2026-11-25 (hardware: B in the cheat editor reset the console and the user sat through the whole boot): a tool's
 * way back (lseq.h om_go_home) -- exit A's sequence (relaunch.c: bounded, each step checked, SYSRES on any failure)
 * minus the CD stop (no play was started: the tools ran in API mode), and the menu disc run by om_run_return. The
 * L1 harness (OM_HO_TEST_NO_SATIATOR) skips the Satiator calls and boots the disc in the drive. */
static const om_handoff *s_home;
static void home_cd(void) { }
static int home_ok(void) { return 0; }
static int home_exists(const char *path) { (void)path; return 1; }
static int home_run(const char *image)
{
    om_launch_opts o;
    memset(&o, 0, sizeof o);
    o.loader = OM_LDR_JHL;                                    /* the menu disc: the BIOS boot, as exit A */
    o.igr = 0;                                                /* the menu has its own A+B+C+Start */
    om_handoff_fill2(&s_menu, "/", image, s_home->area_code, s_home->flags & OM_HO_TEST_NO_SATIATOR, &o);
    return om_run_return(&s_menu, s_rl_err);                    /* returns only on failure */
}
static void home_say(const char *what, int rc)
{   /* the page already says "Returning to Aether": only a failure, ~1.5 s, before the reset -- on the eye's page while
     * it is up, else on the text screen (after step 9 .lui is gone), as exit A's rl_say */
    char line[41];
    int f;
    if (!rc) return;
    if (!(om_lg_on && om_lui_ok)) text_screen();
    om_msg_rc(line, sizeof line, what, "", rc);
    say(line, 2);
    for (f = 0; f < 90; f++) om_lgap();
}
static int home_sysres(void)
{
    (void)om_smpc_pad();                                      /* straight after a pad read: SYSRES's window */
    return om_smpc_sysres();
}

int om_go_home(const om_handoff *h, om_lerr *e)
{
    static const om_relaunch_ops live = { home_cd, rl_mode, rl_emu_off, rl_chdir, file_exists, home_run, home_sysres,
                                          home_say };
    static const om_relaunch_ops test = { home_cd, home_ok, home_ok, home_ok, home_exists, home_run, home_sysres,
                                          home_say };
    int rc;
    if (om_ret_arm((om_retblk *)OM_CT(OM_LW_RETURN)))        /* no block (an older menu): an ordinary start */
        om_ret_copy16((volatile void *)OM_SND_RETURN, (const volatile void *)OM_CT(OM_LW_RETURN));   /* past the BIOS */
    s_home = h;
    s_rl_err = e;
    s_area = h->area_code;
    rc = om_relaunch((h->flags & OM_HO_TEST_NO_SATIATOR) ? &test : &live);   /* returns only if SYSRES failed */
    e->step = 9; e->rc = rc; e->what = "RESET FAILED -- POWER OFF AND ON"; e->detail = "";
    return rc ? rc : -1;
}

void om_fail_screen(const om_lerr *first)
{
    static om_lerr e;
    uint16_t prev = 0xFFFFu;          /* a button already held when the screen appears is not a press */
    int shown = 0, why;
    char line[41];
    om_cgest cg;
    e = *first;
    show_error(&e);
    om_cgest_init(&cg, 0xFFFFu);      /* C: alone, on release (padgest.h); held at entry is not a press */
    for (;;) {
        uint16_t p, newly;
        p = om_smpc_pad();          /* waits for the INTBACK window: one read per field (smpcwait.h) */
        if (om_lg_on && om_lui_ok) om_lg_pump(1);
        if (s_orb) om_orb_tick();
        if (om_smpc_pad_fallback(&why)) {                         /* say why A/B may not answer */
            om_msg_rc(line, sizeof line, "PAD: NO DATA", "", why);
            say(line, 2);
            shown = 1;
        } else if (shown) {
            say("", 0);
            shown = 0;
        }
        newly = (uint16_t)(p & ~prev);
        prev = p;
        if (om_smpc_pad_ok() && om_cgest_step(&cg, p, OM_PAD_C)) {  /* real reads only: also SYSRES's window */
            int rrc;
            say("resetting the console...", 0);
            rrc = om_smpc_sysres();                               /* HWF-53; the SCSP was silenced at the hand-off */
            om_msg_rc(line, sizeof line, "RESET CONSOLE FAILED", "", rrc);
            say(line, 2);                             /* stays: `shown` is the PAD line's own flag */
            continue;
        }
        if (newly & OM_PAD_A) {                                   /* relaunch.c: checked, bounded, else SYSRES */
            static const om_relaunch_ops ops = { rl_cd_stop, rl_mode, rl_emu_off, rl_chdir, file_exists, rl_run,
                                                 om_smpc_sysres, rl_say };
            int rrc;
            s_rl_err = &e;
            text_screen();                                        /* the relaunch's own steps start from text */
            rrc = om_relaunch(&ops);                              /* returns only if SYSRES failed */
            show_error(&e);
            om_msg_rc(line, sizeof line, "RESET CONSOLE FAILED", "", rrc);
            say(line, 2);
        }
        if (newly & OM_PAD_B) {                                   /* HWF-58: the hop to the official menu fails on FW163 */
            int rrc;
            say("resetting the console...", 0);
            rrc = om_smpc_sysres();
            om_msg_rc(line, sizeof line, "RESET CONSOLE FAILED", "", rrc);
            say(line, 2);
        }
    }
}

void lmain(void)
{
    const om_handoff *h = (const om_handoff *)OM_CT(OM_LW_HANDOFF);
    om_lerr e = { 0, 0, 0, 0, 0 };
    int rc;
    om_vec_install();
    /* a launch, the cheat editor, the save manager and the import keep the menu's last eye frame up until their screens
     * continue it (no cut to black, no text: lgfx.h); a rejected block and the L1 harness's tests start on the text
     * screen, whose rows below are its diagnostics */
    if (om_handoff_check(h) || (h->flags & (OM_HO_TEST_NO_SATIATOR | OM_HO_TEST_ERRSCREEN)))
        om_txt_init();
    om_smpc_pad_init();                                            /* display on: calibrate polls/line */
    om_txt_puts(0, 0, "Aether LAUNCHER", 0);
    if ((rc = om_handoff_check(h)) != 0) {
        e.rc = rc; e.what = "Hand-off block rejected"; e.detail = "menu and launcher disagree";
        if (rc == OM_E_VERSION) {                                 /* a stale autoboot.iso or LAUNCHER.BIN */
            e.what = "LAUNCHER.BIN AND MENU VERSIONS DIFFER";
            e.detail = "COPY BOTH (autoboot.iso + LAUNCHER.BIN)";
        }
    } else {
        s_area = h->area_code;
        om_txt_puts(0, 1, h->dir, 1);
        om_txt_puts(0, 2, h->image, 1);
        if (h->flags & OM_HO_TEST_ERRSCREEN) {
            e.what = "TEST: error screen (L1 harness)"; e.detail = h->image;
        } else
            om_run_launch(h, &e);
    }
    om_fail_screen(&e);
}
