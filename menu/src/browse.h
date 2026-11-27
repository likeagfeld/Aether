/* menu/src/browse.h -- the Folders view model with spec §7.4's virtual roots above the card tree, plus sort and
 * filter (§7.6). Replaces direct om_tree navigation in the view; om_tree still holds the card structure. BSD-3-Clause. */
#ifndef OM_BROWSE_H
#define OM_BROWSE_H
#include <stdint.h>
#include "gamesdb.h"
#include "libidx.h"
#include "libsort.h"
#include "plist.h"
#include "tree.h"

enum { OM_IT_FOLDER = 0, OM_IT_GAME = 1, OM_IT_VROOT = 2, OM_IT_VSUB = 3 };
enum { OM_VR_FAVS, OM_VR_RECENT, OM_VR_AZ, OM_VR_GENRE, OM_VR_REGION, OM_VR_COUNT };
enum { OM_LOC_CARD, OM_LOC_VROOT, OM_LOC_VSUB };
#define OM_VIEW_CAP    (OM_MAX_FOLDERS + OM_MAX_ENTRIES + OM_VR_COUNT)
#define OM_SUB_UNKNOWN 0xFFu

typedef struct { uint8_t kind, vroot; uint16_t arg; } om_loc;

typedef struct {
    const om_tree *tree;
    const om_libidx *lib;
    const om_sortctx *sort;
    const om_gamesdb *db;
    const om_plist *favs, *recent;
    uint8_t sort_mode, filter_region;
    uint16_t filter_genre;
    om_loc loc, stack[OM_MAX_DEPTH + 2];
    uint8_t depth;
    uint16_t n, cur, top;
    om_item view[OM_VIEW_CAP];
    uint16_t ccur[OM_MAX_FOLDERS], ctop[OM_MAX_FOLDERS];
    uint16_t vcur[OM_VR_COUNT + 1], vtop[OM_VR_COUNT + 1];     /* [OM_VR_COUNT]: the bucket list last entered */
    uint16_t scratch[OM_MAX_ENTRIES], tmp[OM_MAX_ENTRIES];
} om_browse;

void om_browse_init(om_browse *b, const om_tree *t, const om_libidx *l, const om_sortctx *s, const om_gamesdb *db,
                    const om_plist *favs, const om_plist *recent);
void om_browse_set(om_browse *b, uint8_t sort_mode, uint16_t filter_genre, uint8_t filter_region);
void om_browse_refresh(om_browse *b);
const om_item *om_browse_items(const om_browse *b, uint16_t *n);
const om_item *om_browse_selected(const om_browse *b);
void om_browse_move(om_browse *b, int delta);
void om_browse_step(om_browse *b, int dir, int wrap);   /* Up/Down: dir -1 or 1; wrap = a fresh press */
void om_browse_page(om_browse *b, int dir);
int om_browse_enter(om_browse *b);
int om_browse_back(om_browse *b);
void om_browse_label(const om_browse *b, const om_item *it, char *out, uint32_t cap);
void om_browse_path(const om_browse *b, char *out, uint32_t cap);
const char *om_vroot_name(uint8_t vr);
/* the games on the card that pass the filter (multi-disc counted once): the top level's count line
 * and the start-up interstitial's "N games" say the same number. */
uint32_t om_browse_games(const om_browse *b);
/* the cursor on game `entry` inside its card folder, walked down from the root as
 * om_browse_enter would (so B climbs the same path back): 1. When the entry is filtered out or not in the tree: 0,
 * with the browser at the root and its cursor at the top (M3.5 decision 7: no message). */
int om_browse_reveal(om_browse *b, uint16_t entry);
/* 2026-11-25 (the return from a tool, handoff.h om_retblk): back to a place saved from b->stack[0..depth-1] and b->loc
 * (path[0..depth]) with its cursor and first row, each level entered as om_browse_enter would (so B climbs the same way
 * back). 1 = there; 0 = a level that is no longer there (the browser at the root, its cursor at the top). */
int om_browse_replay(om_browse *b, const om_loc *path, uint32_t depth, uint16_t cur, uint16_t top);
#endif
