/* menu/src/pgsplan.c -- see pgsplan.h (contract/SPEC.md §5.17). Runs in LAUNCHER.BIN once per launch;
 * nothing in the menu calls it (the app's --gc-sections drops it). BSD-3-Clause. */
#include <string.h>
#include "pgsplan.h"
#include "bupfile.h"
#include "om_fmt.h"

static const char *const k_ops[OM_OP_COUNT] = { "ARCHIVE", "VERIFY", "ROTATE_GENERAL", "CLEAR", "LOAD",
                                                "WRITE_MARKER", "DELETE_MARKER", "CHECK_FITS", "QUARANTINE",
                                                "LOAD_NEWER" };

const char *om_pgs_op_name(uint32_t op) { return op < OM_OP_COUNT ? k_ops[op] : "?"; }

int om_pgs_slot_dir_po(char out[OM_PGS_DIR], const char *gid, uint32_t slot, int po)
{
    char f[OM_GID_MAX + 1];
    uint32_t n;
    out[0] = 0;
    if (slot > 9 || om_gameid_dir(gid, f))
        return OM_E_FIELD;
    if (po) {                                          /* "<product>[<areas>]" -> "<product>" */
        char *b = strchr(f, '[');
        if (b) *b = 0;
    }
    memcpy(out, "SAVES/", 6);
    n = (uint32_t)strlen(f);
    memcpy(out + 6, f, n + 1);                         /* 6 + 21 + 3 + 1 <= OM_PGS_DIR */
    if (slot) {
        n += 6;
        out[n++] = '/'; out[n++] = '0'; out[n++] = (char)('0' + slot); out[n] = 0;
    }
    return OM_OK;
}

int om_pgs_slot_dir(char out[OM_PGS_DIR], const char *gid, uint32_t slot) { return om_pgs_slot_dir_po(out, gid, slot, 0); }

int om_pgs_product_dir(char out[OM_PGS_DIR], const char *gid)
{
    char f[OM_GID_MAX + 1];
    out[0] = 0;
    if (om_gameid_dir(gid, f) || !strchr(f, '['))
        return OM_E_FIELD;
    return om_pgs_slot_dir_po(out, gid, 0, 1);
}

static char *put_u(char *o, uint32_t v);

int om_pgs_q_dir(char out[OM_PGS_DIR], uint32_t n)
{
    out[0] = 0;
    if (!n || n > 65535u) return OM_E_FIELD;
    memcpy(out, OM_PGS_QUAR "/", sizeof OM_PGS_QUAR);
    *put_u(out + sizeof OM_PGS_QUAR, n) = 0;
    return OM_OK;
}

typedef struct { om_pgs_op *o; uint32_t cap, n; int err; } list;

static void add(list *l, int op, const char *dir)
{
    if (l->n >= l->cap) { l->err = OM_E_FULL; return; }
    l->o[l->n].op = (uint8_t)op;
    strcpy(l->o[l->n].dir, dir);
    l->n++;
}

/* m, w: the marker's and the wanted slot dirs; returns the case number (1-5) or < 0 */
static int classify(const om_pgs_state *s, char m[OM_PGS_DIR], char w[OM_PGS_DIR])
{
    m[0] = w[0] = 0;
    if (s->foreign)                                  /* m = the quarantine folder */
        return om_pgs_q_dir(m, s->qnum) ? OM_E_FIELD : 6;
    if (s->want_slot > 9 || (s->has_marker && s->marker_slot > 9))
        return OM_E_FIELD;
    if (s->has_marker && om_pgs_slot_dir_po(m, s->marker_gid, s->marker_slot, s->marker_po))
        return OM_E_FIELD;
    if (s->want_pgs && om_pgs_slot_dir_po(w, s->want_gid, s->want_slot, s->want_po))
        return OM_E_FIELD;
    if (s->want_pgs)
        return !s->has_marker ? 3 : strcmp(m, w) ? 2 : 1;
    return s->has_marker ? 4 : 5;
}

static int done(list *l, uint32_t *n) { *n = l->err ? 0 : l->n; return l->err; }

int om_pgs_plan(const om_pgs_state *s, om_pgs_op *ops, uint32_t cap, uint32_t *n)
{
    char m[OM_PGS_DIR], w[OM_PGS_DIR];
    list l = { ops, cap, 0, 0 };
    int c = classify(s, m, w);
    *n = 0;
    if (c < 0) return c;
    if (c == 1 || c == 5) return OM_OK;
    if (c == 6) {
        add(&l, OM_OP_QUARANTINE, m);
        add(&l, OM_OP_CHECK_FITS, OM_PGS_GENERAL);
        add(&l, OM_OP_CLEAR_BRAM_EXCEPT_MARKER, "");
        add(&l, OM_OP_LOAD_DIR_TO_BRAM, OM_PGS_GENERAL);
        add(&l, OM_OP_DELETE_MARKER, "");
        return done(&l, n);
    }
    if (c == 3) add(&l, OM_OP_ROTATE_GENERAL, "");
    add(&l, OM_OP_ARCHIVE_BRAM_TO, c == 3 ? OM_PGS_GENERAL : m);
    add(&l, OM_OP_VERIFY_DIR, c == 3 ? OM_PGS_GENERAL : m);
    add(&l, OM_OP_CHECK_FITS, c == 4 ? OM_PGS_GENERAL : w);
    add(&l, OM_OP_CLEAR_BRAM_EXCEPT_MARKER, "");
    if (c == 4) {
        add(&l, OM_OP_LOAD_DIR_TO_BRAM, OM_PGS_GENERAL);
        add(&l, OM_OP_DELETE_MARKER, "");
    } else {
        if (s->share && !s->slot_exists)               /* Share: the game's first use gets a copy (decision 9) */
            add(&l, OM_OP_LOAD_DIR_TO_BRAM, OM_PGS_GENERAL);
        add(&l, OM_OP_LOAD_DIR_TO_BRAM, w);
        /* the first load takes newer general copies -- slot 0 only; slots 1-9
         * are separate save lines and never take records from _GENERAL */
        if (s->first_load && s->slot_exists && s->want_slot == 0)
            add(&l, OM_OP_LOAD_NEWER, OM_PGS_GENERAL);
        add(&l, OM_OP_WRITE_MARKER, w);
    }
    return done(&l, n);
}

int om_pgs_undo(const om_pgs_state *s, om_pgs_op *ops, uint32_t cap, uint32_t *n)
{
    char m[OM_PGS_DIR], w[OM_PGS_DIR];
    list l = { ops, cap, 0, 0 };
    int c = classify(s, m, w);
    *n = 0;
    if (c < 0) return c;
    if (c == 1 || c == 5) return OM_OK;
    add(&l, OM_OP_CLEAR_BRAM_EXCEPT_MARKER, "");
    add(&l, OM_OP_LOAD_DIR_TO_BRAM, c == 3 ? OM_PGS_GENERAL : m);
    if (c == 3 || c == 6)
        add(&l, OM_OP_DELETE_MARKER, "");
    else
        add(&l, OM_OP_WRITE_MARKER, m);
    return done(&l, n);
}

static char *put_u(char *o, uint32_t v)
{
    char t[11];
    int k = 0;
    do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (k) *o++ = t[--k];
    return o;
}

static char *put_x(char *o, uint32_t v)
{
    int k;
    for (k = 28; k >= 0; k -= 4) *o++ = "0123456789ABCDEF"[(v >> k) & 15u];
    return o;
}

static int line_fmt(char *out, uint32_t cap, const char *gid, uint32_t slot, uint32_t nonce, const uint32_t *fp)
{
    char t[OM_GID_MAX + 24], *o;
    uint32_t n = (uint32_t)strlen(gid);
    if (slot > 9 || om_gameid_check(gid))
        return OM_E_FIELD;
    memcpy(t, gid, n);
    o = t + n;
    *o++ = ' '; *o++ = (char)('0' + slot); *o++ = ' ';
    o = put_x(o, nonce);
    if (fp) { *o++ = ' '; o = put_x(o, *fp); }
    *o++ = '\n';
    n = (uint32_t)(o - t);
    if (n + 1 > cap)
        return OM_E_FULL;
    memcpy(out, t, n);
    out[n] = 0;
    return (int)n;
}

int om_pgs_marker_fmt(char *out, uint32_t cap, const char *gid, uint32_t slot, uint32_t nonce)
{
    return line_fmt(out, cap, gid, slot, nonce, 0);
}

int om_pgs_active_fmt(char *out, uint32_t cap, const char *gid, uint32_t slot, uint32_t nonce, uint32_t fp)
{
    return line_fmt(out, cap, gid, slot, nonce, &fp);
}

static int hex8(const uint8_t *d, uint32_t *v)
{
    int k;
    *v = 0;
    for (k = 0; k < 8; k++) {
        uint8_t c = d[k];
        uint32_t x = c >= '0' && c <= '9' ? (uint32_t)(c - '0') : c >= 'A' && c <= 'F' ? (uint32_t)(c - 'A') + 10u : 99u;
        if (x > 15) return 0;
        *v = *v << 4 | x;
    }
    return 1;
}

uint32_t om_pgs_rec_id(const char name[12], uint32_t date, uint32_t size, uint32_t data_crc)
{
    uint8_t b[24];
    memcpy(b, name, 12);
    om_put32(b + 12, date);
    om_put32(b + 16, size);
    om_put32(b + 20, data_crc);
    return om_crc32(0, b, sizeof b);
}

int om_pgs_marker_parse(const uint8_t *d, uint32_t n, char gid[OM_GID_MAX], uint8_t *slot, uint32_t *nonce,
                        uint32_t *fp)
{
    uint32_t i, e;
    gid[0] = 0;
    if (n && d[n - 1] == '\n') n--;                    /* the LF is optional */
    for (i = 0; i < n && d[i] != ' '; i++)
        if (i + 1 >= OM_GID_MAX) return OM_E_FIELD;
    e = i + 2 + 1 + 8 + (fp ? 9u : 0u);                /* " s nnnnnnnn[ ffffffff]" */
    if (i == n || n != e || d[i + 1] < '0' || d[i + 1] > '9' || d[i + 2] != ' ' || !hex8(d + i + 3, nonce)
        || (fp && (d[i + 11] != ' ' || !hex8(d + i + 12, fp))))
        return OM_E_FIELD;
    memcpy(gid, d, i);
    gid[i] = 0;
    *slot = (uint8_t)(d[i + 1] - '0');
    if (om_gameid_check(gid)) { gid[0] = 0; return OM_E_FIELD; }
    return OM_OK;
}

int om_pgs_reserved(const char name[12])
{
    char a[12];
    if (!om_bup_name_norm(a, name)) return 0;
    return !memcmp(a, OM_PGS_MARKER, 12) || !memcmp(a, "PSEUDO_", 7) || !memcmp(a, "_CARTDMP", 8)
        || !memcmp(a, "SAVRING", 7);
}

uint32_t om_pgs_fp(const void *list, uint32_t n, uint32_t stride, uint32_t *count)
{   /* selection by name, so the listing's order does not matter (n <= 512: at most n^2 / 2 compares, once a launch) */
    const uint8_t *b = (const uint8_t *)list;
    char last[12], best[12], nm[12];
    uint32_t i, crc = 0, c = 0;
    int have_last = 0;
    for (;;) {
        const om_bup_dir *pick = 0;
        for (i = 0; i < n; i++) {
            const om_bup_dir *d = (const om_bup_dir *)(b + i * stride);
            if (!om_bup_name_norm(nm, d->name) || om_pgs_reserved(nm)) continue;
            if (have_last && memcmp(nm, last, 12) <= 0) continue;
            if (!pick || memcmp(nm, best, 12) < 0) { pick = d; memcpy(best, nm, 12); }
        }
        if (!pick) break;
        {
            uint8_t rec[19];
            memcpy(rec, best, 11);
            om_put32(rec + 11, pick->date);
            om_put32(rec + 15, pick->datasize);
            crc = om_crc32(crc, rec, sizeof rec);
        }
        memcpy(last, best, 12);
        have_last = 1;
        c++;
    }
    if (count) *count = c;
    return crc;
}

int om_pgs_supersedes(uint32_t new_date, uint32_t old_date, int same)
{
    return new_date > old_date || (new_date == old_date && same);
}

/* --- the journal line ------------------------------------------------------------------------------------------- */
enum { J_OP, J_PH, J_UNDO, J_GID, J_SLOT, J_PGS, J_SHARE, J_HAD, J_MARKER, J_MGID, J_MSLOT, J_FOREIGN, J_Q, J_FP,
       J_FN, J_G, J_N };
static const char *const k_keys[J_N] = { "op", "ph", "undo", "gid", "slot", "pgs", "share", "had", "marker", "mgid",
                                         "mslot", "foreign", "q", "fp", "fn", "g" };

typedef struct { char *o; uint32_t left; int err; } sbuf;
static void ps(sbuf *b, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    if (n >= b->left) { b->err = 1; return; }
    memcpy(b->o, s, n + 1);
    b->o += n; b->left -= n;
}
static void pkv(sbuf *b, int k, uint32_t v, const char *str)
{
    char t[12];
    if (k) ps(b, " ");
    ps(b, k_keys[k]);
    ps(b, "=");
    if (str) { ps(b, str[0] ? str : "-"); return; }
    *put_u(t, v) = 0;
    ps(b, t);
}

int om_pgs_journal_fmt(char *out, uint32_t cap, const om_pgs_journal *j)
{
    sbuf b = { out, cap, 0 };
    const om_pgs_state *s = &j->s;
    if (!cap) return OM_E_FULL;
    out[0] = 0;
    pkv(&b, J_OP, j->op, 0); pkv(&b, J_PH, j->ph, 0); pkv(&b, J_UNDO, j->undo, 0);
    pkv(&b, J_GID, 0, s->want_gid); pkv(&b, J_SLOT, s->want_slot, 0); pkv(&b, J_PGS, s->want_pgs, 0);
    pkv(&b, J_SHARE, s->share, 0); pkv(&b, J_HAD, s->slot_exists, 0); pkv(&b, J_MARKER, s->has_marker, 0);
    pkv(&b, J_MGID, 0, s->has_marker ? s->marker_gid : ""); pkv(&b, J_MSLOT, s->marker_slot, 0);
    pkv(&b, J_FOREIGN, s->foreign, 0); pkv(&b, J_Q, s->qnum, 0);
    {
        char x[9];
        put_x(x, j->fp)[0] = 0;
        ps(&b, " fp="); ps(&b, x);
    }
    pkv(&b, J_FN, j->fn, 0);
    if (j->g) pkv(&b, J_G, j->g, 0);                  /* optional, so every earlier line still parses */
    ps(&b, "\n");
    return b.err ? OM_E_FULL : (int)(b.o - out);
}

int om_pgs_journal_parse(const char *t, uint32_t n, om_pgs_journal *j)
{
    uint32_t i = 0, seen = 0, v[J_N];
    char g[2][OM_GID_MAX];
    memset(j, 0, sizeof *j);
    memset(v, 0, sizeof v);
    g[0][0] = g[1][0] = 0;
    while (i < n && t[i] != '\n') {
        uint32_t a = i, k, len, e;
        while (i < n && t[i] != '=' && t[i] != ' ' && t[i] != '\n') i++;
        for (k = 0; k < J_N; k++)
            if (strlen(k_keys[k]) == i - a && !memcmp(t + a, k_keys[k], i - a)) break;
        if (k == J_N || i >= n || t[i] != '=' || (seen & (1u << k))) return OM_E_FIELD;
        a = ++i;
        while (i < n && t[i] != ' ' && t[i] != '\n') i++;
        len = i - a;
        if (!len) return OM_E_FIELD;
        if (k == J_GID || k == J_MGID) {
            char *d = g[k == J_MGID];
            if (len >= OM_GID_MAX) return OM_E_FIELD;
            if (!(len == 1 && t[a] == '-')) { memcpy(d, t + a, len); d[len] = 0; }
        } else if (k == J_FP) {
            if (len != 8 || !hex8((const uint8_t *)t + a, &v[k])) return OM_E_FIELD;
        } else {
            for (e = a; e < i; e++) {
                if (t[e] < '0' || t[e] > '9' || len > 5) return OM_E_FIELD;
                v[k] = v[k] * 10u + (uint32_t)(t[e] - '0');
            }
        }
        seen |= 1u << k;
        if (i < n && t[i] == ' ') i++;
    }
    if ((seen | 1u << J_G) != (1u << J_N) - 1u || v[J_G] > 99999u                 /* g: optional */
        || v[J_OP] >= OM_PGS_MAX_OPS || v[J_PH] > 9 || v[J_UNDO] > 1 || v[J_SLOT] > 9
        || v[J_PGS] > 1 || v[J_SHARE] > 1 || v[J_HAD] > 1 || v[J_MARKER] > 1 || v[J_MSLOT] > 9
        || v[J_FOREIGN] > 1 || v[J_Q] > 65535u || (v[J_FOREIGN] && !v[J_Q]) || v[J_FN] > 65535u
        || (v[J_PGS] && om_gameid_check(g[0])) || (v[J_MARKER] && om_gameid_check(g[1])))
        return OM_E_FIELD;
    j->op = (uint8_t)v[J_OP]; j->ph = (uint8_t)v[J_PH]; j->undo = (uint8_t)v[J_UNDO];
    strcpy(j->s.want_gid, g[0]); strcpy(j->s.marker_gid, g[1]);
    j->s.want_slot = (uint8_t)v[J_SLOT]; j->s.want_pgs = (uint8_t)v[J_PGS]; j->s.share = (uint8_t)v[J_SHARE];
    j->s.slot_exists = (uint8_t)v[J_HAD]; j->s.has_marker = (uint8_t)v[J_MARKER];
    j->s.marker_slot = (uint8_t)v[J_MSLOT];
    j->s.foreign = (uint8_t)v[J_FOREIGN]; j->s.qnum = (uint16_t)v[J_Q];
    j->fp = v[J_FP]; j->fn = (uint16_t)v[J_FN]; j->g = v[J_G];
    return OM_OK;
}

/* --- archive file names ----------------------------------------------------------------------------------------- */
static int ieq(const char *a, const char *b)
{
    for (;; a++, b++) {
        char x = *a >= 'a' && *a <= 'z' ? (char)(*a - 32) : *a, y = *b >= 'a' && *b <= 'z' ? (char)(*b - 32) : *b;
        if (x != y) return 0;
        if (!x) return 1;
    }
}

int om_pgs_file_name(char out[OM_PGS_FNAME], const char name[12], const char (*taken)[OM_PGS_FNAME], uint32_t ntaken)
{
    char nm[12];
    uint32_t n, i, t;
    out[0] = 0;
    if (!om_bup_name_norm(nm, name))
        return OM_E_FIELD;
    for (n = 0; n < 11 && nm[n]; n++) {
        char c = nm[n];
        out[n] = (char)(((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') ? c : '_');
    }
    for (i = 0; i <= 9; i++) {
        uint32_t e = n;
        if (i) { out[e++] = '~'; out[e++] = (char)('0' + i); }
        memcpy(out + e, ".BUP", 5);
        for (t = 0; t < ntaken && !ieq(out, taken[t]); t++) ;
        if (t == ntaken) return OM_OK;
    }
    out[0] = 0;
    return OM_E_FULL;
}

uint32_t om_pgs_blocks(uint32_t datasize, uint32_t file_blocks)
{
    return file_blocks ? file_blocks : 1u + (datasize + 0x1Du) / 58u;
}

void om_pgs_fit_msg(char *out, uint32_t cap, uint32_t blocks, uint32_t block_size)
{
    char t[48], *o;
    uint32_t n;
    memcpy(t, "SAVES: ", 7);
    o = put_u(t + 7, (uint32_t)(((uint64_t)blocks * block_size + 1023u) / 1024u));
    memcpy(o, " KB DO NOT FIT IN 32 KB BRAM", 29);
    if (!cap) return;
    n = (uint32_t)strlen(t);
    if (n >= cap) n = cap - 1;
    memcpy(out, t, n);
    out[n] = 0;
}
