/* menu/src/yield.h -- the start-up screen's pacing hook. Long start-up work calls om_yield() before each Satiator
 * command (sd.c, satfs.c, themes.c, sdwrite.c) and every few dozen items of its long CPU loops (msort.c, libsort.c,
 * tree.c); while the loading screen runs, interstitial.c points om_yield_fn at its pacer, which draws a frame once
 * ~9 ms or a V-blank have passed since the last, so the time-based comet moves every V-blank. The hook issues no SD
 * command (a directory listing in progress is not disturbed). Unset (the default, the menu, the launcher, the host
 * tests unless they set it) om_yield() is one load and a branch. BSD-3-Clause. */
#ifndef OM_YIELD_H
#define OM_YIELD_H
#include <stdint.h>
extern void (*om_yield_fn)(void);
static inline void om_yield(void) { if (om_yield_fn) om_yield_fn(); }
/* The launch's eye (hardware 2026-11-27, Aether 0.1: it stalled through the save swap, then ran at full speed in the
 * last half second): lseq.c points om_yield_fn at its pacer from the loading screen's start to step 4, and the save
 * swap's code (pgsrun.c, pgsbak.c, savegen.c, sdwrite.c) and the launcher's stdio (lsys.c) call om_yield() before each
 * Satiator path command and each 2 KB read, between two whole libsatiator calls; the pacer then draws and may read
 * LOADBG.BIN (its own file). Inside a directory listing (between an s_opendir and its last s_stat(NULL)) no other SD
 * command may run, so a listing's long loop calls om_yield_draw() instead: om_yield_draw_fn draws only. Unset (the
 * menu, the host tests unless they set it) it is one load and a branch. */
extern void (*om_yield_draw_fn)(void);
static inline void om_yield_draw(void) { if (om_yield_draw_fn) om_yield_draw_fn(); }
/* Files open for writing now (sdwrite.c's writes, lsys.c's write handles): while any is, the launch's pacer draws only
 * -- no LOADBG.BIN read lands between a file's open for write and its close (the old rule, kept until a hardware
 * session shows a read on a second handle mid-write is safe on the Satiator's firmware) */
extern volatile uint8_t om_yield_wr;
#endif
