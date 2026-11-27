/* menu/app/main.c -- Aether entry: SGL_Start -> main() (menu/runtime/crt0.c) -> ss_main(). BSD-3-Clause. */
#include <string.h>
#include "sgl.h"
#include "satiator.h"
#include "art.h"
#include "audio.h"
#include "msgfmt.h"
#include "om_fmt.h"
#include "bootplay.h"
#include "boottime.h"
#include "omlayout.h"
#include "handoff.h"
#include "card.h"
#include "fatal.h"
#include "frcgap.h"
#include "folders.h"
#include "fwcheck.h"
#include "interstitial.h"
#include "isodir.h"
#include "logo.h"
#include "satfs.h"
#include "sd.h"
#include "text.h"
#include "theme.h"
#include "tree.h"
#include "vdp1tex.h"
#include "vdp2theme.h"
#include "uitext.h"          /* om_ut_frc: sd_gap's time bound */

#ifndef OMSAT_FAKECARD
#define OM_LIBBUF (208u * 1024u)
#else
#define OM_LIBBUF (16u * 1024u)     /* fake build only: frees HWRAM for the FAKECARD blob (plan D5); 16 KB since
                                     * Task 13c, whose LOADBG.BIN rides in the blob (the fake LIBRARY.IDX is 2.4 KB) */
#include "fakesat.h"
extern Uint8 om_fakecard[], om_fakecard_end[];
#endif
Uint8 g_area_code;
om_card g_card;
/* 2026-11-25: the return from a tool (handoff.h om_retblk): a copy of the block, which is cleared at once; ok = armed
 * and intact, so the intro is skipped and the menu reopens where the tool was started (folders.c) */
om_retblk g_om_ret;
int g_om_ret_ok;
static om_tree s_tree;
static uint8_t s_libbuf[OM_LIBBUF] __attribute__((aligned(4)));
static om_scan_work s_work;
typedef char om_bp_fits_in_s_work[sizeof(om_scan_work) >= OM_BP_ARENA ? 1 : -1];   /* Task 13b borrows it at boot */
const om_card_bufs g_bufs = {
    (uint8_t *)OM_CT(OM_LW_GAMESDB), OM_LW_GAMESDB_MAX,      /* HWF-12 */
    (uint8_t *)OM_CT(OM_LW_ARTTAB), OM_LW_ARTTAB_MAX,
    s_libbuf, OM_LIBBUF, &s_work,
    (uint8_t *)OM_CT(OM_LW_VRTILES), OM_LW_VRTILES_MAX };    /* M6 C3, HWF-12 */
/* status() already receives a step name before each blocking card-load call
 * (card.c's om_card_load), so the step that was in progress when a timeout fires is captured here and
 * folded into the fatal message, instead of just scrolling off-screen above it. */
static char s_last_step[40] = "s_mode";
/* Task 13d (Ruling M3-R40 (a)): the developer boot log is no longer printed as the steps run; the lines are kept here
 * and shown only above a fatal error (fail()), or in the fake build with FAKESCRIPT.DIAG. */
#define LOG_N 8
static char s_log[LOG_N][41];
static int s_log_n;
static char s_intro_msg[41];                          /* "INTRO SKIPPED: <step> rc=<n>", or empty */

static char s_sat_msg[40];
/* Builds "SATIATOR: NO RESPONSE (<step>)" without sprintf/strncpy/strcat (card.c's om_card_summary avoids
 * them the same way; see its app()/appn() helpers) -- <step> is already <= 39 B (om_text_put's own width
 * cap below), so this cannot overflow s_sat_msg. */
static const char *sat_timeout_msg(const char *step)
{
    static const char prefix[] = "SATIATOR: NO RESPONSE (";
    /* DEVIATION (toolchain): size_t needs <stddef.h>/<string.h>, not pulled in by this file's existing
     * includes -- "unknown type name 'size_t'" under saturn-sh2-elf-gcc. uint32_t (stdint.h, already
     * transitively visible) is plenty for a <= 39-byte buffer index. */
    uint32_t i = 0, j;
    while (prefix[i] && i < (uint32_t)sizeof s_sat_msg - 2) { s_sat_msg[i] = prefix[i]; i++; }
    for (j = 0; step[j] && i < (uint32_t)sizeof s_sat_msg - 2; j++, i++) s_sat_msg[i] = step[j];
    s_sat_msg[i++] = ')';
    s_sat_msg[i] = 0;
    return s_sat_msg;
}

/* Spec §7.1 + S2 conservative default: copy VDP1 VRAM before slInitSystem touches it, so LAUNCHER.BIN can
 * restore it before the game boots. 16-bit access only (HWF-23); cache-through aliases (HWF-12). */
static void snapshot_vdp1(void)
{
    volatile Uint16 *src = (volatile Uint16 *)OM_CT(OM_VDP1_VRAM);
    volatile Uint16 *dst = (volatile Uint16 *)OM_CT(OM_LW_SNAPSHOT);
    Uint32 i;
    for (i = 0; i < OM_VDP1_BYTES / 2; i++)
        dst[i] = src[i];
}

static void logline(const char *s)
{
    int i;
    char *d = s_log[s_log_n < LOG_N ? s_log_n++ : LOG_N - 1];
    for (i = 0; i < 40 && s[i]; i++) d[i] = s[i];
    d[i] = 0;
    if (om_boot_diag()) om_text_put(0, 3 + (s_log_n - 1), d, 40);
}

static void status(void *ctx, const char *s)
{
    (void)ctx;
    {
        int i;
        for (i = 0; i < 39 && s[i]; i++) s_last_step[i] = s[i];
        s_last_step[i] = 0;
    }
    logline(s);
    /* review-13d M1: the counts as card.c's sub-steps complete (GAMES.DB, then ART.PAK, then LIBRARY.IDX or a scan) */
    if (g_card.art.count || g_card.lib.count)
        om_istl_counts(om_libidx_games(&g_card.lib), g_card.lib.nfolders, g_card.art_rc ? 0u : g_card.art.count);
    om_istl_status();                                    /* a frame of the interstitial, if the last step took time */
}

/* 2026-11-23 hardware report: a card scan issues thousands of SD commands (measured on a host model of the reported
 * card: 4,841 for its first 470 root folders) and drew no frame, so the loading screen froze. The scan now reports each
 * folder (scan.h); every 4th V-blank at most, the interstitial draws a frame with the running counts (images, folders). */
static void scan_progress(void *ctx, uint32_t dirs, uint32_t images)
{
    static uint32_t s_at;
    (void)ctx;
    if (om_v2_vblanks() - s_at < 4u)
        return;
    s_at = om_v2_vblanks();
    om_istl_counts(images, dirs, g_card.art_rc ? 0u : g_card.art.count);
    om_istl_status();
}

/* OM_E_FULL from om_card_load, named by the step it came from (s_last_step): never a long scan that cannot fit */
static const char *too_big_msg(void)
{
    if (!strcmp(s_last_step, "Reading the card root"))
        return "CARD ROOT: TOO MANY FILES AND FOLDERS";
    if (!strcmp(s_last_step, "Loading LIBRARY.IDX"))
        return "LIBRARY.IDX OVER 208 KB: TOO MANY GAMES";
    return "CARD TOO BIG TO SCAN: BUILD IT ON THE PC";
}

/* A boot failure (from any module while the interstitial is up, through om_fatal's hook): the
 * interstitial stops, the log so far goes above the named error and its rc (fatal.c rows 23-27), as M2 showed it. */
static void boot_fatal(void)
{
    int i;
    if (!om_istl_active())
        return;                                          /* the menu is up: its own screen, no boot log */
    om_istl_abort();
    for (i = 0; i < s_log_n; i++)
        om_text_put(0, 3 + i, s_log[i], 40);
}

static void fail(const char *what, int rc)
{
    om_fatal(what, rc);                                  /* boot_fatal runs first */
}

/* the gap between SD write chunks (sdwrite.h): one V-blank, counted by the V-blank hook (om_v2_vblanks).
 * Not om_txt_wait_vblank: MEASURED on Mednafen 1.32.1 (the --sdtest stress page, gap -> instant fake write -> gap, no
 * slSynch), the menu stopped inside that bounded wait in 3 of 3 runs -- an exception or an emulator stall, mechanism
 * unknown (docs/ledger.md); the same function runs in cdblk.c's boot stream without incident.
 * bounded by time, not by an iteration count (20,000,000 iterations was about 10 s): at most
 * 40 ms (two PAL frames, a little over two NTSC ones), summed from short FRC differences (SH7604 FRT: the 16-bit FRC
 * at H'FFFFFE12, per SH7604_Hardware.txt ch. 11, lines 12689-12779; om_ut_frc_us applies the TCR divider), so no
 * single difference can wrap. A difference over 2 ms is dropped: one poll takes microseconds, and MEASURED on Mednafen
 * (the stress page, 1,500 gaps) 11 gaps ran out at 40 ms while the V-blank totals stayed exact, so the FRC sometimes
 * steps back mid-gap (a counter clear on compare match is the unverified explanation) and that step read as a near
 * full wrap. A gap that ran out is counted in om_sd_gap_late (a dead hook; the stress page shows it).
 * the bound is menu/src/frcgap.h -- raw ticks summed and converted once, and an
 * iteration cap that ends the gap even with a frozen FRC. */
static void sd_gap(void)
{
    uint32_t v0 = om_v2_vblanks();
    om_frcgap g;
    om_frcgap_start(&g, om_ut_frc(), om_ut_frc_us(10000u));
    while (om_v2_vblanks() == v0)
        if (om_frcgap_step(&g, om_ut_frc())) { om_sd_gap_late++; return; }
}

void ss_main(void)
{
    char sum[3][41];
    int rc, i;
    snapshot_vdp1();
    {   /* before anything else: the tool's armed block in sound RAM (handoff.h), copied out and cleared there */
        static om_retblk raw;
        om_ret_copy16(&raw, (const volatile void *)OM_SND_RETURN);
        om_ret_copy16((volatile void *)OM_SND_RETURN, 0);
        g_om_ret_ok = om_ret_take(&raw, &g_om_ret);
    }
    slInitSystem(TV_320x224, om_tex, 1);
    om_exit_set_silence(om_audio_handoff);               /* review-final-m3 I1: both error-screen exits and the menu's
                                                          * A+B+C+Start key every slot off first (guarded until SNDOFF) */
    om_text_clear();
    if ((rc = om_art_init()) != 0)
        om_fatal("Art init failed (JP LUT, scheduler)", rc);
    if ((rc = om_logo_init()) != 0)
        om_fatal("Logo asset has the wrong size", rc);
    om_v2_init();                                              /* plan D4: NBG1 bitmap, NBG2 panels, CRAM */
    {
        om_theme t;
        om_theme_builtin(&t, OM_THEME_SATURN);                 /* themed from the first frame; SETTINGS.INI later */
        om_v2_bg_gradient(t.bg_top, t.bg_bottom);
        om_v2_apply_colors(&t);
    }
    /* 2026-11-20 hardware report: om_v2_init shows NBG1/NBG2, so the gradient just drawn appeared as a solid blue
     * screen for the ~1 s om_audio_boot spends reading SOUND.BIN, between the license screen and the animation.
     * Hidden here until the interstitial's fade-in shows them again (interstitial.c, om_istl_fade_in); om_bootplay
     * keeps them off too. The back screen stays black. */
    slScrDisp(NBG0ON | NBG1OFF | NBG2OFF | NBG3OFF | RBG0OFF);
    slBack1ColSet((void *)BACK_CRAM, 0x0000);
    slGetStatus();                                       /* HWF-16, HWF-40: status + pads from here on (the intro's skip
                                                          * reads the pad; the area code is read two slSynch later) */
    {   /* the boot animation streams from the disc before API mode; it borrows the card-scan
         * work area (untouched until om_card_load, zeroed again by om_bootplay). A failure only skips it. */
        char line[41];
#ifdef OMSAT_FAKECARD
        om_fakesat_mount(om_fakecard, (uint32_t)(om_fakecard_end - om_fakecard));
        om_boot_diag_set(om_fakesat_has("aether/FAKESCRIPT.DIAG"));   /* counters off-screen by default */
#endif
        /* BIGLOGO.BIN (the interstitial's and the saver's logo) rides along, into LWRAM (docs/ledger.md) */
        om_disc_file blg = { "BIGLOGO.BIN", (uint8_t *)OM_CT(OM_LW_BIGLOGO), OM_LW_BIGLOGO_MAX, 0, 0 };   /* HWF-12 */
        om_audio_boot((uint8_t *)&s_work, (uint32_t)sizeof s_work, &blg);   /* SOUND.BIN (the chant) */
        om_istl_set_logo((const uint8_t *)OM_CT(OM_LW_BIGLOGO), blg.len, blg.rc);
        /* the intro's frame counter: the fake build with FAKESCRIPT.DIAG only (Task 13d's toggle) */
        if (!g_om_ret_ok)                                /* back from a tool: no intro, straight to the library */
            om_bootplay((uint8_t *)&s_work, (uint32_t)sizeof s_work, om_boot_diag());
        om_bootplay_line(line, 0);                       /* named, non-fatal: the menu boots without the intro */
        if (line[0]) {
            logline(line);
            strcpy(s_intro_msg, line);                   /* and a toast in the menu (no boot text on screen) */
        }
#ifdef OMSAT_FAKECARD
        if (om_boot_diag()) {                            /* Task 13b's measurements; NBG0 has no color offset */
            om_text_put(0, 0, "Aether M3", 40);
            om_text_put(0, 1, "FAKE CARD BUILD - NOT FOR A SATURN", 40);
            om_bootplay_line(line, 1);
            om_text_put(0, 25, line, 40);
            om_bootplay_line(line, 2);
            om_text_put(0, 26, line, 40);
            om_bootplay_line(line, 3);
            om_text_put(0, 27, line, 40);
            om_bootplay_line(line, 4);                   /* restart / re-sync detail (HW report 2026-11-19) */
            om_text_put(0, 24, line, 40);
        }
#endif
    }
#ifdef OMSAT_FAKECARD
    {   /* Task 13c round 2: LOADBG.BIN from the fake disc into LWRAM's snapshot range (the fake build never hands off).
         * read while the screen is still black from the animation, before the interstitial (a ~2 s CD
         * read with no frames in between would freeze its fade-in) */
        char line[41], n[12];
        int r = om_fakesat_cd_file("LOADBG.BIN", "/aether/LOADBG.BIN", (uint8_t *)OM_CT(OM_LW_SNAPSHOT),
                                   OM_VDP1_BYTES);
        strcpy(line, "FAKE: LOADBG.BIN FROM CD rc ");
        strcat(line, om_itoa(n, r));
        logline(line);
    }
#endif
    /* Rulings M3-R40..R42: from the animation's black, the interstitial (menu/views/interstitial.c) and the
     * per-step boot timing (boottime.h); its scratch is the card-scan work area, handed back zeroed */
    om_fatal_set_hook(boot_fatal);
    om_istl_set_vblank_chain(om_audio_vblank);           /* the interstitial's V-blank runs the audio handler */
    if ((rc = om_istl_begin((uint8_t *)&s_work, (uint32_t)sizeof s_work)) != 0) {
        char line[41];                                   /* the logo stays blank, named (as NO ART is) */
        om_msg_rc(line, sizeof line, "LOGO: BIGLOGO.BIN ", rc == OM_ISO_E_NONE ? "not on the disc" : om_strerror(rc),
                  rc);
        logline(line);                                   /* shown above a later fatal */
        if (!s_intro_msg[0]) strcpy(s_intro_msg, line);  /* and the menu's first status line */
    }
#ifdef OMSAT_FAKECARD
    if (om_fakesat_has("aether/FAKESCRIPT.SLOWBOOT"))
        om_istl_slow(40);                                /* captures: ~0.7 s a step, like the card on hardware */
#endif
    om_istl_fade_in();                                   /* logo + background up (>= two slSynch before the area code,
                                                          * HWF-16) before s_mode and the library block */
    g_area_code = Smpc_Status->area;
    logline("Switching Satiator to API mode...");
    om_boot_step_begin(OM_BT_SMODE);
    rc = s_mode(s_api);                                  /* HWF-22 */
    om_boot_step_end(OM_BT_SMODE);
    if (rc != 0) {
        /* fail() never returns (om_fatal: an infinite slSynch loop); the
         * `else` makes it explicit that exactly one of these two messages is ever shown. */
        if (rc == SAT_ETIMEDOUT)                          /* bounded exec_cmd wait, satiator.c */
            fail(sat_timeout_msg("s_mode"), rc);
        else
            fail("Satiator API mode failed (s_mode)", rc);
    }
    om_fw_capture();                                     /* judged once SETTINGS.INI is known (folders.c) */
    om_sd_gap = sd_gap;                                  /* before any SD write (sd.h) */
    om_boot_step_begin(OM_BT_LIBRARY);
    om_satfs_set_progress(scan_progress);
    rc = om_card_load(&g_card, &g_bufs, status, NULL);   /* paced by the loading screen (yield.h, interstitial.c) */
#ifdef OMSAT_FAKECARD
    om_istl_gt_phase(0x30);                              /* the boot-gap trace: the summary and the folder tree */
#endif
    if (rc != 0) {
        if (rc == OM_SD_TIMEOUT)                          /* sd.h; see satiator.h SAT_ETIMEDOUT */
            fail(sat_timeout_msg(s_last_step), rc);
        else if (rc == OM_E_FULL)
            fail(too_big_msg(), rc);
        else
            fail("Card load failed (root listing or scan)", rc);
    }
    om_card_summary(&g_card, sum);
    for (i = 0; i < 3; i++)
        logline(sum[i]);
    if ((rc = om_tree_build(&s_tree, &g_card.lib)) != 0)
        fail("TREE: >1024 DIRS, >2048 GAMES OR DEPTH>8", rc);       /* 40 cols (HWF-24); rc on fatal.c's code line */
    om_boot_step_end(OM_BT_LIBRARY);
    om_istl_counts(om_libidx_games(&g_card.lib), g_card.lib.nfolders, g_card.art_rc ? 0u : g_card.art.count);
    /* the 1.5 s hold on the load summary is gone; a scan says so in the menu's status line */
    om_folders_status(g_card.lib_source != OM_LIB_FROM_IDX ? sum[2] : s_intro_msg);
    om_folders_run(&g_card, &s_tree, g_area_code, &g_bufs);
}
