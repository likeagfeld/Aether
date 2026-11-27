/* menu/app/fatal.c -- see fatal.h. BSD-3-Clause. */
#include "sgl.h"
#include "satiator.h"
#include "fatal.h"
#include "pad.h"
#include "padgest.h"
#include "text.h"

#define OM_SYSRES_WAIT 120        /* frames: SYSRES takes "100 msec +" (SMPC_Manual.txt:1224-1243); 2 s, then named */

static void (*s_silence)(void);

void om_exit_set_silence(void (*fn)(void)) { s_silence = fn; }

static void silence(void)
{   /* review-final-m3 I1: every slot keyed off and the common word written before the menu is left */
    if (s_silence)
        s_silence();
}

void om_return_official(void)
{   /* HWF-58: on Satiator FW163 the BIOS get_mpeg_rom load of the official menu (s_reset_to_satiator) fails with -67 on
     * hardware, as Rings issue #16 and Kai v6.464 found from FW158 on. User decision 2026-11-20: A+B+C+Start resets the
     * console instead (as Kai does); the Satiator then boots its official menu, which autoboots Aether again. */
    for (;;)
        om_reset_console();   /* returns only on failure, named on row 27 */
}

int om_reset_console(void)
{   /* HWF-53: SMPC SYSRES (command 0DH, SMPC_Manual.txt:1224-1243: "Resets the entire SATURN system ... starts up the
     * boot ROM"), through SGL's own SMPC queue (slSystemReset, SL_DEF.H:1544), which issues it from SGL's V-blank code
     * and so keeps the manual's "300 us after V-BLANK-IN and before the next V-BLANK-IN" rule (SMPC_Manual.txt:949-950)
     * and never collides with SGL's per-frame INTBACK. What the Satiator does on a SYSRES (restart as at power-on, so
     * its official menu and then autoboot.iso) is UNVERIFIED: HWF-53 is emulator-tier, the hardware check asks. */
    char num[12];
    int i;
    silence();
    om_text_set_inset(0);                            /* DEFAULT 4: the fixed 40-column layout */
    om_text_put(0, 27, "Resetting the console...", 40);
    slSynch();
    if (!slSystemReset()) {
        om_text_put(0, 27, "RESET CONSOLE FAILED: SMPC QUEUE FULL", 40);
        return -1;
    }
    for (i = 0; i < OM_SYSRES_WAIT; i++)
        slSynch();
    om_text_put(0, 27, "RESET CONSOLE FAILED: NO RESET rc=", 40);
    om_text_put(34, 27, om_itoa(num, -2), 6);
    return -2;
}

static void (*s_hook)(void);

void om_fatal_set_hook(void (*fn)(void)) { s_hook = fn; }

void om_fatal(const char *what, int code)
{
    char num[12];
    om_cgest g;
    om_text_set_inset(0);                            /* DEFAULT 4: the fixed 40-column layout */
    if (s_hook) {
        void (*h)(void) = s_hook;
        s_hook = 0;                                  /* once: a fault inside the hook does not recurse */
        h();
    }
    om_text_put(0, 23, "FATAL ERROR", 40);
    om_text_put(0, 24, what, 40);
    om_text_put(0, 25, "code", 5);
    om_text_put(5, 25, om_itoa(num, code), 35);
    om_text_put(0, 26, "A+B+C+START or C: reset console", 40);
    om_text_put(0, 27, "", 40);
    om_cgest_init(&g, om_pad_held());                /* a button held as the screen appears is not a press */
    for (;;) {
        Uint16 held;
        slSynch();
        held = om_pad_held();
        if (om_pad_soft_reset(held))
            om_return_official();
        if (om_cgest_step(&g, held, PER_DGT_TC))
            om_reset_console();                      /* returns only on failure, named on row 27 */
    }
}
