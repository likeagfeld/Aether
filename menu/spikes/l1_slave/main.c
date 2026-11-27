/* L1 slave gate (emulator tier): does the slave SH-2 stop at the hand-off? Replays the menu's SMPC state
 * -- main.c's slGetStatus() at boot (HWF-40: SGL's persistent per-frame INTBACK from then on) -- then copies the L1
 * LAUNCHER.BIN to 0x00200000, writes a test hand-off block and calls the shipped om_launch_jump (menu/app/launchj.c),
 * the menu's own jump. The L1 launcher (OM_L1_HARNESS) sees the l1gate.h request and halts twice: phase 1 at its entry
 * (what the menu side left) and phase 2 before step 9's boot (what a game would get). tools/omsat/slavegate.py reads
 * SMPC SlaveSH2On from a Mednafen savestate in each phase (tools/emucap.ps1 -StateTimes). No button is needed. */
#include <string.h>
#include "spk.h"
#include "handoff.h"
#include "omlayout.h"
#include "settings.h"
#include "l1gate.h"

extern Uint8 om_launcher_bin[], om_launcher_bin_end[];
void om_launch_jump(void);
#define L1G ((volatile Uint32 *)OM_CT(OM_L1G_BASE))

void ss_main(void)
{
    om_lnch_info li;
    om_launch_opts o;
    Uint32 n = (Uint32)(om_launcher_bin_end - om_launcher_bin), i, f;
    volatile Uint8 *dst = (volatile Uint8 *)OM_CT(OM_LAUNCHER_LOAD);
    om_handoff *h = (om_handoff *)OM_CT(OM_LW_HANDOFF);
    spk_init("L1 SLAVE GATE (emulator-tier)");
    slGetStatus();                                   /* menu/app/main.c: status + pads from here on (HWF-40) */
    for (f = 0; f < 120u; f++) slSynch();            /* 2 s of menu frames with the INTBACK running */
    if (om_lnch_parse(om_launcher_bin, n, &li)) {
        spk_line(5, "LAUNCHER.BIN HEADER REJECTED");
        for (;;) slSynch();
    }
    for (i = OM_LNCH_HDR; i < n; i++)
        dst[i - OM_LNCH_HDR] = om_launcher_bin[i];
    memset(&o, 0, sizeof o);
    o.loader = OM_LDR_JHL;
    om_handoff_fill2(h, "/", "autoboot.iso", 4, OM_HO_TEST_NO_SATIATOR | OM_HO_NO_IGR, &o);
    L1G[1] = 0; L1G[2] = 0xFFFFFFFFu; L1G[3] = 0;
    L1G[0] = OM_L1G_REQ;
    spk_line(5, "JUMPING (om_launch_jump)");
    om_launch_jump();
}
