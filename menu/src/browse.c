/* menu/src/browse.c -- see browse.h. BSD-3-Clause. */
#include <string.h>
#include "browse.h"
#include "msort.h"
#include "settings.h"

static const char *const k_vroot[OM_VR_COUNT] = { "Favorites", "Recently Played", "A-Z", "Genre", "Region" };   /* American spelling (addendum 7) */
static const char *const k_region[5] = { "Japan", "Asia (NTSC)", "North America", "Europe/PAL", "Brazil" };  /* bits J T U E B */

const char *om_vroot_name(uint8_t vr) { return vr < OM_VR_COUNT ? k_vroot[vr] : "?"; }

static void add(om_browse *b, uint8_t kind, uint16_t idx)
{
    if (b->n < OM_VIEW_CAP) {
        b->view[b->n].kind = kind;
        b->view[b->n].idx = idx;
        b->n++;
    }
}

static int ok(const om_browse *b, uint16_t e) { return om_entry_passes(b->sort, e, b->filter_genre, b->filter_region); }

static void add_games(om_browse *b, uint32_t ng, int sorted)
{
    uint32_t i;
    if (sorted)
        om_sort_entries(b->sort, b->sort_mode, b->scratch, ng, b->tmp);
    for (i = 0; i < ng; i++)
        add(b, OM_IT_GAME, b->scratch[i]);
}

static int in_bucket(const om_browse *b, uint8_t vr, uint16_t arg, uint16_t e)
{
    const om_sortkey *k = &b->sort->key[e];
    if (vr == OM_VR_AZ) return om_title_letter(k->title) == arg;
    if (vr == OM_VR_GENRE) return k->genre == arg;
    if (vr == OM_VR_REGION) return arg == OM_SUB_UNKNOWN ? !(k->region & 0x1Fu) : (k->region >> arg) & 1u;
    return 0;
}

/* 2026-11-23 (Satiator Rings' model): a card folder lists its folders first -- its folder rows and its single-game
 * folders' games (named and listed as the folder) -- by name, then its files under the active sort mode. A "handle"
 * below is a LIBRARY.IDX index, or OM_MAX_ENTRIES plus a folder index. */
static int cmp_folder(const void *ctx, uint16_t a, uint16_t b)
{
    const om_browse *x = (const om_browse *)ctx;
    const char *na = a >= OM_MAX_ENTRIES ? om_tree_folder_name(x->tree->fpath[a - OM_MAX_ENTRIES]) : x->sort->key[a].title;
    const char *nb = b >= OM_MAX_ENTRIES ? om_tree_folder_name(x->tree->fpath[b - OM_MAX_ENTRIES]) : x->sort->key[b].title;
    return om_name_cmp(na, a < OM_MAX_ENTRIES, nb, b < OM_MAX_ENTRIES);
}

static void add_buckets(om_browse *b, uint8_t vr)
{
    uint8_t seen[256];
    uint32_t e, i;
    memset(seen, 0, sizeof seen);
    for (e = 0; e < b->sort->n; e++) {
        const om_sortkey *k = &b->sort->key[e];
        if (!ok(b, (uint16_t)e)) continue;
        if (vr == OM_VR_AZ) seen[om_title_letter(k->title)] = 1;
        else if (vr == OM_VR_GENRE) seen[k->genre] = 1;
        else {
            if (!(k->region & 0x1Fu)) seen[OM_SUB_UNKNOWN] = 1;
            for (i = 0; i < 5; i++) if (k->region & (1u << i)) seen[i] = 1;
        }
    }
    if (vr == OM_VR_AZ) {
        if (seen['#']) add(b, OM_IT_VSUB, '#');
        for (i = 'A'; i <= 'Z'; i++) if (seen[i]) add(b, OM_IT_VSUB, (uint16_t)i);
    } else if (vr == OM_VR_GENRE) {
        for (i = 0; i < b->sort->ngenre; i++) if (seen[b->sort->genre_by_rank[i]]) add(b, OM_IT_VSUB, b->sort->genre_by_rank[i]);
        if (seen[0xFF]) add(b, OM_IT_VSUB, OM_SUB_UNKNOWN);
    } else {
        for (i = 0; i < 5; i++) if (seen[i]) add(b, OM_IT_VSUB, (uint16_t)i);
        if (seen[OM_SUB_UNKNOWN]) add(b, OM_IT_VSUB, OM_SUB_UNKNOWN);
    }
}

static void rebuild(om_browse *b)
{
    uint32_t i, ng = 0;
    b->n = 0;
    if (b->loc.kind == OM_LOC_CARD) {
        const om_tree *t = b->tree;
        uint16_t f = b->loc.arg;
        uint32_t nother = 0;
        if (f == 0)
            for (i = 0; i < OM_VR_COUNT; i++) add(b, OM_IT_VROOT, (uint16_t)i);   /* spec §7.4: above the card tree */
        for (i = t->start[f]; i < (uint32_t)t->start[f] + t->count[f]; i++) {   /* the folders */
            uint16_t x = t->items[i].idx;
            if (t->items[i].kind == 0) b->scratch[nother++] = (uint16_t)(OM_MAX_ENTRIES + x);
            else if (om_sort_is_folder(b->sort, x) && ok(b, x)) b->scratch[nother++] = x;
        }
        if (nother > 1)
            om_msort(b->scratch, nother, b->tmp, cmp_folder, b);
        for (i = 0; i < nother; i++) {
            uint16_t h = b->scratch[i];
            add(b, h >= OM_MAX_ENTRIES ? OM_IT_FOLDER : OM_IT_GAME, h >= OM_MAX_ENTRIES ? (uint16_t)(h - OM_MAX_ENTRIES) : h);
        }
        for (i = t->start[f]; i < (uint32_t)t->start[f] + t->count[f]; i++)    /* then the files */
            if (t->items[i].kind != 0 && !om_sort_is_folder(b->sort, t->items[i].idx) && ok(b, t->items[i].idx))
                b->scratch[ng++] = t->items[i].idx;
        add_games(b, ng, 1);
    } else if (b->loc.kind == OM_LOC_VROOT && (b->loc.vroot == OM_VR_FAVS || b->loc.vroot == OM_VR_RECENT)) {
        const om_plist *p = b->loc.vroot == OM_VR_FAVS ? b->favs : b->recent;
        for (i = 0; i < p->n; i++)
            if (p->idx[i] < b->sort->n && ok(b, p->idx[i])) b->scratch[ng++] = p->idx[i];
        add_games(b, ng, b->loc.vroot == OM_VR_FAVS);                       /* plan Q4: recent stays MRU */
    } else if (b->loc.kind == OM_LOC_VROOT) {
        add_buckets(b, b->loc.vroot);
    } else {
        for (i = 0; i < b->sort->n; i++)
            if (ok(b, (uint16_t)i) && in_bucket(b, b->loc.vroot, b->loc.arg, (uint16_t)i)) b->scratch[ng++] = (uint16_t)i;
        add_games(b, ng, 1);
    }
}

static void clamp(om_browse *b)
{
    if (!b->n) { b->cur = b->top = 0; return; }
    if (b->cur >= b->n) b->cur = (uint16_t)(b->n - 1);
    if (b->cur < b->top) b->top = b->cur;
    if (b->cur >= b->top + OM_ROWS) b->top = (uint16_t)(b->cur - OM_ROWS + 1);
    if (b->top + OM_ROWS > b->n) b->top = b->n > OM_ROWS ? (uint16_t)(b->n - OM_ROWS) : 0;
}

static uint16_t *slot(om_browse *b, uint16_t **top)
{
    if (b->loc.kind == OM_LOC_CARD) { *top = &b->ctop[b->loc.arg]; return &b->ccur[b->loc.arg]; }
    if (b->loc.kind == OM_LOC_VROOT) { *top = &b->vtop[b->loc.vroot]; return &b->vcur[b->loc.vroot]; }
    *top = &b->vtop[OM_VR_COUNT];
    return &b->vcur[OM_VR_COUNT];
}

static void save(om_browse *b) { uint16_t *t, *c = slot(b, &t); *c = b->cur; *t = b->top; }
static void restore(om_browse *b) { uint16_t *t, *c = slot(b, &t); b->cur = *c; b->top = *t; clamp(b); }

static void reselect(om_browse *b, om_item want)
{
    uint16_t i;
    for (i = 0; i < b->n; i++)
        if (b->view[i].kind == want.kind && b->view[i].idx == want.idx) { b->cur = i; break; }
    clamp(b);
}

static void to_root(om_browse *b)
{   /* the card root, its cursor at the top */
    b->depth = 0;
    b->loc.kind = OM_LOC_CARD;
    b->loc.arg = 0;
    b->ccur[0] = b->ctop[0] = 0;
    rebuild(b);
    restore(b);
}

int om_browse_reveal(om_browse *b, uint16_t entry)
{
    const om_tree *t = b->tree;
    uint16_t chain[OM_MAX_DEPTH], f, home = 0xFFFF;
    uint32_t i;
    int n = 0, j;
    om_item want;
    const om_item *sel;
    if (entry >= b->lib->count || entry >= b->sort->n || !ok(b, entry)) { to_root(b); return 0; }
    for (f = 0; f < t->nfolders && home == 0xFFFF; f++)
        for (i = t->start[f]; i < (uint32_t)t->start[f] + t->count[f]; i++)
            if (t->items[i].kind != 0 && t->items[i].idx == entry) { home = f; break; }
    if (home == 0xFFFF) { to_root(b); return 0; }
    for (f = home; f != 0; f = t->parent[f]) {
        if (n >= (int)OM_MAX_DEPTH) { to_root(b); return 0; }
        chain[n++] = f;
    }
    to_root(b);
    for (j = n - 1; j >= 0; j--) {
        want.kind = OM_IT_FOLDER;
        want.idx = chain[j];
        reselect(b, want);
        sel = om_browse_selected(b);
        if (!sel || sel->kind != OM_IT_FOLDER || sel->idx != chain[j] || om_browse_enter(b) != 1) { to_root(b); return 0; }
    }
    want.kind = OM_IT_GAME;
    want.idx = entry;
    reselect(b, want);
    sel = om_browse_selected(b);
    if (!sel || sel->kind != OM_IT_GAME || sel->idx != entry) { to_root(b); return 0; }
    return 1;
}

int om_browse_replay(om_browse *b, const om_loc *path, uint32_t depth, uint16_t cur, uint16_t top)
{
    uint32_t i;
    to_root(b);
    if (depth >= sizeof b->stack / sizeof b->stack[0] || path[0].kind != OM_LOC_CARD || path[0].arg != 0) return 0;
    for (i = 1; i <= depth; i++) {
        const om_loc *l = &path[i];
        const om_item *sel;
        om_item want;
        if (l->kind == OM_LOC_CARD) { want.kind = OM_IT_FOLDER; want.idx = l->arg; }
        else if (l->kind == OM_LOC_VROOT) { want.kind = OM_IT_VROOT; want.idx = l->vroot; }
        else if (l->kind == OM_LOC_VSUB) { want.kind = OM_IT_VSUB; want.idx = l->arg; }
        else { to_root(b); return 0; }
        reselect(b, want);
        sel = om_browse_selected(b);
        if (!sel || sel->kind != want.kind || sel->idx != want.idx || om_browse_enter(b) != 1) { to_root(b); return 0; }
    }
    if (cur >= b->n) { to_root(b); return 0; }
    b->cur = cur;
    b->top = top;
    clamp(b);
    return 1;
}

void om_browse_init(om_browse *b, const om_tree *t, const om_libidx *l, const om_sortctx *s, const om_gamesdb *db,
                    const om_plist *favs, const om_plist *recent)
{
    memset(b, 0, sizeof *b);
    b->tree = t; b->lib = l; b->sort = s; b->db = db; b->favs = favs; b->recent = recent;
    b->filter_genre = OM_FILTER_ANY;
    b->loc.kind = OM_LOC_CARD;
    rebuild(b);
}

void om_browse_refresh(om_browse *b)
{
    om_item want;
    const om_item *sel = om_browse_selected(b);
    want.kind = sel ? sel->kind : 0xFF;
    want.idx = sel ? sel->idx : 0;
    rebuild(b);
    reselect(b, want);
}

void om_browse_set(om_browse *b, uint8_t sort_mode, uint16_t filter_genre, uint8_t filter_region)
{
    b->sort_mode = sort_mode;
    b->filter_genre = filter_genre;
    b->filter_region = filter_region;
    om_browse_refresh(b);
}

const om_item *om_browse_items(const om_browse *b, uint16_t *n) { *n = b->n; return b->view; }
const om_item *om_browse_selected(const om_browse *b) { return b->n ? &b->view[b->cur] : NULL; }

void om_browse_move(om_browse *b, int delta)
{
    int c = (int)b->cur + delta;
    if (!b->n) return;
    if (c < 0) c = 0;
    if (c > (int)b->n - 1) c = (int)b->n - 1;
    b->cur = (uint16_t)c;
    clamp(b);
}

void om_browse_step(om_browse *b, int dir, int wrap)
{   /* one row; a fresh press (wrap) past either end goes round to the other */
    if (wrap && b->n && (dir < 0 ? b->cur == 0 : b->cur + 1 >= b->n))
        dir = dir < 0 ? (int)b->n : -(int)b->n;
    om_browse_move(b, dir);
}

void om_browse_page(om_browse *b, int dir) { om_browse_move(b, dir < 0 ? -(int)OM_ROWS : (int)OM_ROWS); }

int om_browse_enter(om_browse *b)
{
    const om_item *it = om_browse_selected(b);
    om_loc nl;
    if (!it) return 0;
    if (it->kind == OM_IT_GAME) return 2;
    if (b->depth >= sizeof b->stack / sizeof b->stack[0]) return 0;
    save(b);
    nl = b->loc;
    if (it->kind == OM_IT_FOLDER) { nl.kind = OM_LOC_CARD; nl.arg = it->idx; }
    else if (it->kind == OM_IT_VROOT) { nl.kind = OM_LOC_VROOT; nl.vroot = (uint8_t)it->idx; nl.arg = 0; }
    else { nl.kind = OM_LOC_VSUB; nl.arg = it->idx; b->vcur[OM_VR_COUNT] = b->vtop[OM_VR_COUNT] = 0; }
    b->stack[b->depth++] = b->loc;
    b->loc = nl;
    rebuild(b);
    restore(b);
    return 1;
}

int om_browse_back(om_browse *b)
{
    if (!b->depth) return 0;
    save(b);
    b->loc = b->stack[--b->depth];
    rebuild(b);
    restore(b);
    return 1;
}

static const char *sub_name(const om_browse *b, uint8_t vr, uint16_t code, char letter[2])
{
    const char *g;
    if (code == OM_SUB_UNKNOWN && vr != OM_VR_AZ) return "Unknown";
    if (vr == OM_VR_AZ) { letter[0] = (char)code; letter[1] = 0; return letter; }
    if (vr == OM_VR_GENRE) { g = b->db ? om_gamesdb_genre(b->db, (uint8_t)code) : NULL; return g ? g : "?"; }
    return code < 5 ? k_region[code] : "?";
}

static uint32_t app(char *out, uint32_t o, uint32_t cap, const char *s)
{
    while (*s && o + 1 < cap) out[o++] = *s++;
    out[o] = 0;
    return o;
}

void om_browse_label(const om_browse *b, const om_item *it, char *out, uint32_t cap)
{
    char letter[2];
    uint32_t o = 0;
    out[0] = 0;
    if (!it || it->kind == OM_IT_GAME) return;              /* games are titled by the view (om_title) */
    if (it->kind == OM_IT_FOLDER) o = app(out, 0, cap, om_tree_folder_name(b->tree->fpath[it->idx]));
    else if (it->kind == OM_IT_VROOT) o = app(out, 0, cap, om_vroot_name((uint8_t)it->idx));
    else o = app(out, 0, cap, sub_name(b, b->loc.vroot, it->idx, letter));
    app(out, o, cap, "/");                                   /* the folder glyph (ASCII font, M2) */
}

void om_browse_path(const om_browse *b, char *out, uint32_t cap)
{
    char letter[2];
    uint32_t o = app(out, 0, cap, "/");
    if (b->loc.kind == OM_LOC_CARD) { app(out, o, cap, b->tree->fpath[b->loc.arg]); return; }
    o = app(out, o, cap, om_vroot_name(b->loc.vroot));
    if (b->loc.kind == OM_LOC_VSUB) {
        o = app(out, o, cap, "/");
        app(out, o, cap, sub_name(b, b->loc.vroot, b->loc.arg, letter));
    }
}

uint32_t om_browse_games(const om_browse *b)
{
    uint32_t e, n = 0;
    for (e = 0; e < b->lib->count; e++)
        if (b->lib->table[32 * e + 0x10] <= 1u && ok(b, (uint16_t)e)) n++;
    return n;
}
