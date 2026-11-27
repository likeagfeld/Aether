/* menu/views/folders.h -- the Folders view (spec §7.4), the only view. BSD-3-Clause. */
#ifndef OM_FOLDERS_H
#define OM_FOLDERS_H
#include "sgl.h"
#include "card.h"
#include "tree.h"
void om_folders_status(const char *msg);
/* review-sound I1: a status not caused by input (the music stream's errors): never replaces a visible error or a status
 * not yet shown (a launch outcome, a SOUND.BIN boot message); then it waits in the one-entry queue for the next press */
void om_folders_status_soft(const char *msg);
void om_folders_run(om_card *c, om_tree *t, Uint8 area_code, const om_card_bufs *bufs);
/* frame-miss counters since boot, for the hidden diagnostics screen (menu/views/diag.c). worst_tag is
 * one of 'I','T','S','N','A','R','D' (PH_INPUT..PH_DRAW, folders.c's own enum). */
void om_folders_miss_total(uint32_t *total, char *worst_tag, uint32_t *worst_count);
#endif
