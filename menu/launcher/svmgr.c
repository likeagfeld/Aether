/* menu/launcher/svmgr.c -- see svmgr.h. BSD-3-Clause. */
#include <string.h>
#include "bram.h"
#include "om_fmt.h"
#include "pgsbak.h"
#include "satiator.h"
#include "savegen.h"
#include "sdwrite.h"
#include "svmgr.h"

#define JOURNAL  "SAVES/JOURNAL.TXT"
#define ACTIVE   "SAVES/ACTIVE.TXT"
#define GENERAL  "_GENERAL"
#define SEQ_MAX  99999u

static uint32_t s_al[OM_SV_PATH / 4 + 8];
static uint32_t s_st[(300 + 4) / 4];
static om_gen_scr s_gs;

static const char *al(const char *p)
{
    uint32_t n = (uint32_t)strlen(p);
    if (n >= sizeof s_al) n = sizeof s_al - 1;
    memcpy(s_al, p, n);
    ((char *)s_al)[n] = 0;
    return (const char *)s_al;
}
static int absent(int rc) { return rc == -FR_NO_FILE || rc == -FR_NO_PATH; }
static int exists(const char *p) { return s_stat(al(p), (s_stat_t *)s_st, (int)sizeof s_st - 4) >= 0; }
static int is_dir(const char *p)
{
    return s_stat(al(p), (s_stat_t *)s_st, (int)sizeof s_st - 4) >= 0 && (((s_stat_t *)s_st)->attrib & AM_DIR);
}
static int mkdir_p(const char *p)
{   /* one level; an existing folder is fine */
    int rc = s_mkdir(al(p));
    if (rc >= 0 || is_dir(p)) return 0;
    return OM_SDW_ERR(rc);
}
static int ends_bup(const char *n)
{
    size_t k = strlen(n);
    return k > 4 && n[k - 4] == '.' && (n[k - 3] | 32) == 'b' && (n[k - 2] | 32) == 'u' && (n[k - 1] | 32) == 'p';
}
static int cat(char *out, uint32_t cap, const char *a, const char *b)
{
    size_t x = strlen(a), y = strlen(b);
    if (x + 1 + y + 1 > cap) return OM_E_FULL;
    memcpy(out, a, x);
    out[x] = '/';
    memcpy(out + x + 1, b, y + 1);
    return 0;
}

const char *om_sv_why(int r)
{
    switch (r) {
    case 0:              return "Done";
    case OM_SVR_JOURNAL: return "A save swap is unfinished: launch a game first";
    case OM_SVR_KAI:     return "Pseudo Saturn Kai's saves are in console memory";
    case OM_SVR_OTHERSET:return "Console memory holds another game's saves: launch this one first";
    case OM_SVR_FOREIGN: return "Console saves changed outside Aether: launch a game first";
    case OM_SVR_NOROOM:  return "Not enough room there";
    case OM_SVR_RESERVED:return "A system record: left as it is";
    case OM_SVR_TOOBIG:  return "Over 93 KB: copy this save on a PC";
    case OM_SVR_NODEV:   return "Not there (no cartridge, or not formatted)";
    case OM_SVR_GENS:    return "This save folder is full: fix it on a PC";
    case OM_SVR_BAD:     return "This save file does not read";
    default:             return r <= -1000 ? "Console memory or cartridge error" : "Card error";
    }
}

/* --- the console and the cartridge ---------------------------------------------------------------------------------- */
static int use(om_sv *v, int dev)
{
    int rc = dev == OM_SV_CART ? v->cart_rc : v->con_rc;
    if (rc) return OM_SVR_NODEV;
    return om_bram_use(dev == OM_SV_CART ? 1u : 0u) ? OM_SVR_NODEV : 0;
}

int om_sv_open(om_sv *v, const om_pgs_env *e, const char *gid, uint8_t *big, uint32_t bigcap, om_sv_item *items,
               uint32_t cap, const char *date)
{
    om_bup_dir d;
    memset(v, 0, sizeof *v);
    v->e = e;
    v->big = big;
    v->bigcap = bigcap;
    v->it = items;
    v->cap = cap;
    v->date = date ? date : "";
    strncpy(v->gid, gid ? gid : "", sizeof v->gid - 1);
    v->journal = (uint8_t)(exists(JOURNAL) || exists(JOURNAL ".NEW"));   /* as the swap reads it (sdwrite.h) */
    v->con_rc = om_bram_init(e->lib, e->work);
    v->cart_rc = v->con_rc ? v->con_rc : om_bram_use(1);
    if (!v->con_rc) {
        om_bram_use(0);
        if (om_bram_read(OM_PGS_MARKER, e->rec, OM_PGS_REC, &d) == 0 &&
            om_pgs_marker_parse(e->rec, d.datasize, v->mgid, &v->mslot, &(uint32_t){ 0 }, 0) == OM_OK) {
            char ag[OM_GID_MAX];
            uint8_t as;
            uint32_t mn = 0, an = 1, fp, len;
            v->marker = 1;
            om_pgs_marker_parse(e->rec, d.datasize, v->mgid, &v->mslot, &mn, 0);
            v->foreign = !(om_sdw_read_head(ACTIVE, e->chk, 260, &len) == 0 &&
                           om_pgs_marker_parse(e->chk, len, ag, &as, &an, &fp) == OM_OK &&
                           !strcmp(ag, v->mgid) && as == v->mslot && an == mn);
        }
        v->kai = om_bram_read(OM_PGS_KAI_MARKER, e->rec, OM_PGS_REC, &d) == 0;
    }
    return om_sv_set_folder(v, "");
}

static int list_dev(om_sv *v, int dev)
{
    uint32_t n, i;
    int rc;
    if ((rc = use(v, dev)) != 0) return rc;
    if ((rc = om_bram_list(v->e->list, OM_PGS_LIST, &n)) != 0 && rc != OM_E_TABLESIZE) return rc;
    if (n > OM_PGS_LIST) n = OM_PGS_LIST;
    for (i = 0; i < n && v->n < v->cap; i++) {
        om_sv_item *x = &v->it[v->n];
        char nm[12];
        if (!om_bup_name_norm(nm, v->e->list[i].name) || om_pgs_reserved(nm)) continue;
        memset(x, 0, sizeof *x);
        x->d = v->e->list[i];
        memcpy(x->d.name, nm, 12);
        x->dev = (uint8_t)dev;
        x->mine = 1;
        v->n++;
    }
    return 0;
}

/* --- the card's sets -------------------------------------------------------------------------------------------- */
static int folder_of_gid(const char *gid, char out[OM_PGS_DIR])
{   /* the game's slot-0 folder by Kai's product-only rule (pgsrun.c): the product alone when only that one exists */
    char reg[OM_PGS_DIR], po[OM_PGS_DIR];
    if (om_pgs_slot_dir(reg, gid, 0) != OM_OK) return OM_E_FIELD;
    if (om_pgs_product_dir(po, gid) == OM_OK && !is_dir(reg) && is_dir(po)) { strcpy(out, po + 6); return 0; }
    strcpy(out, reg + 6);                                          /* past "SAVES/" */
    return 0;
}

int om_sv_set_folder(om_sv *v, const char *folder)
{
    uint32_t s;
    v->nset = 0;
    v->folder[0] = 0;
    if (folder && folder[0]) strncpy(v->folder, folder, sizeof v->folder - 1);
    else if (v->gid[0] && folder_of_gid(v->gid, v->folder)) v->folder[0] = 0;
    if (!v->folder[0]) return 0;
    for (s = 0; s < 10u; s++) {
        om_sv_set *t = &v->set[v->nset];
        uint32_t g;
        char d[OM_PGS_DIR + 8];
        int rc;
        if (cat(d, sizeof d, "SAVES", v->folder)) return OM_E_FULL;
        if (s) { size_t k = strlen(d); d[k] = '/'; d[k + 1] = '0'; d[k + 2] = (char)('0' + s); d[k + 3] = 0; }
        if (strlen(d) >= sizeof t->slot_dir || strlen(d) + OM_GEN_SUFFIX >= sizeof t->cur) break;   /* too long */
        if (!is_dir(d)) { if (!s) break; continue; }
        strcpy(t->slot_dir, d);
        if ((rc = om_gen_cur(d, t->cur, &g, &s_gs)) != 0) return rc;
        t->slot = (uint8_t)s;
        t->general = !strcmp(v->folder, GENERAL);
        v->nset++;
        if (t->general) break;                                    /* the general set has no slots */
    }
    return 0;
}

int om_sv_folders(om_sv *v, char (*out)[OM_PGS_DIR], uint32_t cap, uint32_t *n)
{
    s_stat_t *st = (s_stat_t *)s_st;
    int len, rc;
    (void)v;
    *n = 0;
    if (is_dir("SAVES/" GENERAL) && cap) strcpy(out[(*n)++], GENERAL);
    if ((rc = s_opendir(al("SAVES"))) < 0) return absent(rc) ? 0 : OM_SDW_ERR(rc);
    while ((len = s_stat(NULL, st, (int)sizeof s_st - 4)) > 0) {
        st->name[len] = 0;
        if (!(st->attrib & AM_DIR) || st->name[0] == '_' || st->name[0] == '.' || len >= (int)OM_PGS_DIR) continue;
        if (*n < cap) strcpy(out[(*n)++], st->name);
    }
    return len == SAT_ETIMEDOUT ? OM_SDW_ERR(len) : 0;
}

static int read_rec(const char *path, uint8_t *buf, uint32_t cap, uint32_t *len, om_bup_dir *d)
{
    int rc = om_sdw_read_file(path, buf, cap, len);
    if (rc == OM_E_FULL) return OM_SVR_TOOBIG;
    if (rc) return rc;
    return om_bup_decode_hdr(buf, *len, d) == OM_OK ? 0 : OM_SVR_BAD;
}

/* the set's files: main (alt 0) or ALT/ (alt 1), each decoded; the §5.16 winner per record name for the main set */
static int list_set(om_sv *v, uint32_t si, int alt)
{
    s_stat_t *st = (s_stat_t *)s_st;
    char dir[OM_SV_PATH], path[OM_SV_PATH + 24];
    int len, rc;
    uint32_t first = v->n, k;
    strcpy(dir, v->set[si].cur);
    if (alt && cat(dir, sizeof dir, v->set[si].cur, "ALT")) return OM_E_FULL;
    if ((rc = s_opendir(al(dir))) < 0) return absent(rc) ? 0 : OM_SDW_ERR(rc);
    /* the listing first (one folder open at a time), then each file's header */
    while ((len = s_stat(NULL, st, (int)sizeof s_st - 4)) > 0) {   /* read to its end (one listing at a time) */
        om_sv_item *x = &v->it[v->n];
        st->name[len] = 0;
        if (v->n >= v->cap || (st->attrib & AM_DIR) || !ends_bup(st->name) || len >= (int)OM_PGS_FNAME) continue;
        memset(x, 0, sizeof *x);
        strcpy(x->file, st->name);
        x->d.datasize = st->size;                                 /* the file's size, until its header is read */
        x->dev = OM_SV_SD;
        x->set = (uint8_t)si;
        x->alt = (uint8_t)alt;
        x->mine = 1;
        v->n++;
    }
    if (len == SAT_ETIMEDOUT) return OM_SDW_ERR(len);
    for (k = first; k < v->n; ) {
        om_sv_item *x = &v->it[k];
        uint32_t flen, j;
        int dup = 0;
        uint32_t fsize = x->d.datasize;
        if (cat(path, sizeof path, dir, x->file) || om_sdw_read_head(path, v->e->chk, 260, &flen) ||
            flen < OM_BUP_HDR || om_bup_decode_hdr(v->e->chk, fsize, &x->d) != OM_OK ||
            om_pgs_reserved(x->d.name)) {
            v->it[k] = v->it[--v->n];                             /* a file that does not decode is not listed */
            continue;
        }
        om_bup_encode_hdr(&x->d, v->e->rec);
        for (j = first; j < k && !alt; j++)                       /* the same record twice: keep the §5.16 winner */
            if (!memcmp(v->it[j].d.name, x->d.name, 12)) {
                uint8_t hj[OM_BUP_HDR];
                om_bup_encode_hdr(&v->it[j].d, hj);
                if (om_bup_prefer(v->e->rec, x->file, hj, v->it[j].file)) v->it[j] = *x;
                dup = 1;
                break;
            }
        if (dup) { v->it[k] = v->it[--v->n]; continue; }
        if (alt) {                                                /* its file name, from the set's folder */
            char f[OM_PGS_FNAME + 4];
            strcpy(f, "ALT/");
            strcat(f, x->file);
            strcpy(x->file, f);
        }
        k++;
    }
    return 0;
}

int om_sv_list(om_sv *v, int dev)
{
    uint32_t i;
    int rc = 0;
    v->n = 0;
    if (dev != OM_SV_SD) {
        if ((rc = list_dev(v, dev)) != 0) return rc;
        if (v->gid[0] && !(v->marker && dev == OM_SV_CON && !strcmp(v->mgid, v->gid))) {
            /* opened for a game: its saves are the names its card sets hold (or all of BRAM when it holds its set) */
            uint32_t k, m = v->n, s;
            om_sv_item *keep = v->it;
            uint32_t cap = v->cap;
            for (k = 0; k < m; k++) keep[k].mine = 0;
            v->it += m; v->cap -= m; v->n = 0;
            for (s = 0; s < v->nset; s++) list_set(v, s, 0);
            for (k = 0; k < m; k++)
                for (i = 0; i < v->n; i++)
                    if (!memcmp(keep[k].d.name, v->it[i].d.name, 12)) keep[k].mine = 1;
            v->it = keep; v->cap = cap; v->n = 0;
            for (k = 0; k < m; k++)                            /* opened for a game: only its saves */
                if (keep[k].mine) keep[v->n++] = keep[k];
        }
        return 0;
    }
    for (i = 0; i < v->nset && !rc; i++)
        if (!(rc = list_set(v, i, 0))) rc = list_set(v, i, 1);
    return rc;
}

/* --- one record into v->big (header + data) --------------------------------------------------------------------- */
static int load(om_sv *v, const om_sv_item *x, uint32_t *len, om_bup_dir *d)
{
    int rc;
    if (om_pgs_reserved(x->d.name)) return OM_SVR_RESERVED;
    if (x->dev == OM_SV_SD) {
        char path[OM_SV_PATH + 24];
        if (x->set >= v->nset || cat(path, sizeof path, v->set[x->set].cur, x->file)) return OM_E_FIELD;
        return read_rec(path, v->big, v->bigcap, len, d);
    }
    if ((rc = use(v, x->dev)) != 0) return rc;
    if (x->d.datasize + OM_BUP_HDR + 4u > v->bigcap) return OM_SVR_TOOBIG;
    if ((rc = om_bram_read(x->d.name, v->big + OM_BUP_HDR, v->bigcap - OM_BUP_HDR, d)) != 0)
        return rc == OM_E_FULL ? OM_SVR_TOOBIG : rc > 0 ? OM_SV_LIB(rc) : rc;
    memcpy(d->name, x->d.name, 12);
    if ((rc = om_bup_encode_hdr(d, v->big)) != OM_OK) return rc;
    *len = OM_BUP_HDR + d->datasize;
    return 0;
}

/* a new folder under SAVES/_MANAGER: one above every number there */
static int new_seq(char out[OM_SV_PATH], uint32_t *seq)
{
    s_stat_t *st = (s_stat_t *)s_st;
    uint32_t mx = 0;
    int len, rc;
    /* SAVES first: a card no game has launched from has none yet */
    if ((rc = mkdir_p("SAVES")) != 0 || (rc = mkdir_p(OM_SV_MGR)) != 0) return rc;
    if ((rc = s_opendir(al(OM_SV_MGR))) < 0) return OM_SDW_ERR(rc);
    while ((len = s_stat(NULL, st, (int)sizeof s_st - 4)) > 0) {
        uint32_t k, n = 0;
        st->name[len] = 0;
        for (k = 0; st->name[k] >= '0' && st->name[k] <= '9' && k < 6u; k++) n = n * 10u + (uint32_t)(st->name[k] - '0');
        if (k && n > mx) mx = n;
    }
    if (len == SAT_ETIMEDOUT) return OM_SDW_ERR(len);
    if (mx >= SEQ_MAX) return OM_SVR_GENS;
    *seq = mx + 1u;
    strcpy(out, OM_SV_MGR "/00000");
    for (len = 0, mx = *seq; len < 5; len++, mx /= 10u) out[sizeof OM_SV_MGR + 4 - len] = (char)('0' + mx % 10u);
    return mkdir_p(out);
}

static int save_copy(om_sv *v, uint32_t len, const om_bup_dir *d, uint32_t *seq)
{   /* v->big (a whole record file) into a new SAVES/_MANAGER/<seq>/<name>.BUP, written and read back */
    char dir[OM_SV_PATH], fn[OM_PGS_FNAME], path[OM_SV_PATH + OM_PGS_FNAME];
    om_sdw_stats st;
    int rc;
    char tmp[OM_SV_PATH + OM_PGS_FNAME];
    if ((rc = om_pgs_file_name(fn, d->name, 0, 0)) != OM_OK) return rc;
    if ((rc = new_seq(dir, seq)) != 0) return rc;
    if (cat(path, sizeof path, dir, fn)) return OM_E_FULL;
    strcpy(tmp, path);
    strcpy(tmp + strlen(tmp) - 4, ".TMP");                    /* a cut leaves a .TMP, never a short .BUP */
    memset(&st, 0, sizeof st);
    if ((rc = om_sdw_write_file(tmp, v->big, len, 1, v->e->scr, v->e->gap, &st)) != 0) return rc;
    {
        static uint32_t a[(OM_SV_PATH + OM_PGS_FNAME) / 4 + 1], b[(OM_SV_PATH + OM_PGS_FNAME) / 4 + 1];
        strcpy((char *)a, tmp);
        strcpy((char *)b, path);
        rc = s_rename((const char *)a, (const char *)b);
    }
    return rc < 0 ? OM_SDW_ERR(rc) : 0;
}

int om_sv_backup(om_sv *v, const om_sv_item *x, uint32_t *seq)
{
    om_bup_dir d;
    uint32_t len, sq = 0;
    int rc;
    if (v->journal) return OM_SVR_JOURNAL;
    if ((rc = load(v, x, &len, &d)) != 0) return rc;
    rc = save_copy(v, len, &d, &sq);
    if (seq) *seq = sq;
    return rc;
}

/* before a save leaves the console memory or is written over there: the whole memory as a .BKP generation (pgsbak.h),
 * and the save itself into SAVES/_MANAGER; a cartridge save, into SAVES/_MANAGER */
static int guard(om_sv *v, int dev, const char name[12])
{
    om_sv_item t;
    uint32_t seq;
    int rc;
    if (dev == OM_SV_CON) {
        om_pgs_bak r;
        char note[OM_PGS_MSG];
        if ((rc = use(v, OM_SV_CON)) != 0) return rc;
        memset(&r, 0, sizeof r);
        om_pgs_backup_rel(1);
        om_pgs_backup(v->e, v->date, &r, note);
        om_pgs_backup_rel(0);
        if (r.rc) return r.rc;
    }
    memset(&t, 0, sizeof t);
    memcpy(t.d.name, name, 12);
    t.dev = (uint8_t)dev;
    return om_sv_backup(v, &t, &seq);
}

static int has(om_sv *v, int dev, const char name[12], om_bup_dir *d)
{   /* 1: dev holds a record of that name (d filled) -- looked up by name (BUP_Dir), however many records it holds;
     * a lookup that cannot tell says yes, so the caller asks and backs up first */
    int rc;
    if (use(v, dev)) return 0;
    memset(d, 0, sizeof *d);
    rc = om_bram_read(name, v->e->chk, 0, d);                  /* cap 0: found = OM_E_FULL (or 0 for an empty one) */
    return rc != OM_BUP_NOTFOUND;
}

static int set_matches_marker(om_sv *v, const om_sv_set *t)
{   /* the console holds this set: its game folder and slot are the marker's */
    char f[OM_PGS_DIR];
    if (!v->marker || t->general) return 0;
    if (folder_of_gid(v->mgid, f) || strcmp(f, v->folder)) return 0;
    return t->slot == v->mslot;
}

int om_sv_copy(om_sv *v, const om_sv_item *x, int to, int confirm)
{
    om_bup_dir d, old;
    om_bram_info bi;
    uint32_t len;
    int rc;
    if (to != OM_SV_CON && to != OM_SV_CART) return OM_E_FIELD;
    if (v->journal) return OM_SVR_JOURNAL;
    if (to == x->dev) return OM_E_FIELD;
    if (to == OM_SV_CON) {
        if (v->kai) return OM_SVR_KAI;
        if (v->foreign) return OM_SVR_FOREIGN;
        if (v->marker) {                                           /* only into this save's own set */
            if (x->dev == OM_SV_SD ? !set_matches_marker(v, &v->set[x->set])
                                   : !(v->gid[0] && !strcmp(v->mgid, v->gid)))
                return OM_SVR_OTHERSET;
        }                                                          /* no marker: the general set, any save */
    }
    if ((rc = load(v, x, &len, &d)) != 0) return rc;
    if (use(v, to)) return OM_SVR_NODEV;
    if (has(v, to, d.name, &old)) {
        if (!confirm) return OM_SV_ASK;
        if ((rc = guard(v, to, d.name)) != 0) return rc;
        if ((rc = load(v, x, &len, &d)) != 0) return rc;          /* the guard used the buffers */
    } else {
        if ((rc = use(v, to)) != 0) return rc;
        if (om_bram_info_get(d.datasize, &bi) == 0 && !bi.datanum) return OM_SVR_NOROOM;
    }
    if ((rc = use(v, to)) != 0) return rc;
    rc = om_bram_write(&d, v->big + OM_BUP_HDR);
    return rc == OM_BUP_NOMEM ? OM_SVR_NOROOM : rc > 0 ? OM_SV_LIB(rc) : rc;
}

/* an SD save goes: a new generation of its slot folder with every other file of the current set (and its ALT/), then
 * DONE.TXT -- from then on that generation is the set (savegen.h); the old one keeps the save */
static int copy_file(om_sv *v, const char *from, const char *to)
{
    om_sdw_stats st;
    uint32_t len;
    int rc = om_sdw_read_file(from, v->big, v->bigcap, &len);
    if (rc == OM_E_FULL) return OM_SVR_TOOBIG;
    if (rc) return rc;
    memset(&st, 0, sizeof st);
    return om_sdw_write_file(to, v->big, len, 1, v->e->scr, v->e->gap, &st);
}

static int copy_dir(om_sv *v, const char *from, const char *to, const char *skip_name, const char *skip_file)
{
    s_stat_t *st = (s_stat_t *)s_st;
    om_pgs_ent *ents = v->e->ents;
    uint32_t n = 0, k;
    int len, rc;
    if ((rc = s_opendir(al(from))) < 0) return absent(rc) ? 0 : OM_SDW_ERR(rc);
    while ((len = s_stat(NULL, st, (int)sizeof s_st - 4)) > 0) {
        st->name[len] = 0;
        if ((st->attrib & AM_DIR) || !ends_bup(st->name)) continue;
        if (n >= OM_PGS_ENTS || len >= (int)OM_PGS_ENAME) return OM_SVR_GENS;
        strcpy(ents[n++].name, st->name);
    }
    if (len == SAT_ETIMEDOUT) return OM_SDW_ERR(len);
    if (n && (rc = mkdir_p(to)) != 0) return rc;
    for (k = 0; k < n; k++) {
        char a[OM_SV_PATH + OM_PGS_ENAME], b[OM_SV_PATH + OM_PGS_ENAME];
        uint32_t flen;
        om_bup_dir d;
        if (skip_file && !strcmp(ents[k].name, skip_file)) continue;
        if (cat(a, sizeof a, from, ents[k].name) || cat(b, sizeof b, to, ents[k].name)) return OM_E_FULL;
        if (skip_name && om_sdw_read_head(a, v->e->chk, 260, &flen) == 0 &&
            flen >= OM_BUP_HDR && memcmp(v->e->chk, OM_BUP_MAGIC, 4) == 0 &&
            om_bup_decode_hdr(v->e->chk, OM_BUP_HDR + om_be32(v->e->chk + 0x2C), &d) == OM_OK &&
            !memcmp(d.name, skip_name, 12))
            continue;                                              /* every copy of the record goes from the set */
        if ((rc = copy_file(v, a, b)) != 0) return rc;
    }
    return 0;
}

static int sd_delete(om_sv *v, const om_sv_item *x)
{
    const om_sv_set *t = &v->set[x->set];
    char g[OM_SV_PATH], sh[OM_SV_PATH], from[OM_SV_PATH], to[OM_SV_PATH], done[OM_SV_PATH + 12];
    om_sdw_stats st;
    uint32_t mx;
    int rc;
    if (strlen(t->slot_dir) + OM_GEN_SUFFIX + 8u >= sizeof g) return OM_E_FULL;
    if ((rc = om_gen_max(t->slot_dir, &mx, &s_gs)) != 0) return rc;
    if (mx >= OM_GEN_MAX) return OM_SVR_GENS;
    om_gen_shard(sh, t->slot_dir, mx + 1u);
    om_gen_dir(g, t->slot_dir, mx + 1u);
    if ((rc = mkdir_p(sh)) != 0 || (rc = mkdir_p(g)) != 0) return rc;
    if ((rc = copy_dir(v, t->cur, g, x->alt ? 0 : x->d.name, 0)) != 0) return rc;
    if (cat(from, sizeof from, t->cur, "ALT") || cat(to, sizeof to, g, "ALT")) return OM_E_FULL;
    if ((rc = copy_dir(v, from, to, 0, x->alt ? x->file + 4 : 0)) != 0) return rc;
    if (cat(done, sizeof done, g, OM_GEN_DONE)) return OM_E_FULL;
    memset(&st, 0, sizeof st);
    if ((rc = om_sdw_write_file(done, v->big, 0, 1, v->e->scr, v->e->gap, &st)) != 0) return rc;
    return om_gen_cur(t->slot_dir, ((om_sv_set *)t)->cur, &mx, &s_gs);
}

int om_sv_delete(om_sv *v, const om_sv_item *x)
{
    int rc;
    if (v->journal) return OM_SVR_JOURNAL;
    if (om_pgs_reserved(x->d.name)) return OM_SVR_RESERVED;
    if (x->dev == OM_SV_SD) return x->set < v->nset ? sd_delete(v, x) : OM_E_FIELD;
    if (x->dev == OM_SV_CON && v->kai) return OM_SVR_KAI;
    if (x->dev == OM_SV_CON && v->foreign) return OM_SVR_FOREIGN;
    if ((rc = guard(v, x->dev, x->d.name)) != 0) return rc;
    if ((rc = use(v, x->dev)) != 0) return rc;
    rc = om_bram_delete(x->d.name);
    return rc > 0 ? OM_SV_LIB(rc) : rc;
}

int om_sv_set_loaded(om_sv *v, uint32_t si)
{
    if (si >= v->nset || v->con_rc) return 0;
    return v->marker ? set_matches_marker(v, &v->set[si]) : v->set[si].general;
}

int om_sv_backup_all(om_sv *v, uint32_t *seq, int *same)
{
    om_pgs_bak r;
    char note[OM_PGS_MSG];
    int rc;
    if (v->journal) return OM_SVR_JOURNAL;
    if ((rc = use(v, OM_SV_CON)) != 0) return rc;
    memset(&r, 0, sizeof r);
    om_pgs_backup_rel(1);
    om_pgs_backup(v->e, v->date, &r, note);
    om_pgs_backup_rel(0);
    if (seq) *seq = r.seq;
    if (same) *same = r.same;
    return r.rc;
}

int om_sv_in_console(om_sv *v, const om_sv_item *x, om_sv_item *con)
{
    om_bup_dir d;
    const om_sv_set *t;
    if (x->dev != OM_SV_SD || x->set >= v->nset || v->con_rc) return 0;
    t = &v->set[x->set];
    if (v->marker ? !set_matches_marker(v, t) : !t->general) return 0;
    if (!has(v, OM_SV_CON, x->d.name, &d)) return 0;
    memset(con, 0, sizeof *con);
    con->d = d;
    memcpy(con->d.name, x->d.name, 12);
    con->dev = OM_SV_CON;
    return 1;
}
