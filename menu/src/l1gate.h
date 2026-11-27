/* menu/src/l1gate.h -- the slave-SH-2 hand-off gate's LWRAM words (emulator tier, test builds only).
 * menu/spikes/l1_slave writes the request; LAUNCHER.BIN reads it only when built with OM_L1_HARNESS (sbuild.py
 * build_launcher(l1=True)), so the release launcher carries none of this. The words sit at OM_LW_HANDOFF + 0x810,
 * outside the 536-byte hand-off block and past l1_lbtest's boot counter (+0x800..+0x807).
 * Phase 1 (OM_L1G_PRE): the launcher's entry, before its own SSHOFF -- the slave state the menu side left.
 * Phase 2 (OM_L1G_HALT): just before step 9's boot -- the slave state the game would get. tools/omsat/slavegate.py
 * reads both from Mednafen savestates. BSD-3-Clause. */
#ifndef OM_L1GATE_H
#define OM_L1GATE_H
#include "omlayout.h"
#define OM_L1G_BASE    (OM_LW_HANDOFF + 0x810u)
#define OM_L1G_REQ     0x534C5647u     /* "SLVG": word 0, set by the gate harness */
#define OM_L1G_PRE     0x50524531u     /* "PRE1": word 1 while phase 1 waits */
#define OM_L1G_HALT    0x484C5444u     /* "HLTD": word 1 once halted before step 9 */
#define OM_L1G_FRAMES  600u            /* phase 1 lasts 600 V-blanks (~10 s) */
/* word 2: the launcher's om_smpc_sshoff rc (0 = slave off); word 3: "LSSH" once the launcher has issued it */
#define OM_L1G_LSSH    0x4C535348u
/* l1_lbtest's CWX self-boot arms Kai's cheat path with one 0x01 code writing 0xC0DE here every interrupt
 * (lseq.c, OM_L1_HARNESS only); the harness shows the word once it is back (HWRAM, cleared by the CWX boot) */
#define OM_L1_CHEAT_WORD 0x060E0000u
#endif
