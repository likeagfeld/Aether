/* menu/launcher/lcheat.h -- LAUNCHER.BIN's cheat side: the launch-time load of a
 * game's CHEATS.PAK record into Kai's plan (chkai.h), and the cheat editor screen (chedit.h) the menu hands off to with
 * OM_HO_CHEATS. BSD-3-Clause. */
#ifndef OM_LCHEAT_H
#define OM_LCHEAT_H
#include "chkai.h"
#include "handoff.h"
#include "lseq.h"

#define OM_LCH_PATH "/aether/CHEATS.PAK"   /* menu/app/sd.h OM_PATH_CHEATPAK (SPEC §5.15) */

/* In API mode: h->ckey's record -> *p (p->status: OM_CHS_*; OFF when h->cheats is 0, NEED_CWX under JHL). Writes the
 * one-line result at (x, y). Never stops the launch. */
void om_lch_load(const om_handoff *h, om_chk_plan *p, int x, int y);
/* the OM_HO_CHEATS mode: the editor for h->ckey, saved on B, then a console reset to Aether */
int om_run_cheats(const om_handoff *h, om_lerr *e);
#endif
