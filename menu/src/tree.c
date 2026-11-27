/* menu/src/tree.c -- see tree.h. BSD-3-Clause. */
#include <string.h>
#include "tree.h"
#include "yield.h"

const char *om_tree_folder_name(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

int om_tree_build(om_tree *t, const om_libidx *l)
{
    uint32_t i, j, f, o = 0;
    uint8_t depth[OM_MAX_FOLDERS];
    om_lib_folder fo;
    if (l->nfolders > OM_MAX_FOLDERS || l->count > OM_MAX_ENTRIES)
        return OM_E_FULL;
    t->nfolders = (uint16_t)l->nfolders;
    t->cur = 0;
    for (i = 0; i < l->nfolders; i++) {
        om_libidx_folder(l, i, &fo);
        t->fpath[i] = fo.path;
        t->parent[i] = fo.parent;
        t->cursor[i] = t->top[i] = t->count[i] = 0;
        if (i && fo.parent >= i)
            return OM_E_FIELD;               /* parents precede children (clarification 7): no cycles */
        depth[i] = (uint8_t)(i ? depth[fo.parent] + 1 : 0);
        if (depth[i] > OM_MAX_DEPTH)
            return OM_E_FIELD;               /* spec §7.4: depth <= 8 */
    }
    for (f = 0; f < l->nfolders; f++) {
        om_yield();                              /* folders x entries: the start-up screen's frames (yield.h) */
        t->start[f] = (uint16_t)o;
        for (i = 1; i < l->nfolders; i++)    /* subfolders, sorted by name bytes */
            if (t->parent[i] == f) {
                for (j = o; j > t->start[f] && strcmp(om_tree_folder_name(t->fpath[i]),
                     om_tree_folder_name(t->fpath[t->items[j - 1].idx])) < 0; j--)
                    t->items[j] = t->items[j - 1];
                t->items[j].kind = 0;
                t->items[j].idx = (uint16_t)i;
                o++;
            }
        for (i = 0; i < l->count; i++) {     /* then games, in LIBRARY.IDX order: the folder field alone (row +0x18), */
            if (om_be16(l->table + 32 * i + 0x18) == f) {   /* not a full decode -- folders x entries, 614k at 300 x 2,048 */
                t->items[o].kind = 1;
                t->items[o].idx = (uint16_t)i;
                o++;
            }
        }
        t->count[f] = (uint16_t)(o - t->start[f]);
    }
    return OM_OK;
}

const om_item *om_tree_items(const om_tree *t, uint16_t *n) { *n = t->count[t->cur]; return &t->items[t->start[t->cur]]; }
const om_item *om_tree_selected(const om_tree *t) { return t->count[t->cur] ? &t->items[t->start[t->cur] + t->cursor[t->cur]] : NULL; }
uint16_t om_tree_cursor(const om_tree *t) { return t->cursor[t->cur]; }
uint16_t om_tree_top(const om_tree *t) { return t->top[t->cur]; }

static void fix_top(om_tree *t)
{
    uint16_t c = t->cursor[t->cur];
    if (c < t->top[t->cur]) t->top[t->cur] = c;
    if (c >= t->top[t->cur] + OM_ROWS) t->top[t->cur] = (uint16_t)(c - OM_ROWS + 1);
}

void om_tree_move(om_tree *t, int delta)
{
    int n = t->count[t->cur], c = t->cursor[t->cur] + delta;
    if (!n) return;
    if (c < 0) c = 0;
    if (c > n - 1) c = n - 1;
    t->cursor[t->cur] = (uint16_t)c;
    fix_top(t);
}

void om_tree_page(om_tree *t, int dir) { om_tree_move(t, dir < 0 ? -(int)OM_ROWS : (int)OM_ROWS); }

int om_tree_enter(om_tree *t)
{
    const om_item *it = om_tree_selected(t);
    if (!it) return 0;
    if (it->kind == 1) return 2;
    t->cur = it->idx;
    return 1;
}

int om_tree_back(om_tree *t)
{
    if (t->cur == 0) return 0;
    t->cur = t->parent[t->cur];
    return 1;
}

void om_scrollbar(uint32_t n, uint32_t top, uint32_t rows, uint32_t track, uint32_t *pos, uint32_t *len)
{
    if (n <= rows) { *pos = 0; *len = track; return; }
    *len = track * rows / n;
    if (*len < 1) *len = 1;
    *pos = (track - *len) * top / (n - rows);
}
