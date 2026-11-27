/* menu/launcher/lseq.h -- the launch sequence (spec §7.1 launch step 3). BSD-3-Clause. */
#ifndef OM_LSEQ_H
#define OM_LSEQ_H
#include <stdint.h>
#include "handoff.h"
#include "jhboot.h"
/* trace: step 9's boot trace for the error screen (om_boot_last for JHL, om_cwx_last for CWX); NULL before step 9 */
typedef struct { int step, rc; const char *what, *detail; const om_boot_trace *trace; } om_lerr;
void om_restore_vdp1(void);
void om_reset_interrupts(void);
/* the hand-off block (v2) drives the launch: dir, image, area, flags, the loader (step 9: om_boot_disc
 * for JHL, om_cwx_boot for CWX) and igr (arm the in-game reset). */
int om_run_launch(const om_handoff *h, om_lerr *err);
/* 2026-11-25: the way back from a tool (the cheat editor, Manage saves, the import) without a console reset -- the
 * menu disc (menu: a JHL block for "/" + autoboot.iso, no IGR) run as a launch: no save swap, no cheats, the tool's
 * screens kept with a "returning" page. Returns only on failure, as om_run_launch. */
int om_run_return(const om_handoff *menu, om_lerr *err);
/* lmain.c: a tool's exit -- the return block armed (handoff.h om_retblk: the menu reopens where it was), then
 * om_relaunch (relaunch.h) with om_run_return, and SYSRES if any step of it fails. Returns only if SYSRES failed. */
int om_go_home(const om_handoff *h, om_lerr *e);
int om_lseq_step(void);                                       /* the last step announced (for the EXC screen) */
void om_fail_screen(const om_lerr *e) __attribute__((noreturn));   /* lmain.c: error screen + exits */
#endif
