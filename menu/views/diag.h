/* menu/views/diag.h -- the hidden diagnostics screen. One hardware session is meant to give every
 * measurement the old S0-S3 spike discs would have. Entry is a hidden combo in the Folders view
 * (hold L+R, then press Start; menu/views/folders.c's own hook); the screen itself is plain NBG0 text
 * (menu/ui/text.h, HWF-24), three pages switched with L/R (four on the --sdtest disc, where page 4 is the SD
 * stress test: A arms it, A again runs it; M6 Task 21), any other button returns to the menu. Every measurement is
 * time-bounded; a failure shows its own named rc, never hangs. BSD-3-Clause. */
#ifndef OM_DIAG_H
#define OM_DIAG_H
#include <stdint.h>
#include "loading.h"   /* om_load_out: the only exported path to loading.c's (file-static) background counters */

/* folders.c calls this right after every om_loading_run (win, cancel or fail) so the diag screen can show "the
 * last run's" loading-background counters (13c) on its next page. loading.c itself is not touched by this task;
 * om_load_out is already exported to the caller there, this just keeps a copy past the caller's own scope. */
void om_diag_record_load(const om_load_out *out);

/* The combo's handler, called from menu/views/folders.c's input loop once L+R are held and Start is freshly
 * pressed. area: the Folders view's own `area` parameter (the card's region byte; no new global needed). work /
 * work_cap: the card-scan work area (om_card_bufs.work / sizeof *work) -- free scratch whenever the Folders loop
 * is polling input (the only place this can be called from; main.c's own card load has long finished and no
 * launch is in flight). At most the first 8 KB of it is used (aligned(4)), for the SD read-throughput and
 * write-sanity tests and page 4's stress test; nothing here claims new static HWRAM for that purpose. Returns once a
 * button other than L/R (reserved for paging) is freshly pressed; never hangs or returns early on a failure --
 * every failure is shown with its own rc instead. */
void om_diag_run(uint8_t area, void *work, uint32_t work_cap);
#endif
