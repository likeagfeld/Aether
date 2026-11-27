/* menu/src/tree.h -- Folders view model (spec §7.4): per-folder item lists, cursor memory, paging, scrollbar.
 * BSD-3-Clause. */
#ifndef TREE_H
#define TREE_H
#include <stdint.h>
#include "libidx.h"

#define OM_ROWS        8u              /* the Folders view's visible rows (8 rows of 14 px) */
#define OM_MAX_FOLDERS 1024u
#define OM_MAX_ENTRIES 2048u
#define OM_MAX_DEPTH   8u

typedef struct { uint8_t kind; uint16_t idx; } om_item;

typedef struct {
    om_item items[OM_MAX_FOLDERS + OM_MAX_ENTRIES];
    uint16_t start[OM_MAX_FOLDERS], count[OM_MAX_FOLDERS], parent[OM_MAX_FOLDERS];
    uint16_t cursor[OM_MAX_FOLDERS], top[OM_MAX_FOLDERS];
    const char *fpath[OM_MAX_FOLDERS];
    uint16_t nfolders, cur;
} om_tree;

int om_tree_build(om_tree *t, const om_libidx *l);
const om_item *om_tree_items(const om_tree *t, uint16_t *n);
const om_item *om_tree_selected(const om_tree *t);
uint16_t om_tree_cursor(const om_tree *t);
uint16_t om_tree_top(const om_tree *t);
void om_tree_move(om_tree *t, int delta);
void om_tree_page(om_tree *t, int dir);
int om_tree_enter(om_tree *t);
int om_tree_back(om_tree *t);
const char *om_tree_folder_name(const char *path);
void om_scrollbar(uint32_t n, uint32_t top, uint32_t rows, uint32_t track, uint32_t *pos, uint32_t *len);
#endif
