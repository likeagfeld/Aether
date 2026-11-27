/* menu/launcher/relaunch.c -- see relaunch.h. Pure sequencing over the ops (host-tested: test_relaunch.c).
 *
 * Hardware 2026-11-24 (NiGHTS, a CWX read that got no sectors): exit A used to ignore s_mode(s_api)'s result and go
 * straight to om_run_launch. libsatiator caches its mode, so step 1 then "passed" without a handshake, and step 2's
 * s_chdir was the first real command: it timed out (-20) after a long wait. Every libsatiator timeout is
 * SAT_HIRQ_WAIT_CAP = 20,000,000 polls, about 28 s on the console (HWF-54: 85.6 M polls took ~120 s), so a chain of
 * them read as a hang. Now: the CD play is stopped first, each step's result is checked in order, a timed-out mode
 * switch is not retried, and any failure ends in SMPC SYSRES -- the Satiator's own menu then autoboots autoboot.iso
 * (HWF-58), which is the same destination. BSD-3-Clause. */
#include "relaunch.h"

#define SAT_ETIMEDOUT_RC (-20)                /* satiator.h SAT_ETIMEDOUT */
/* s_emulate("") answers -FR_INVALID_NAME (-6) on FW163 every time, and the emulation still ends (hardware igr=e,
 * 2026-11-20, every try on three games; igrstub.c ignores it the same way). Hardware 2026-11-25 (Mortal Kombat II,
 * exit A after a step-1 exception): exit A took that -6 for a failure, showed "SATIATOR: EMULATION STOP FAILED rc -6"
 * and reset the console instead of starting Aether. */
#define EMU_OFF_OK_RC (-6)

int om_relaunch(const om_relaunch_ops *o)
{
    int rc = 0, i;
    o->say("re-launching Aether: stopping the CD...", 0);
    o->cd_stop();
    o->say("re-launching Aether: Satiator API mode...", 0);
    for (i = 0; i < OM_RELAUNCH_TRIES; i++) {
        if ((rc = o->mode_api()) == 0 || rc == SAT_ETIMEDOUT_RC)
            break;
    }
    if (rc) {
        o->say("SATIATOR API MODE FAILED", rc);
        goto reset;
    }
    o->say("re-launching Aether: ending the emulation...", 0);
    if ((rc = o->emulate_off()) < 0 && rc != EMU_OFF_OK_RC) {
        o->say("SATIATOR: EMULATION STOP FAILED", rc);
        goto reset;
    }
    if ((rc = o->chdir_root()) < 0) {
        o->say("SATIATOR: CARD ROOT NOT REACHED", rc);
        goto reset;
    }
    rc = o->run(o->exists("/autoboot.iso") ? "autoboot.iso" : "autoboot.cue");
    o->say("AETHER DID NOT START", rc);
reset:
    o->say("resetting the console...", rc);
    return o->sysres();
}
