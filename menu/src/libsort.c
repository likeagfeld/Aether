/* menu/src/libsort.c -- see libsort.h. BSD-3-Clause. */
#include <string.h>
#include "libsort.h"
#include "msort.h"
#include "yield.h"
#include "settings.h"
#include "title.h"

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int nch(const char *s, int span)
{   /* a single-game folder's name is a span of its entry's path, ending at its '/' */
    if (span && *s == '/')
        return 0;
    return (unsigned char)*s;
}

int om_name_cmp(const char *a, int sa, const char *b, int sb)
{   /* case-insensitive; a case-only tie is broken by the bytes */
    int pass, x = 0, y = 0;
    for (pass = 0; pass < 2; pass++) {
        const char *p = a, *q = b;
        for (;; p++, q++) {
            x = nch(p, sa); y = nch(q, sb);
            if (!pass) { x = lower(x); y = lower(y); }
            if (x != y || !x) break;
        }
        if (x != y) return x - y;
    }
    return 0;
}

int om_title_cmp(const char *a, const char *b) { return om_name_cmp(a, 0, b, 0); }

static int is_span(const om_sortctx *c, uint32_t e) { return (c->span[e >> 3] >> (e & 7)) & 1; }
int om_sort_is_folder(const om_sortctx *c, uint16_t e) { return e < c->n && is_span(c, e); }

void om_sortkey_name(const om_sortctx *c, uint16_t e, char *out, uint32_t cap)
{
    const char *s = c->key[e].title;
    uint32_t o = 0;
    for (; o + 1 < cap && nch(s + o, is_span(c, e)); o++) out[o] = s[o];
    out[o] = 0;
}

uint8_t om_title_letter(const char *t)
{
    int c = lower((unsigned char)t[0]);
    return (uint8_t)(c >= 'a' && c <= 'z' ? c - 32 : '#');
}

static int cmp_genre_name(const void *ctx, uint16_t a, uint16_t b)
{
    const om_gamesdb *db = (const om_gamesdb *)ctx;
    const char *x = om_gamesdb_genre(db, (uint8_t)a), *y = om_gamesdb_genre(db, (uint8_t)b);
    return om_title_cmp(x ? x : "", y ? y : "");
}

void om_sortctx_build(om_sortctx *c, const om_libidx *l, const om_gamesdb *db)
{
    uint16_t ids[256], tmp[256];
    uint32_t i;
    om_lib_entry e;
    om_game g;
    uint32_t len;
    memset(c->genre_rank, 255, sizeof c->genre_rank);
    memset(c->span, 0, sizeof c->span);
    c->ngenre = db ? (uint16_t)(db->ngenre > 255 ? 255 : db->ngenre) : 0;
    for (i = 0; i < c->ngenre; i++)
        ids[i] = (uint16_t)i;
    if (c->ngenre)
        om_msort(ids, c->ngenre, tmp, cmp_genre_name, db);
    for (i = 0; i < c->ngenre; i++) {
        c->genre_by_rank[i] = (uint8_t)ids[i];
        c->genre_rank[ids[i]] = (uint8_t)i;
    }
    c->n = l->count > OM_SORT_MAX ? OM_SORT_MAX : l->count;
    for (i = 0; i < c->n; i++) {
        om_sortkey *k = &c->key[i];
        int have;
        if (!(i & 15u)) om_yield();              /* a GAMES.DB search an entry: frames on time (yield.h) */
        om_libidx_entry(l, i, &e);
        have = db && e.key[0] && om_gamesdb_find(db, e.key, &g);
        k->title = om_entry_raw(l, &e, &len);    /* the row's name (title.h), so A-Z sorts and groups as the list reads */
        if (k->title[len])
            c->span[i >> 3] |= (uint8_t)(1u << (i & 7));
        k->year = have ? om_game_year(&g) : 0;
        k->genre = have && g.genre < c->ngenre ? g.genre : 0xFF;
        /* the same bits, and so the same primary area, as the flag the list shows (uiline.c om_primary_area) */
        k->region = om_region_bits(&e, have ? &g : NULL);
    }
}

uint16_t om_game_year(const om_game *g)
{   /* the year uiline.c's meta line shows from a GAMES.DB record: its year, else its date's (YYYYMMDD) */
    return (uint16_t)(g->year ? g->year : g->date / 10000u);
}

uint8_t om_region_bits(const om_lib_entry *e, const om_game *g)
{   /* GAMES.DB's region for the product (a US release whose disc also lists J/T, such as Golden Axe The Duel "UTJ",
     * or an XBAND disc patched to every area, stays USA), else the LIBRARY.IDX area bits read from its IP.BIN */
    return (uint8_t)(((g && g->region) ? g->region : e->area) & 0x1Fu);
}

uint8_t om_region_primary(uint8_t f)
{
    /* which bit is "primary" when more than one is set: the same priority uiline.c's canonical area order
     * ("JTUBKAEL") gives, restricted to the 5 bits tracked here (J,T,U,B,E -- Brazil ranks above Europe), so a
     * multi-region game's region-sort group is the flag the list draws (uiline.c om_primary_area, from these same
     * bits whether or not the game's IP.BIN has been loaded since -- sort keys are built once at card load).
     * The returned value is still the bit index in the J,T,U,E,B order k_region[] (browse.c) and the Region
     * vroot's bucket list use, so picking a different primary bit does not reorder the bucket groups themselves. */
    static const uint8_t pri[5] = { 0, 1, 2, 4, 3 };
    uint8_t i;
    for (i = 0; i < 5; i++)
        if (f & (1u << pri[i]))
            return pri[i];
    return 5;
}

int om_sort_cmp(const om_sortctx *c, uint8_t mode, uint16_t a, uint16_t b)
{
    const om_sortkey *ka = &c->key[a], *kb = &c->key[b];
    int d = 0;
    switch (mode) {
    case OM_SORT_GENRE:  d = (int)c->genre_rank[ka->genre] - (int)c->genre_rank[kb->genre]; break;
    case OM_SORT_REGION: d = (int)om_region_primary(ka->region) - (int)om_region_primary(kb->region); break;
    case OM_SORT_YEAR:   d = (int)(ka->year ? ka->year : 0xFFFFu) - (int)(kb->year ? kb->year : 0xFFFFu); break;
    default: break;
    }
    return d ? d : om_name_cmp(ka->title, is_span(c, a), kb->title, is_span(c, b));
}

typedef struct { const om_sortctx *c; uint8_t mode; } cmpctx;

static int cmp_entries(const void *ctx, uint16_t a, uint16_t b)
{
    const cmpctx *x = (const cmpctx *)ctx;
    return om_sort_cmp(x->c, x->mode, a, b);
}

void om_sort_entries(const om_sortctx *c, uint8_t mode, uint16_t *idx, uint32_t n, uint16_t *tmp)
{
    cmpctx x;
    x.c = c;
    x.mode = mode;
    om_msort(idx, n, tmp, cmp_entries, &x);
}

int om_entry_passes(const om_sortctx *c, uint16_t e, uint16_t genre, uint8_t region_mask)
{
    const om_sortkey *k = &c->key[e];
    if (genre != OM_FILTER_ANY && k->genre != genre)
        return 0;
    return !region_mask || (k->region & region_mask) != 0;
}
