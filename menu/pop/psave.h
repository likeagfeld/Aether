/* menu/pop/psave.h -- the Manage saves popup's model (mockup 2026-11-25, popups.html): the tabs Console / Cartridge /
 * Card over svmgr.h's operations, which it only calls -- every rule stays theirs (the add-only card, the .BKP before a
 * console save goes or is written over, SD -> console only into this game's set or the general one, nothing written
 * while a save swap is unfinished). Pure C over svmgr; host-tested by test_psave.c over fake_bram.c and
 * fake_satiator.c. The view (pdraw.c) only reads it.
 *  - LIST: Left/Right change the tab (Console, Cartridge, Card); Up/Down the row; A on a save turns the side panel
 *    into its options; on the Card tab the rows are first its save sets (newest first, the console's own tagged
 *    "loaded", alternates as their own row), A opens a set, B goes back to the sets. B on the top level closes the
 *    popup (Settings' manager: back to the game list).
 *  - ACT: the save's options (Copy to the card, Copy to the console / the cartridge, Back up now, Delete...). Delete
 *    asks twice (the row reads "Press A again"); a copy over a save of the same name asks the same way. Any move or
 *    B disarms; B goes back to the list.
 *  - GAMES (Settings > Manage / Import Saves): row 0 is "Import from Rings / Kai" (A: OM_PS_IMPORT, the popup closes
 *    and the menu runs the import picker and its hand-off), then the card's save folders (grow 1.. is fold[grow - 1]);
 *    A opens one (its Card tab), B closes.
 * An operation runs one frame after A (om_ps_pending / om_ps_run), so the view can show "Working..." first. BSD-3-Clause. */
#ifndef OM_PSAVE_H
#define OM_PSAVE_H
#include <stdint.h>
#include "svmgr.h"

#define OM_PS_ROWS   300u                 /* list rows (svmgr's items, or sets) */
#define OM_PS_MSG    80u
enum { OM_PS_LIST, OM_PS_ACT, OM_PS_GAMES };
enum { OM_PSA_TO_CARD, OM_PSA_TO_CON, OM_PSA_TO_CART, OM_PSA_BACKUP_ALL, OM_PSA_DELETE, OM_PSA_N };
enum { OM_PSM_NONE, OM_PSM_OK, OM_PSM_ERR, OM_PSM_ASK };
/* the pad (SGL PER_DGT_*, as chedit.h's OM_CK_*) */
#define OM_PS_K_R (1u << 15)
#define OM_PS_K_L (1u << 14)
#define OM_PS_K_D (1u << 13)
#define OM_PS_K_U (1u << 12)
#define OM_PS_K_A (1u << 10)
#define OM_PS_K_B (1u << 8)
#define OM_PS_K_REP 1u                    /* with U/D: a held key's repeat (stops at the ends) */
/* om_ps_key's result */
#define OM_PS_CLOSE 1
#define OM_PS_MOVED 2
#define OM_PS_OK    4
#define OM_PS_BACK  8
#define OM_PS_IMPORT 16                   /* GAMES row 0 chosen: close, then the menu's import */

typedef struct { uint8_t set, alt, loaded; uint16_t n; uint32_t newest; } om_ps_set;   /* a Card-tab set row */

typedef struct {
    om_sv *v;
    uint8_t page, tab, sdlev, armed, global, pending, msgk;
    uint16_t row, top, vis, nrow, act, nact, grow, gtop;
    uint8_t acts[OM_PSA_N];
    uint16_t map[OM_PS_ROWS];             /* a list row's item (svmgr's v->it) or set (sets[]) */
    om_ps_set sets[2u * OM_SV_SETS];
    uint16_t nsets, cur_set;              /* the open set (sdlev 1) */
    int list_rc;                          /* the tab's listing: 0, or why it is empty (a refusal or an error) */
    om_sv_item sel;                       /* the save the options are for (a copy: the list is re-read after) */
    char (*fold)[OM_PGS_DIR];             /* GAMES: the card's save folders */
    uint32_t nfold, fcap;
    char msg[OM_PS_MSG];
} om_ps;

/* v: om_sv_open done. global: Settings' manager (fold/fcap: the folder table; the GAMES page first) */
void om_ps_open(om_ps *p, om_sv *v, int global, char (*fold)[OM_PGS_DIR], uint32_t fcap, uint16_t vis);
uint32_t om_ps_key(om_ps *p, uint16_t keys);
int om_ps_pending(const om_ps *p);        /* an operation is armed to run (the view shows "Working...") */
void om_ps_run(om_ps *p);                 /* runs it; the message says how it went; the lists are read again */
/* the list's rows, for the view: a save (*it set) or a set (*st set); NULL both past the end */
void om_ps_row(const om_ps *p, uint16_t r, const om_sv_item **it, const om_ps_set **st);
const char *om_ps_act_label(const om_ps *p, uint16_t i);   /* "Copy to the card", ..., "Press A again" */
const char *om_ps_folder(const om_ps *p, uint16_t i);      /* save folder i (GAMES row i + 1), or NULL */
/* the open set's saves count etc. are in sets[]; the device the tab shows (OM_SV_*) */
static inline int om_ps_dev(const om_ps *p) { return p->tab; }
#endif
