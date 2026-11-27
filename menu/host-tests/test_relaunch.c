/* menu/host-tests/test_relaunch.c -- the error screen's exit A (menu/launcher/relaunch.c) over fake Satiator ops.
 * Hardware 2026-11-24 (NiGHTS, CWX read -43): exit A sat on "re-launching VibeMenu..." for a long time, then showed
 * "LAUNCH FAILED at step 2 / Cannot enter game folder / rc -20". s_mode(s_api)'s result was ignored and libsatiator
 * caches the mode, so step 1 "passed" without a handshake and s_chdir was the first real command: it timed out.
 * The rule: leave CD mode (s_mode(s_api), retried), stop the emulation, chdir "/", then launch -- every result
 * checked -- and on any failure a console reset (SYSRES), whose Satiator menu then autoboots autoboot.iso.
 * BSD-3-Clause. */
#include "th.h"
#include "../launcher/relaunch.c"

#define SAT_ETIMEDOUT (-20)

static char trace[256];
static int mode_rc[8], n_mode, emu_rc, chdir_rc, run_rc, sysres_rc, has_iso;
static const char *ran_image;

static void add(const char *s) { strncat(trace, s, sizeof trace - strlen(trace) - 1); }
static void f_cd_stop(void) { add("S"); }
static int f_mode(void) { add("M"); return mode_rc[n_mode < 8 ? n_mode++ : 7]; }
static int f_emu(void) { add("E"); return emu_rc; }
static int f_chdir(void) { add("C"); return chdir_rc; }
static int f_exists(const char *p) { (void)p; return has_iso; }
static int f_run(const char *image) { add("L"); ran_image = image; return run_rc; }
static int f_sysres(void) { add("R"); return sysres_rc; }
static void f_say(const char *w, int rc) { (void)w; (void)rc; }

static const om_relaunch_ops ops = { f_cd_stop, f_mode, f_emu, f_chdir, f_exists, f_run, f_sysres, f_say };

static void reset(void)
{
    trace[0] = 0;
    memset(mode_rc, 0, sizeof mode_rc);
    n_mode = emu_rc = chdir_rc = 0;
    run_rc = -1;                                   /* om_run_launch returns only on failure */
    sysres_rc = -99;                               /* SYSRES returns only on failure */
    has_iso = 1;
    ran_image = NULL;
}

int main(void)
{
    int rc;

    /* the user's case: API mode "succeeds" (cached), chdir times out -> a console reset, not the menu launch */
    reset();
    chdir_rc = SAT_ETIMEDOUT;
    rc = om_relaunch(&ops);
    CHECK_STR(trace, "SMECR");
    CHECK_INT(rc, -99);

    /* s_mode(s_api) fails twice, then answers: retried, then the rest */
    reset();
    mode_rc[0] = -1; mode_rc[1] = -1; mode_rc[2] = 0;
    run_rc = -5;
    om_relaunch(&ops);
    CHECK_STR(trace, "SMMMECLR");                  /* the menu launch failed too: reset */

    /* every attempt fails: reset, no SD command after the mode switch */
    reset();
    mode_rc[0] = mode_rc[1] = mode_rc[2] = mode_rc[3] = -1;
    om_relaunch(&ops);
    CHECK_STR(trace, "SMMMR");

    /* a timeout is not retried: each costs SAT_HIRQ_WAIT_CAP polls, ~28 s per wait on the console (HWF-54) */
    reset();
    mode_rc[0] = SAT_ETIMEDOUT;
    om_relaunch(&ops);
    CHECK_STR(trace, "SMR");

    /* s_emulate("") fails: reset */
    reset();
    emu_rc = SAT_ETIMEDOUT;
    om_relaunch(&ops);
    CHECK_STR(trace, "SMER");

    /* hardware 2026-11-25: FW163 answers s_emulate("") with -6 (FR_INVALID_NAME) and the emulation still ends -- go on to
     * "/" and the menu disc, as the IGR stub does; it used to reset with "EMULATION STOP FAILED rc -6" */
    reset();
    emu_rc = -6;
    om_relaunch(&ops);
    CHECK_STR(trace, "SMECLR");
    CHECK_STR(ran_image, "autoboot.iso");

    /* all fine: the menu disc in "/" (the .iso if present, else the .cue); its launch returns only on failure */
    reset();
    om_relaunch(&ops);
    CHECK_STR(trace, "SMECLR");
    CHECK_STR(ran_image, "autoboot.iso");
    reset();
    has_iso = 0;
    om_relaunch(&ops);
    CHECK_STR(ran_image, "autoboot.cue");

    printf("test_relaunch: %d passed, %d failed\n", th_pass, th_fail);
    return th_fail || !th_pass;
}
