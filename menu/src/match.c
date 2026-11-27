/* menu/src/match.c -- see match.h (SPEC §6 v3). Deviation, documented: candidate sets are capped at OM_MATCH_MAXCAND (64);
 * an overflowing set is reported unmatched with overflow=1 (Python is unbounded). The exact/alias branches look up
 * k-50 and k-18 directly, which is the same set as Python's _suffixed_by_base[k]. BSD-3-Clause. */
#include <string.h>
#include "match.h"

static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
static int is_dig(char c) { return c >= '0' && c <= '9'; }

const char *om_how_name(om_how h)
{
    static const char *const n[] = { NULL, "exact", "alias", "skeleton", "prefix", "suffix" };
    return (unsigned)h < 6 ? n[h] : NULL;
}

void om_cleanup(const char *in, uint32_t len, char *out, uint32_t cap)
{
    uint32_t n = 0, e, v;
    while (n < len && in[n] && n + 1 < cap) { out[n] = in[n]; n++; }
    out[n] = 0;
    e = n;
    while (e && is_ws(out[e - 1])) e--;                          /* the field's trailing blanks are not the code */
    if (e >= 3 && is_dig(out[e - 1]) && out[e - 2] == 'V' && is_dig(out[e - 3])) {
        out[e - 2] = 0;                                          /* (?<=\d)V\d$ : "MK-81029V1" */
        return;
    }
    v = (e >= 1 && out[e - 1] == 'V') ? e - 1 : (e >= 2 && is_dig(out[e - 1]) && out[e - 2] == 'V') ? e - 2 : 0;
    if (v && is_ws(out[v - 1])) {                                /* \s+V\d?$ : "T-29002G V", "GS-9055 V1" */
        while (v && is_ws(out[v - 1])) v--;
        out[v] = 0;
    }
}

void om_normalise(const char *in, uint32_t len, char out[17])
{
    uint32_t i, j, n = 0, o = 0;
    while (n < len && in[n] && in[n] != '\n' && in[n] != '\r') n++;    /* text before the first newline */
    for (i = 0; i < n; i++) {
        if (in[i] == '(') {                                             /* delete "(...)" groups */
            for (j = i + 1; j < n && in[j] != ')'; j++) ;
            if (j < n) { i = j; continue; }
        }
        if (is_ws(in[i])) continue;
        if (o < 16) out[o++] = up(in[i]);
    }
    out[o] = 0;
}

static int has_suffix(const char *k)
{
    size_t n = strlen(k);
    return n >= 3 && k[n - 3] == '-' && ((k[n - 2] == '5' && k[n - 1] == '0') || (k[n - 2] == '1' && k[n - 1] == '8'));
}

static void skeleton(const char *base, uint32_t blen, char out[32])
{
    uint32_t i, n = 0, L = 0, z = 0;
    for (i = 0; i < blen && base[i] && n < 31; i++) {
        char c = up(base[i]);
        if (c == '-' || c == ' ' || c == '_') continue;
        out[n++] = c;
    }
    out[n] = 0;
    while (L < n && out[L] >= 'A' && out[L] <= 'Z') L++;
    if (L) {
        while (L + z < n && out[L + z] == '0') z++;
        if (z && L + z < n && out[L + z] >= '1' && out[L + z] <= '9')
            memmove(out + L, out + L + z, n - L - z + 1);
    }
}

/* skeleton_forms(): the skeleton, plus it less a leading "MK", or less a leading "T" before a digit */
static const char *alt_form(const char *s)
{
    if (s[0] == 'M' && s[1] == 'K') return s + 2;
    if (s[0] == 'T' && is_dig(s[1])) return s + 1;
    return NULL;
}

static int forms_meet(const char *a, const char *b)
{
    const char *a2 = alt_form(a), *b2 = alt_form(b);
    return !strcmp(a, b) || (a2 && !strcmp(a2, b)) || (b2 && !strcmp(a, b2)) || (a2 && b2 && !strcmp(a2, b2));
}

typedef struct { char k[OM_MATCH_MAXCAND][17]; uint32_t n; int overflow; } cands;

static void add(cands *c, const char *k)
{
    uint32_t i;
    for (i = 0; i < c->n; i++)
        if (!strcmp(c->k[i], k)) return;
    if (c->n == OM_MATCH_MAXCAND) { c->overflow = 1; return; }
    strncpy(c->k[c->n], k, 16);
    c->k[c->n++][16] = 0;
}

static int choose(cands *c, const char *area, om_how how, om_match_result *r)
{
    int has_e = 0, has_o = 0, pref;
    uint32_t i, nkeep = 0, shortest = 99, nbest = 0, nfifty = 0, allsuf = 1, best = 0, fifty = 0;
    uint8_t keep[OM_MATCH_MAXCAND];
    for (; *area; area++) {
        char a = up(*area);
        has_e |= a == 'E';
        has_o |= a == 'J' || a == 'T' || a == 'U';
    }
    pref = (has_e && !has_o) ? 1 : (!has_e ? 2 : 0);                    /* 1 PAL, 2 NTSC, 0 none */
    for (i = 0; i < c->n; i++) {
        int s = has_suffix(c->k[i]);
        keep[i] = (uint8_t)(pref == 1 ? s : pref == 2 ? !s : 1);
        nkeep += keep[i];
    }
    if (!nkeep)
        for (i = 0; i < c->n; i++) keep[i] = 1;                          /* step 1: never empty the set */
    for (i = 0; i < c->n; i++)
        if (keep[i] && strlen(c->k[i]) < shortest) shortest = (uint32_t)strlen(c->k[i]);
    for (i = 0; i < c->n; i++)
        if (keep[i] && strlen(c->k[i]) == shortest) {
            nbest++; best = i;
            if (!has_suffix(c->k[i])) allsuf = 0;
            else if (c->k[i][shortest - 2] == '5') { nfifty++; fifty = i; }
        }
    r->ncand = nbest;
    if (c->overflow) { r->key[0] = 0; r->how = OM_HOW_NONE; r->overflow = 1; return 0; }
    if (nbest == 1) { strcpy(r->key, c->k[best]); r->how = how; return 1; }
    if (allsuf && nfifty == 1) { strcpy(r->key, c->k[fifty]); r->how = how; return 1; }   /* step 3, R8 */
    r->key[0] = 0;
    r->how = OM_HOW_NONE;
    return 0;
}

static void add_family(const om_keysrc *ks, cands *c, const char *k)
{
    char s[24];
    size_t n = strlen(k);
    add(c, k);
    if (n + 3 <= 16) {
        memcpy(s, k, n);
        memcpy(s + n, "-50", 4); if (ks->has(ks->ctx, s)) add(c, s);
        memcpy(s + n, "-18", 4); if (ks->has(ks->ctx, s)) add(c, s);
    }
}

/* every DB key whose split() base's skeleton forms meet `sk`'s */
static void family(const om_keysrc *ks, cands *c, const char *sk)
{
    char x[17], skx[32];
    uint32_t i, bl;
    for (i = 0; i < ks->count; i++) {
        ks->key_at(ks->ctx, i, x);
        bl = (uint32_t)strlen(x) - (has_suffix(x) ? 3u : 0u);
        skeleton(x, bl, skx);
        if (forms_meet(skx, sk)) add(c, x);
    }
}

int om_match(const om_keysrc *ks, const char *product, uint32_t plen, const char *area, om_match_result *r)
{
    static cands c;                    /* 1.1 KB: static to spare the SGL master stack */
    char cl[64], k[17], canon[17], x[17], sk[32];
    uint32_t i, klen;
    memset(r, 0, sizeof *r);
    om_cleanup(product, plen, cl, sizeof cl);
    om_normalise(cl, (uint32_t)strlen(cl), k);
    if (!k[0])
        return 0;
    c.n = 0; c.overflow = 0;
    if (ks->has(ks->ctx, k)) { add_family(ks, &c, k); return choose(&c, area, OM_HOW_EXACT, r); }
    if (ks->alias && ks->alias(ks->ctx, k, canon) && ks->has(ks->ctx, canon)) {
        add_family(ks, &c, canon);
        return choose(&c, area, OM_HOW_ALIAS, r);
    }
    klen = (uint32_t)strlen(k);
    skeleton(k, klen, sk);
    family(ks, &c, sk);
    if (c.n || c.overflow) return choose(&c, area, OM_HOW_SKELETON, r);
    if (klen >= 2 && ((k[klen - 2] == '5' && k[klen - 1] == '0') || (k[klen - 2] == '1' && k[klen - 1] == '8'))) {
        uint32_t cut = klen - 2;                                   /* suffix_base(): -50/-18, or dashless 50/18 */
        if (cut && k[cut - 1] == '-') cut--;
        if (cut >= 4) {
            skeleton(k, cut, sk);
            family(ks, &c, sk);
            if (c.n || c.overflow) return choose(&c, area, OM_HOW_SUFFIX, r);
        }
    }
    for (i = 0; i < ks->count; i++) {
        ks->key_at(ks->ctx, i, x);
        if (!strncmp(x, k, klen)) add(&c, x);
    }
    if (c.n || c.overflow) return choose(&c, area, OM_HOW_PREFIX, r);
    return 0;
}

static void g_key(void *ctx, uint32_t i, char out[17]) { om_game g; om_gamesdb_at((const om_gamesdb *)ctx, i, &g); strcpy(out, g.key); }
static int g_has(void *ctx, const char *k) { om_game g; return om_gamesdb_find((const om_gamesdb *)ctx, k, &g); }
static int g_alias(void *ctx, const char *a, char canon[17]) { return om_gamesdb_alias((const om_gamesdb *)ctx, a, canon); }

void om_keysrc_gamesdb(om_keysrc *ks, const om_gamesdb *db)
{
    ks->ctx = (void *)db;
    ks->count = db ? db->count : 0;
    ks->key_at = g_key;
    ks->has = g_has;
    ks->alias = g_alias;
}
