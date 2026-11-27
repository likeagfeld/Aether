/* menu/launcher/pgsbak.c -- see pgsbak.h. Runs once per launch (the backup) or once per
 * user request (the import), never per frame. SD calls follow pgsrun.c's discipline: one file or one listing open at a
 * time, a listing read in full before any file in it is opened (satfs.c, HWF-22), names handed to libsatiator from
 * 4-aligned buffers (satiator.c buffer_xfer; s_rename copies both names itself). Every rc is compared with its success
 * value. BSD-3-Clause. */
#include <string.h>
#include "carddir.h"
#include "om_fmt.h"
#include "pgsbak.h"
#include "savegen.h"
#include "satiator.h"
#include "yield.h"

#define PATHMAX 256u
#define NAMEW   24u                                   /* a folder name in the import's name lists (<= 21 + NUL) */
#define DMAX    256u                                  /* a destination or quarantine folder's entries tracked */

static const om_pgs_env *E;
static uint32_t s_al[PATHMAX / 4];
static uint32_t s_st[(300 + 4) / 4];
static uint32_t s_io[(2048 + 4) / 4];                 /* a 2 KB read piece / header */
static uint32_t s_esize[OM_PGS_ENTS];                 /* E->ents[i]'s file size (a listing) */
static om_gen_scr s_gs;                               /* savegen.h's listing buffers */

/* --- strings ---------------------------------------------------------------------------------------------------- */
static char *cat(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }
static char *cat_u(char *o, uint32_t v, int width)
{
    char t[12];
    int k = 0;
    do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v || k < width);
    while (k) *o++ = t[--k];
    *o = 0;
    return o;
}
static char *cat_rc(char *o, int rc)
{
    if (rc < 0) { *o++ = '-'; *o = 0; return cat_u(o, (uint32_t)-rc, 1); }
    return cat_u(o, (uint32_t)rc, 1);
}
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t get32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void jp(char *out, const char *a, const char *b) { cat(cat(cat(out, a), "/"), b); }

int om_pgs_bak_name(char *out, uint32_t cap, uint32_t seq, const char *date, const char *ext)
{
    char t[48], *o;
    if (!seq || seq > 99999u || strlen(date) > 15u || strlen(ext) > 4u) return OM_E_FIELD;
    o = cat_u(t, seq, 5);
    if (date[0]) o = cat(cat(o, "-"), date);
    cat(o, ext);
    if (strlen(t) + 1 > cap) return OM_E_FULL;
    strcpy(out, t);
    return OM_OK;
}

uint32_t om_pgs_bak_seq(const char *f, int *complete)
{
    uint32_t v = 0, k = 0, n = (uint32_t)strlen(f);
    const char *x;
    *complete = 0;
    while (f[k] >= '0' && f[k] <= '9' && k < 5) v = v * 10u + (uint32_t)(f[k++] - '0');
    if (k != 5 || !v || n < 9) return 0;
    if (f[k] != '-' && f[k] != '.') return 0;
    x = f + n - 4;
    if (x[0] != '.') return 0;
    if ((x[1] | 32) == 'b' && (x[2] | 32) == 'k' && (x[3] | 32) == 'p') *complete = 1;
    else if (!((x[1] | 32) == 't' && (x[2] | 32) == 'm' && (x[3] | 32) == 'p')) return 0;
    return v;
}

/* om_pgs_imp_folder, om_pgs_rings_slot: menu/src/impscan.c (the menu's picker scans with them) */

/* --- SD helpers ------------------------------------------------------------------------------------------------- */
static const char *al(const char *p)
{
    uint32_t n = (uint32_t)strlen(p);
    if (n >= PATHMAX) n = PATHMAX - 1;
    memcpy(s_al, p, n);
    ((char *)s_al)[n] = 0;
    return (const char *)s_al;
}
static int sd_rc(int rc) { return rc < 0 ? OM_SDW_ERR(rc) : 0; }

/* the import's progress (pgsbak.h) */
static om_pgs_prog s_pg;
static void emit(void) { if (E && E->prog) E->prog(&s_pg); }
static void beat(uint32_t bytes)
{
    s_pg.beat++;
    s_pg.file_done += bytes;
    if (s_pg.file_done > s_pg.file_total) s_pg.file_done = s_pg.file_total;
    emit();
}
static void pg_file(const char *path, uint32_t total)
{
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    strncpy(s_pg.file, b, sizeof s_pg.file - 1);
    s_pg.file[sizeof s_pg.file - 1] = 0;
    s_pg.file_done = 0;
    s_pg.file_total = total;
    emit();
}
/* the user's cancel (B held, lseq.c), asked only between files and between games: the file in flight is finished
 * (written as .IMP, verified, renamed) before it is asked, so nothing is left half-written; a run again copies only
 * what is missing (the import is idempotent) */
static int stop(void) { return E && E->cancel && E->cancel(); }
static void pg_saves(uint32_t n)
{
    s_pg.saves_done += n;
    if (s_pg.saves_total && s_pg.saves_done > s_pg.saves_total) s_pg.saves_done = s_pg.saves_total;
    emit();
}
void om_pgs_bar(char *out, uint32_t width, uint32_t done, uint32_t total)
{
    uint32_t i, fill = total ? (uint32_t)((uint64_t)done * width / total) : 0;
    uint32_t pc = total ? (uint32_t)((uint64_t)done * 100u / total) : 0;
    if (fill > width) fill = width;
    if (pc > 100) pc = 100;
    for (i = 0; i < width; i++) out[i] = i < fill ? '#' : '-';
    out[width] = ' ';
    out[width + 1] = pc >= 100 ? '1' : ' ';
    out[width + 2] = pc >= 10 ? (char)('0' + pc / 10 % 10) : ' ';
    out[width + 3] = (char)('0' + pc % 10);
    out[width + 4] = '%';
    out[width + 5] = 0;
}
static int absent(int rc) { return rc == -FR_NO_FILE || rc == -FR_NO_PATH; }
static int sd_mkdir(const char *p) { int rc; om_yield(); rc = s_mkdir(al(p)); return rc == -FR_EXIST ? 0 : sd_rc(rc); }
/* the folder chain this run last made (import speed, 2026-11-24): a folder made, or found, once in a run is not asked
 * for again -- every s_mkdir walks the card from its root. Reset at the start of each import. */
static char s_made[PATHMAX];
static int made(const char *t, uint32_t i)
{   /* t (i chars) is s_made or one of its parent folders */
    return !strncmp(s_made, t, i) && (s_made[i] == '/' || !s_made[i]);
}
static int mkdirs(const char *abs)
{   /* every folder of "/aether/SAVES/..." from SAVES down */
    char t[PATHMAX];
    uint32_t i, n = (uint32_t)strlen(abs);
    int rc;
    if (n >= PATHMAX) return OM_E_FIELD;
    for (i = sizeof OM_CARD_ROOT - 1; i <= n; i++)    /* past "/aether/" */
        if (abs[i] == '/' || !abs[i]) {
            memcpy(t, abs, i);
            t[i] = 0;
            if (made(t, i)) continue;
            if ((rc = sd_mkdir(t)) != 0) return rc;
        }
    memcpy(s_made, abs, n + 1);
    return 0;
}
static int is_ext(const char *n, const char *ext)  /* ext: ".bup" lower case */
{
    uint32_t k = (uint32_t)strlen(n);
    const char *x = n + k - 4;
    return k > 4 && x[0] == '.' && (x[1] | 32) == ext[1] && (x[2] | 32) == ext[2] && (x[3] | 32) == ext[3];
}

/* a listing, drained in full; fn gets each entry ("." and ".." dropped); an absent folder lists nothing */
typedef int (*ent_fn)(void *ctx, const char *name, int len, int isdir, uint32_t size);
static int list(const char *p, ent_fn fn, void *ctx)
{
    s_stat_t *st = (s_stat_t *)s_st;
    int len, rc, first = 0;
    uint32_t k = 0;
    om_yield();                                       /* the launch's eye (yield.h): before each path command */
    if ((rc = s_opendir(al(p))) < 0)
        return absent(rc) ? 0 : sd_rc(rc);
    while ((len = s_stat(NULL, st, (int)sizeof s_st - 4)) > 0) {
        if (!(++k & 15u)) om_yield_draw();            /* inside a listing: a frame, no SD command */
        st->name[len] = 0;
        if (!strcmp(st->name, ".") || !strcmp(st->name, "..") || first) continue;
        first = fn(ctx, st->name, len, (st->attrib & AM_DIR) != 0, st->size);   /* an error: drain, then return it */
    }
    if (len == SAT_ETIMEDOUT) return sd_rc(len);
    return first;
}

/* n bytes (<= 2048) at off into buf */
static int read_at(const char *p, uint32_t off, uint32_t n, uint32_t *buf)
{
    int fd, r;
    om_yield();
    if ((fd = s_open(al(p), FA_READ | FA_OPEN_EXISTING)) < 0) return sd_rc(fd);
    r = off ? s_seek(fd, (int)off, C_SEEK_SET) : 0;
    if (r >= 0) r = s_read(fd, buf, (int)n);
    s_close(fd);
    if (r < 0) return sd_rc(r);
    return (uint32_t)r == n ? 0 : OM_E_SHORT;
}
static int read_head(const char *p, uint32_t off, uint32_t n) { return read_at(p, off, n, s_io); }

/* 1 when len bytes at pa+oa equal len bytes at pb+ob; < 0 on an error. One file open at a time, in pieces of
 * CMPPIECE through E->rec and E->chk (free whenever a compare runs: every caller compares before it loads E->rec, and
 * put_big after its last piece). Import speed, 2026-11-24: it used to reopen both files for every 2 KB -- a 512 KB
 * cart save cost 512 path walks from the card's root (~700 entries on the user's exFAT card) for its verify alone. */
static int read_span(const char *p, uint32_t off, uint32_t n, uint8_t *buf);
#define CMPPIECE 32768u
static int same_bytes(const char *pa, uint32_t oa, const char *pb, uint32_t ob, uint32_t len)
{
    uint32_t x, k;
    int rc;
    for (x = 0; x < len; x += k) {
        k = len - x > CMPPIECE ? CMPPIECE : len - x;
        if ((rc = read_span(pa, oa + x, k, E->rec)) != 0 || (rc = read_span(pb, ob + x, k, E->chk)) != 0) return rc;
        if (memcmp(E->rec, E->chk, k)) return 0;
        beat(k);
    }
    return 1;
}

/* 1 when the .BUP files pa (header ha) and pb (header hb) hold the same record version: the same date and datasize and
 * byte-identical data (the 64-byte headers differ between writers: Kai's save_id and 0x34 date, the stats) */
static int same_rec(const char *pa, const uint8_t *ha, const char *pb, const uint8_t *hb)
{
    uint32_t ds = get32(ha + 0x2C);
    if (get32(ha + 0x28) != get32(hb + 0x28) || ds != get32(hb + 0x2C)) return 0;
    return same_bytes(pa, OM_BUP_HDR, pb, OM_BUP_HDR, ds);
}

/* =================================================================================================================
 * IMPORT (on request only; Kai's and Rings' copies of one game merged, newest intact wins)
 * ================================================================================================================= */
static om_pgs_imp *RI;
/* the picked games (IMPORT.SEL, impscan.h), or NULL: every game */
static const char *s_selt;
static uint32_t s_sell;
static int picked(const char *key) { return !s_selt || om_isel_has(s_selt, s_sell, key); }
/* every save file left out is counted and named: "skipped <why> <path>" lines for IMPORT.TXT */
static char s_skip[4096];
static uint32_t s_nskip, s_skipmore;
static uint8_t s_alt_mode;                            /* merge_class into an ALT/ folder (Rings [SLOT10-19], all full) */
static void skip_line(const char *why, const char *path, uint32_t n)
{
    uint32_t a = (uint32_t)strlen(why), b = (uint32_t)strlen(path);
    RI->skipped = (uint16_t)(RI->skipped + n);
    if (s_nskip + a + b + 24 >= sizeof s_skip) { s_skipmore += n; return; }
    s_nskip = (uint32_t)(cat(cat(cat(cat(s_skip + s_nskip, "skipped "), why), " "), path) - s_skip);
    if (n != 1) s_nskip = (uint32_t)(cat_u(cat(s_skip + s_nskip, " x"), n, 1) - s_skip);
    s_skip[s_nskip++] = '\n';
    s_skip[s_nskip] = 0;
}
static void skip_file(const char *path, const char *why) { skip_line(why, path, 1); }

/* every "*.BUP" of a folder (and of its sub-folders, one level) left out: each counted and named */
typedef struct { const char *dir; const char *why; char subs[16][NAMEW]; uint32_t nsub, more; } sk_t;
static int sk_ent(void *ctx, const char *nm, int len, int isdir, uint32_t size)
{
    sk_t *k = (sk_t *)ctx;
    char p[PATHMAX];
    (void)size;
    if (isdir) {
        if ((uint32_t)len < NAMEW && k->nsub < 16) memcpy(k->subs[k->nsub++], nm, (uint32_t)len + 1);
        else k->more++;
        return 0;
    }
    if (!is_ext(nm, ".bup")) return 0;
    jp(p, k->dir, nm);
    skip_file(p, k->why);
    return 0;
}
static int skip_tree(const char *dir, const char *why, int deep)
{
    sk_t k;
    uint32_t i;
    int rc;
    memset(&k, 0, sizeof k);
    k.dir = dir;
    k.why = why;
    if ((rc = list(dir, sk_ent, &k)) != 0) return rc;
    if (k.more) skip_line("too-many-folders", dir, k.more);
    for (i = 0; deep && i < k.nsub; i++) {
        char p[PATHMAX];
        jp(p, dir, k.subs[i]);
        if ((rc = skip_tree(p, why, 0)) != 0) return rc;
    }
    return 0;
}
typedef struct { char (*v)[NAMEW]; uint32_t n, cap; uint32_t over; } names;

static int add_dir(void *ctx, const char *nm, int len, int isdir, uint32_t size)
{
    names *l = (names *)ctx;
    (void)size;
    if (!isdir) return 0;
    if ((uint32_t)len >= NAMEW || l->n >= l->cap) { l->over++; return 0; }
    memcpy(l->v[l->n++], nm, (uint32_t)len + 1);
    return 0;
}

/* one (destination, slot): up to 4 source folders, each tagged (ents[i].isdir = 1 + source index) */
#define SRC_MAX 12
typedef struct { char path[PATHMAX]; uint8_t kai; } srcf;
typedef struct { uint32_t n, over; uint16_t subs; uint8_t tag; } srcl;
static int add_src(void *ctx, const char *nm, int len, int isdir, uint32_t size)
{
    srcl *s = (srcl *)ctx;
    if (isdir) {                                     /* Kai's slot folders 01..09 */
        if (len == 2 && nm[0] == '0' && nm[1] >= '1' && nm[1] <= '9') s->subs |= (uint16_t)(1u << (nm[1] - '0'));
        return 0;
    }
    if (!is_ext(nm, ".bup")) return 0;
    if ((uint32_t)len >= OM_PGS_ENAME || s->n >= OM_PGS_ENTS) { s->over++; return 0; }
    memcpy(E->ents[s->n].name, nm, (uint32_t)len + 1);
    E->ents[s->n].isdir = s->tag;
    s_esize[s->n] = size;
    s->n++;
    return 0;
}

/* a folder's .BUP names and the records they hold (decoded headers); names of OM_PGS_FNAME or more are never ours */
typedef struct { char file[DMAX][OM_PGS_FNAME]; char rec[DMAX][12]; uint32_t n; int scanned, over; } fold_t;
static fold_t s_dst, s_q, s_alt;                     /* s_alt: a slot's ALT/ (fix I8) */
static char s_conf_alt[PATHMAX];
/* the versions imported into _GENERAL (Kai's LATEST), never loaded -- recorded on
 * GENLIST.TXT's "I <id> ..." line, so the general set's next archive keeps one that differs from the console's as an
 * alternate (pgsrun.c cand_act) even with no "R" list */
#define GIMP_MAX 128u
static uint32_t s_gimp[GIMP_MAX], s_ngimp;
static uint8_t s_gen_imp;
static void gimp_note(const char *path)
{   /* path's version id (it decodes: a winner of merge_class) */
    uint32_t len = 0, k, id;
    char nm[12];
    if (!s_gen_imp || s_ngimp == GIMP_MAX || om_sdw_read_file(path, E->rec, OM_PGS_REC, &len) || len < OM_BUP_HDR) return;
    memcpy(nm, E->rec + 0x10, 11);
    nm[11] = 0;
    if (!om_bup_name_norm(nm, nm)) return;
    id = om_pgs_rec_id(nm, get32(E->rec + 0x28), get32(E->rec + 0x2C), om_crc32(0, E->rec + OM_BUP_HDR, len - OM_BUP_HDR));
    for (k = 0; k < s_ngimp && s_gimp[k] != id; k++) ;
    if (k == s_ngimp) s_gimp[s_ngimp++] = id;
}
static int add_fold(void *ctx, const char *nm, int len, int isdir, uint32_t size)
{
    fold_t *f = (fold_t *)ctx;
    (void)size;
    if (isdir || !is_ext(nm, ".bup")) return 0;
    if (f->n >= DMAX) { f->over = 1; return 0; }       /* that folder is skipped, never a stop */
    if ((uint32_t)len >= OM_PGS_FNAME) f->file[f->n][0] = 0;
    else memcpy(f->file[f->n], nm, (uint32_t)len + 1);
    f->rec[f->n][0] = 0;
    f->n++;
    return 0;
}
static int fold_scan(fold_t *f, const char *abs)
{
    char p[PATHMAX];
    uint32_t i;
    om_bup_dir d;
    int rc;
    f->n = 0;
    f->scanned = 1;
    f->over = 0;
    if ((rc = list(abs, add_fold, f)) != 0) return rc;
    for (i = 0; i < f->n; i++) {
        if (!f->file[i][0]) continue;
        jp(p, abs, f->file[i]);
        if ((rc = read_head(p, 0, OM_BUP_HDR)) != 0) { if (rc == OM_E_SHORT) continue; return rc; }
        if (!om_bup_decode_hdr((const uint8_t *)s_io, OM_BUP_HDR + get32((const uint8_t *)s_io + 0x2C), &d))
            memcpy(f->rec[i], d.name, 12);
    }
    return 0;
}
static int fold_find_from(const fold_t *f, const char nm[12], uint32_t k)
{
    for (; k < f->n; k++) if (f->rec[k][0] && !memcmp(f->rec[k], nm, 12)) return (int)k;
    return -1;
}
static int fold_find(const fold_t *f, const char nm[12]) { return fold_find_from(f, nm, 0); }

/* writes E->rec (len) as a new file of /aether/... folder abs (made on demand) under its record name: ".IMP" first
 * (verify on), then renamed; the folder's lists grow by it */
static int put_new(fold_t *f, const char *abs, const char nm[12], uint32_t len)
{
    char fname[OM_PGS_FNAME], p[PATHMAX], q[PATHMAX];
    int rc;
    if ((rc = mkdirs(abs)) != 0) return rc;
    if ((rc = om_pgs_file_name(fname, nm, (const char (*)[OM_PGS_FNAME])f->file, f->n)) != 0) return rc;
    jp(p, abs, fname);
    cat(cat(q, p), ".IMP");
    pg_file(fname, len);
    if ((rc = om_sdw_write_file(q, E->rec, len, 1, E->scr, E->gap, 0)) != 0) return rc;
    beat(len);
    om_yield();
    if ((rc = s_rename(q, p)) != 0) return sd_rc(rc) ? sd_rc(rc) : -1;
    if (f->n < DMAX) { strcpy(f->file[f->n], fname); memcpy(f->rec[f->n], nm, 12); f->n++; }
    return 0;
}

/* a file of any size copied in pieces of BIGPIECE (rec + chk, which merge never needs at once here):
 * each piece read with the source alone open, then appended with the temporary file alone open (sdwrite.h
 * om_sdw_write_at; one file open at a time), the whole compared with its source, then renamed into the folder */
#define BIGPIECE (32u * OM_SDW_CHUNK)                 /* 65,408 B: every chunk a multiple of 4 but the file's last */
static int read_span(const char *p, uint32_t off, uint32_t n, uint8_t *buf)
{
    int fd, r = 0;
    uint32_t got;
    om_yield();
    if ((fd = s_open(al(p), FA_READ | FA_OPEN_EXISTING)) < 0) return sd_rc(fd);
    if (off && (r = s_seek(fd, (int)off, C_SEEK_SET)) < 0) { s_close(fd); return sd_rc(r); }
    for (got = 0; got < n; got += (uint32_t)r) {
        uint32_t k = n - got > 2048u ? 2048u : n - got;
        om_yield();
        if ((r = s_read(fd, buf + got, (int)k)) <= 0) break;
    }
    s_close(fd);
    if (r < 0) return sd_rc(r);
    return got == n ? 0 : OM_E_SHORT;
}

/* a whole class-1 save into E->rec, its size known from the listing: one open (om_sdw_read_file stats it first) */
static int read_rec(const char *p, uint32_t size, uint32_t *len)
{
    if (size + 4u > OM_PGS_REC) return OM_E_FULL;
    *len = size;
    return read_span(p, 0, size, E->rec);
}

static int put_big(fold_t *f, const char *abs, const char nm[12], const char *srcp, uint32_t len)
{
    char fname[OM_PGS_FNAME], p[PATHMAX], q[PATHMAX];
    /* rec and chk are adjacent (om_pgs_arena_split: OM_PGS_REC is a multiple of 4): one 65,408 B piece; else half */
    uint32_t off, k, piece = E->chk == E->rec + OM_PGS_REC ? BIGPIECE : BIGPIECE / 2u, crc = 0, c2;
    int rc;
    if ((rc = mkdirs(abs)) != 0) return rc;
    if ((rc = om_pgs_file_name(fname, nm, (const char (*)[OM_PGS_FNAME])f->file, f->n)) != 0) return rc;
    jp(p, abs, fname);
    cat(cat(q, p), ".IMP");
    pg_file(srcp, 2u * len);                          /* copied, then read back */
    for (off = 0; off < len || !off; off += k) {
        k = len - off > piece ? piece : len - off;
        if ((rc = read_span(srcp, off, k, E->rec)) != 0) return rc;
        crc = om_crc32(crc, E->rec, k);
        if ((rc = om_sdw_write_at(q, off, E->rec, k, E->scr, E->gap, 0)) != 0) return rc;
        if (!k) break;
        beat(k);
    }
    /* the read-back (import speed, 2026-11-24): the copy against the CRC-32 of the source bytes it was written from --
     * the check sdwrite.c's verify makes (sdwrite.h) -- instead of reading the source a second time */
    for (off = 0, c2 = 0; off < len; off += k) {
        k = len - off > CMPPIECE ? CMPPIECE : len - off;
        if ((rc = read_span(q, off, k, E->rec)) != 0) return rc;
        c2 = om_crc32(c2, E->rec, k);
        beat(k);
    }
    if (c2 != crc) return OM_E_CRC;
    om_yield();
    if ((rc = s_rename(q, p)) != 0) return sd_rc(rc) ? sd_rc(rc) : -1;
    if (f->n < DMAX) { strcpy(f->file[f->n], fname); memcpy(f->rec[f->n], nm, 12); f->n++; }
    return 0;
}

static void progress(const char *what)
{
    char t[48];
    uint32_t n = (uint32_t)RI->kai + RI->rings;
    if (!E->say) return;
    if (what) { E->say(what); return; }
    cat_u(cat(t, "SAVES: COPYING KAI/RINGS SAVES "), n, 1);
    E->say(t);
}

/* SAVES/<dest>[/0n]: every source's records; per record the newest intact copy (§5.16 om_bup_prefer) is copied in
 * unless the folder's current set (savegen.h) already holds that record (byte-identical: present; different: a
 * conflict, the Aether save is never written over); a losing copy that differs from the winner is copied to SAVES/_QUARANTINE/IMPORT/<dest>[/0n] */
static int merge_class(uint8_t cls, const char *dst, const char *qd, const srcf *src, uint32_t ns);
static int merge_slot(const char *dest, uint32_t slot, const srcf *src, uint32_t nsrc, uint16_t *subs, int alt)
{
    char dst[PATHMAX], qd[PATHMAX], p[PATHMAX];
    srcl s;
    uint32_t i;
    om_bup_dir d;
    int rc;
    memset(&s, 0, sizeof s);
    if (subs) *subs = 0;
    for (i = 0; i < nsrc; i++) {
        uint16_t before = s.subs;
        s.tag = (uint8_t)(1 + i);
        s.subs = 0;
        if ((rc = list(src[i].path, add_src, &s)) != 0) return rc;
        if (subs) *subs |= s.subs;
        s.subs = before;
    }
    if (s.over) skip_line("too-many-files", src[0].path, s.over);
    if (!s.n) return 0;
    for (i = 0; i < s.n; i++) {                       /* decode; too big, reserved and undecodable drop out */
        om_pgs_ent *e = &E->ents[i];
        e->win = 0;
        jp(p, src[e->isdir - 1].path, e->name);
        if ((rc = read_head(p, 0, OM_BUP_HDR)) != 0) { if (rc == OM_E_SHORT) { skip_file(p, "not-a-save"); continue; } return rc; }
        if (om_bup_decode_hdr((const uint8_t *)s_io, s_esize[i], &d)) { skip_file(p, "not-a-save"); continue; }
        if (om_pgs_reserved(d.name)) { skip_file(p, "reserved"); continue; }
        memcpy(e->hdr, s_io, OM_BUP_HDR);
        /* a save over 32 KB (a cartridge or floppy save) cannot go into the
         * internal memory: class 2, copied cart-only into the folder's CART sub-folder, which no load reads */
        e->win = (uint8_t)(s_esize[i] + 4u > OM_PGS_REC ? 2 : 1);
    }
    {
        char *o = cat(cat(dst, "/aether/"), dest), *o2 = cat(cat(qd, "/aether/" OM_PGS_QUAR "/IMPORT/"), dest + 6);
        if (slot) {                                   /* dest is "SAVES/<folder>"; slot n is sub-folder 0n */
            o = cat(o, "/0"); *o++ = (char)('0' + slot); *o = 0;
            o2 = cat(o2, "/0"); *o2++ = (char)('0' + slot); *o2 = 0;
        }
    }
    {   /* into the folder's current set (savegen.h), so the launcher loads what was imported; new names
         * only -- nothing on the card is written over */
        char cur[PATHMAX];
        uint32_t g;
        if ((rc = om_gen_cur(dst, cur, &g, &s_gs)) != 0) return rc;
        if (alt == 1) cat(cur + strlen(cur), "/ALT");   /* kept as alternates (never loaded) */
        s_alt_mode = (uint8_t)alt;                  /* 2 (fix I8): a conflict goes to this set's ALT/, never dropped */
        strcpy(s_conf_alt, cur);
        cat(s_conf_alt + strlen(s_conf_alt), "/ALT");
        s_alt.scanned = 0;
        rc = merge_class(1, cur, qd, src, s.n);
        s_alt_mode = 0;
        if (rc) return rc;
    }
    cat(dst + strlen(dst), "/" OM_PGS_CART);
    cat(qd + strlen(qd), "/" OM_PGS_CART);
    if ((rc = merge_class(2, dst, qd, src, s.n)) == 0)
        pg_saves(s.n);                                /* every listed save of these sources handled */
    return rc;
}

/* one class of merge_slot's decoded entries (1: for the internal memory, 2: cart-only) into dst, losers into qd */
static int merge_class(uint8_t cls, const char *dst, const char *qd, const srcf *src, uint32_t ns)
{
    char p[PATHMAX], q[PATHMAX];
    uint32_t i, k, len;
    int rc;
    struct { uint32_t n; } s = { ns };
    s_dst.scanned = s_q.scanned = 0;
    for (i = 0; i < s.n; i++) {                       /* each record once: the group of i, if i leads it */
        om_pgs_ent *w;
        char nm[12];
        uint32_t best = i, wlen;
        if (E->ents[i].win != cls) continue;
        for (k = 0; k < i; k++)
            if (E->ents[k].win == cls && !memcmp(E->ents[k].hdr + 0x10, E->ents[i].hdr + 0x10, 11)) break;
        if (k < i) continue;                          /* not the group's first member */
        for (k = i + 1; k < s.n; k++)
            if (E->ents[k].win == cls && !memcmp(E->ents[k].hdr + 0x10, E->ents[i].hdr + 0x10, 11) &&
                om_bup_prefer(E->ents[k].hdr, E->ents[k].name, E->ents[best].hdr, E->ents[best].name))
                best = k;
        w = &E->ents[best];
        memcpy(nm, w->hdr + 0x10, 11);
        nm[11] = 0;
        if (!om_bup_name_norm(nm, nm)) { jp(p, src[w->isdir - 1].path, w->name); skip_file(p, "bad-name"); continue; }
        wlen = OM_BUP_HDR + get32(w->hdr + 0x2C);
        jp(p, src[w->isdir - 1].path, w->name);
        /* the losers: quarantined when they differ from the winner, never dropped */
        for (k = i; k < s.n; k++) {
            om_pgs_ent *l = &E->ents[k];
            uint32_t llen;
            int same, qi;
            if (k == best || l->win != cls || memcmp(l->hdr + 0x10, w->hdr + 0x10, 11)) continue;
            llen = OM_BUP_HDR + get32(l->hdr + 0x2C);
            jp(q, src[l->isdir - 1].path, l->name);
            if ((same = same_rec(p, w->hdr, q, l->hdr)) < 0) return same;
            if (same) continue;
            if (!s_q.scanned && (rc = fold_scan(&s_q, qd)) != 0) return rc;
            if (s_q.over) { skip_file(q, "quarantine-full"); continue; }    /* the loser stays where it is (its source) */
            for (qi = fold_find(&s_q, nm), same = 0; qi >= 0 && !same; qi = fold_find_from(&s_q, nm, (uint32_t)qi + 1)) {
                char r2[PATHMAX];                     /* already there, the same version: an earlier run */
                uint8_t h2[OM_BUP_HDR];
                jp(r2, qd, s_q.file[qi]);
                if ((rc = read_head(r2, 0, OM_BUP_HDR)) != 0) return rc;
                memcpy(h2, s_io, OM_BUP_HDR);
                if ((same = same_rec(q, l->hdr, r2, h2)) < 0) return same;
            }
            if (same) continue;
            if (stop()) return OM_PGS_E_CANCEL;
            if (cls == 2) rc = put_big(&s_q, qd, nm, q, llen);
            else if ((rc = read_rec(q, s_esize[k], &len)) == 0) rc = put_new(&s_q, qd, nm, len);
            if (rc) return rc;
            RI->older++;
        }
        /* the winner */
        if (!s_dst.scanned && (rc = fold_scan(&s_dst, dst)) != 0) return rc;
        if (s_dst.over) { skip_file(p, "destination-full"); continue; }   /* over 256 saves: not touched */
        {
            int same = 0, any = 0, qi;
            for (qi = fold_find(&s_dst, nm); qi >= 0 && !same; qi = fold_find_from(&s_dst, nm, (uint32_t)qi + 1)) {
                char r2[PATHMAX];
                uint8_t h2[OM_BUP_HDR];
                any = 1;
                jp(r2, dst, s_dst.file[qi]);
                if ((rc = read_head(r2, 0, OM_BUP_HDR)) != 0) return rc;
                memcpy(h2, s_io, OM_BUP_HDR);
                if ((same = same_rec(p, w->hdr, r2, h2)) < 0) return same;
            }
            if (same) { RI->present++; gimp_note(p); continue; }
            if (any && s_alt_mode == 2) {           /* fix I8: a high slot's conflicting save -> ALT/, never dropped */
                int qi2, in_alt = 0;
                if (!s_alt.scanned && (rc = fold_scan(&s_alt, s_conf_alt)) != 0) return rc;
                for (qi2 = fold_find(&s_alt, nm); qi2 >= 0 && !in_alt; qi2 = fold_find_from(&s_alt, nm, (uint32_t)qi2 + 1)) {
                    char r2[PATHMAX];
                    uint8_t h2[OM_BUP_HDR];
                    jp(r2, s_conf_alt, s_alt.file[qi2]);
                    if ((rc = read_head(r2, 0, OM_BUP_HDR)) != 0) return rc;
                    memcpy(h2, s_io, OM_BUP_HDR);
                    if ((in_alt = same_rec(p, w->hdr, r2, h2)) < 0) return in_alt;
                }
                if (in_alt) { RI->present++; continue; }
                if (stop()) return OM_PGS_E_CANCEL;
                if ((rc = read_rec(p, s_esize[best], &len)) != 0) return rc;
                if ((rc = put_new(&s_alt, s_conf_alt, nm, len)) != 0) return rc;
                RI->alternates++;
                continue;
            }
            if (any && s_alt_mode != 1) { RI->conflicts++; continue; }   /* never over an Aether save */
        }
        if (stop()) return OM_PGS_E_CANCEL;
        if (cls == 2) {                               /* cart-only: copied in pieces, whatever its size */
            if ((rc = put_big(&s_dst, dst, nm, p, wlen)) != 0) return rc;
            RI->cart++;
            continue;
        }
        if ((rc = read_rec(p, s_esize[best], &len)) != 0) return rc;
        if ((rc = put_new(&s_dst, dst, nm, len)) != 0) return rc;
        gimp_note(p);
        if (s_alt_mode == 1) RI->alternates++;
        else if (src[w->isdir - 1].kai) RI->kai++; else RI->rings++;
        if (!(((uint32_t)RI->kai + RI->rings) & 7u)) progress(0);
    }
    return 0;
}

/* name lists in the arena's backup-library buffers (unused by an import: it makes no BUP call) */
#define NKA 384u
#define NRB 192u
#define NRC 192u
#define NVV 256u
#ifdef __sh__
_Static_assert((NKA + NRB + NRC + NVV) * NAMEW <= OM_BRAM_LIB_BYTES + OM_BRAM_WORK_BYTES, "the import's name lists");
#endif
static names s_ka, s_rb, s_rc, s_vv;

static void product_of(const char *f, char out[OM_GID_MAX + 1])
{
    char *b;
    if (om_pgs_imp_folder(f, out)) { out[0] = 0; return; }
    if ((b = strchr(out, '[')) != 0) *b = 0;
}

/* user decision 2026-11-23: a Rings [SLOT10]..[SLOT19] folder of a game (Aether has 10 slots) goes
 * into the game's slot that already holds one of its saves byte for byte (a run again, a resume after a cut); else
 * into the lowest empty slot, 1..9, then 0; else, every slot full, into slot 0's current set's ALT/ as alternates
 * (never loaded, never lost). Inside the slot the usual merge rules apply.
 * Hardware 2026-11-25 (Policenauts, Panzer Dragoon Saga: their only saves in Rings' [SLOT19]): a game with no saves
 * in any of its 10 slots gets the high slot in slot 0, the slot a launch uses by default (the Slot row's 0) -- in
 * slot 1 no launch the user had not set up found it, and the game offered only "New game". */
typedef struct { char n[48][OM_PGS_FNAME]; uint32_t c; } bl_t;
static int bl_ent(void *ctx, const char *nm, int len, int isdir, uint32_t size)
{
    bl_t *b = (bl_t *)ctx;
    (void)size;
    if (isdir || !is_ext(nm, ".bup")) return 0;
    if ((uint32_t)len < OM_PGS_FNAME && b->c < 48) memcpy(b->n[b->c], nm, (uint32_t)len + 1);
    else b->n[b->c < 48 ? b->c : 47][0] = 0;
    if (b->c < 48) b->c++;
    return 0;
}
/* which of a high Rings slot's (src) and an Aether slot's (dir) saves match,
 * by record version (same_rec): OURS when every save of src is in dir (a run again), or dir holds saves and every one
 * of them is one of src's (a resume after a cut placed part of it). One unchanged record shared with a slot in use (an
 * options save) is neither: that slot is not this high slot's. */
#define HS_SRC_IN_DIR 1
#define HS_DIR_IN_SRC 2
static int match_set(const bl_t *x, const char *xd, const bl_t *y, const char *yd)
{   /* 1 when every file of x has a same-version copy in y */
    uint32_t i, j;
    uint8_t ha[OM_BUP_HDR];
    int rc;
    for (i = 0; i < x->c; i++) {
        char pa[PATHMAX];
        int hit = 0;
        if (!x->n[i][0]) return 0;
        jp(pa, xd, x->n[i]);
        if (read_head(pa, 0, OM_BUP_HDR)) return 0;
        memcpy(ha, s_io, OM_BUP_HDR);
        for (j = 0; j < y->c && !hit; j++) {
            char pb[PATHMAX];
            if (!y->n[j][0]) continue;
            jp(pb, yd, y->n[j]);
            if (read_head(pb, 0, OM_BUP_HDR)) continue;
            if (memcmp(ha + 0x10, (uint8_t *)s_io + 0x10, 11)) continue;
            if ((rc = same_rec(pa, ha, pb, (const uint8_t *)s_io)) < 0) return rc;
            hit = rc;
        }
        if (!hit) return 0;
    }
    return 1;
}
static int hs_match(const char *dir, const char *src, uint32_t *ndir)
{
    static bl_t a, b;
    int rc, m = 0;
    a.c = b.c = 0;
    if ((rc = list(src, bl_ent, &a)) != 0) return rc;
    if ((rc = list(dir, bl_ent, &b)) != 0) return rc;
    *ndir = b.c;
    if (a.c && (rc = match_set(&a, src, &b, dir)) < 0) return rc;
    if (a.c && rc) m |= HS_SRC_IN_DIR;
    if (b.c && (rc = match_set(&b, dir, &a, src)) < 0) return rc;
    if (b.c && rc) m |= HS_DIR_IN_SRC;
    return m;
}
static int place_high(const char *dest, const srcf *h)
{
    static const uint8_t order[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 0 };
    char sd[PATHMAX], cur[PATHMAX];
    uint32_t k, g, nd;
    int rc, empty = -1, used = 0;
    for (k = 0; k < 10; k++) {                       /* a slot that is this high slot's already, else the first empty */
        cat(cat(sd, "/aether/"), dest);
        if (order[k]) { char *o = cat(sd + strlen(sd), "/0"); *o++ = (char)('0' + order[k]); *o = 0; }
        if ((rc = om_gen_cur(sd, cur, &g, &s_gs)) != 0) return rc;
        if ((rc = hs_match(cur, h->path, &nd)) < 0) return rc;
        if (rc) return merge_slot(dest, order[k], h, 1, 0, 2);
        if (!nd && empty < 0) empty = order[k];
        if (nd) used = 1;
    }
    if (!used) empty = 0;                            /* no saves in any slot: slot 0, the launch's default */
    cat(cat(sd, "/aether/"), dest);
    if ((rc = om_gen_cur(sd, cur, &g, &s_gs)) != 0) return rc;
    cat(cur + strlen(cur), "/ALT");
    if ((rc = hs_match(cur, h->path, &nd)) < 0) return rc;
    if (!(rc & HS_SRC_IN_DIR) && empty >= 0) return merge_slot(dest, (uint32_t)empty, h, 1, 0, 2);
    return merge_slot(dest, 0, h, 1, 0, 1);         /* every slot full (or placed there before): alternates */
}

/* Rings' slot folder entries are "<n><raw id>" (n = '1'..'9') */
static int import_product1(const char *P)
{   /* a folder Aether already uses for this product is sticky. A Kai region folder D goes to
     * SAVES/D when Aether has D; else, when Aether reads its product-only SAVES/<P> (it has P and not D), into P --
     * never creating D, which would change the launcher's pick (pgsplan.h want_po); else to a new SAVES/D. Product-only
     * sources (Rings' id folder, Kai's <P>) go into every folder a region of the game reads. */
    char f[OM_GID_MAX + 1], d[OM_PGS_DIR];
    typedef struct { const char *name; int kr[8]; uint32_t nkr; } dst_t;
    dst_t dl[64];
    srcf src[SRC_MAX];
    uint32_t i, k, nd = 0, ns, slot;
    uint32_t rcmask = 0;                              /* Rings slots 1..19 of this product (24c: 10..19 too) */
    int rc, kai_po = -1, rings_po = -1, vv_po = 0;
    for (i = 0; i < s_vv.n; i++) if (!strcmp(s_vv.v[i], P)) vv_po = 1;    /* Aether's own product-only folder */
    for (i = 0; i < s_vv.n; i++) {                   /* Aether's own region folders: each one a destination */
        product_of(s_vv.v[i], d);
        if (strcmp(d, P) || !strchr(s_vv.v[i], '[') || om_pgs_imp_folder(s_vv.v[i], f) || strcmp(f, s_vv.v[i]))
            continue;
        if (nd < 64) { dl[nd].name = s_vv.v[i]; dl[nd].nkr = 0; nd++; }
    }
    if (vv_po && nd < 64) { dl[nd].name = P; dl[nd].nkr = 0; nd++; }
    for (i = 0; i < s_ka.n; i++) {                   /* Kai: the region folders and the product-only one */
        const char *to;
        if (om_pgs_imp_folder(s_ka.v[i], f)) continue;
        if (!strcmp(f, P)) { kai_po = (int)i; continue; }
        product_of(f, d);
        if (strcmp(d, P)) continue;
        for (k = 0; k < nd && strcmp(dl[k].name, s_ka.v[i]); k++) ;
        to = k < nd ? s_ka.v[i] : vv_po ? P : s_ka.v[i];                 /* Aether's D; its P; or a new D */
        for (k = 0; k < nd && strcmp(dl[k].name, to); k++) ;
        if (k == nd) { if (nd == 64) continue; dl[nd].name = to; dl[nd].nkr = 0; nd++; }
        if (dl[k].nkr < 8) dl[k].kr[dl[k].nkr++] = (int)i;
    }
    for (i = 0; i < s_rb.n; i++) { product_of(s_rb.v[i], d); if (!strcmp(d, P) && !strchr(s_rb.v[i], '[')) rings_po = (int)i; }
    for (i = 0; i < s_rc.n; i++) { product_of(s_rc.v[i] + 1, d); if (!strcmp(d, P)) rcmask |= 1u << (s_rc.v[i][0] - '0'); }
    if (!nd && kai_po < 0 && rings_po < 0 && !rcmask) return 0;
    if (!nd) { dl[0].name = P; dl[0].nkr = 0; nd = 1; }                 /* nothing yet: the product folder */
    for (k = 0; k < nd; k++) {                       /* every folder Aether reads for this product */
        char dest[OM_PGS_DIR];
        uint16_t subs = 0;
        uint32_t j;
        if (!dl[k].nkr && kai_po < 0 && rings_po < 0 && !rcmask) continue;   /* Aether's alone */
        cat(cat(dest, "SAVES/"), dl[k].name);
        if (E->say) { char t[48]; cat(cat(t, "IMPORT: "), dest + 6); E->say(t); }
        strncpy(s_pg.game, dest + 6, sizeof s_pg.game - 1);
        s_pg.game[sizeof s_pg.game - 1] = 0;
        s_pg.file[0] = 0;
        s_pg.file_done = s_pg.file_total = 0;
        emit();
        for (slot = 0; slot <= 9; slot++) {
            char t2[4] = { '0', (char)('0' + slot), 0, 0 };
            ns = 0;
            if (slot && !((subs | rcmask) & (1u << slot))) continue;
            for (j = 0; j < dl[k].nkr && ns < SRC_MAX; j++) {
                jp(src[ns].path, OM_PGS_KAI_ROOT, s_ka.v[dl[k].kr[j]]);
                if (slot) jp(src[ns].path, src[ns].path, t2);
                src[ns++].kai = 1;
            }
            if (kai_po >= 0 && ns < SRC_MAX) {
                jp(src[ns].path, OM_PGS_KAI_ROOT, s_ka.v[kai_po]);
                if (slot) jp(src[ns].path, src[ns].path, t2);
                src[ns++].kai = 1;
            }
            if (!slot && rings_po >= 0 && ns < SRC_MAX) { jp(src[ns].path, OM_PGS_RINGS_ROOT, s_rb.v[rings_po]); src[ns++].kai = 0; }
            if (slot && (rcmask & (1u << slot)))
                for (i = 0; i < s_rc.n && ns < SRC_MAX; i++) {
                    product_of(s_rc.v[i] + 1, d);
                    if (s_rc.v[i][0] - '0' != (int)slot || strcmp(d, P)) continue;
                    {   char t[PATHMAX], sl[12] = "[SLOT00]";
                        sl[6] = (char)('0' + slot);
                        jp(t, OM_PGS_RINGS_ROOT, sl);
                        jp(src[ns].path, t, s_rc.v[i] + 1);
                        src[ns++].kai = 0;
                    }
                }
            if (!ns) continue;
            if ((rc = merge_slot(dest, slot, src, ns, slot ? 0 : &subs, 0)) != 0) return rc;
        }
        for (j = 0; j < 10 * s_rc.n; j++) {          /* Rings [SLOT10]..[SLOT19], in slot order */
            int hs;
            srcf h;
            char t[PATHMAX], sl[12] = "[SLOT00]";
            i = j % s_rc.n;
            hs = s_rc.v[i][0] - '0';
            if (hs != 10 + (int)(j / s_rc.n)) continue;
            product_of(s_rc.v[i] + 1, d);
            if (strcmp(d, P)) continue;
            sl[5] = (char)('0' + hs / 10); sl[6] = (char)('0' + hs % 10);
            jp(t, OM_PGS_RINGS_ROOT, sl);
            jp(h.path, t, s_rc.v[i] + 1);
            h.kai = 0;
            if ((rc = place_high(dest, &h)) != 0) return rc;
        }
    }
    return 0;
}

/* a game the picker left unticked is not copied */
static int import_product(const char *P)
{
    int rc;
    if (!picked(P)) return 0;
    if (stop()) return OM_PGS_E_CANCEL;
    rc = import_product1(P);
    if (s_pg.games_done < s_pg.games_total) s_pg.games_done++;
    emit();
    return rc;
}

static int first_of(const char *P, uint32_t list_i, uint32_t idx)
{   /* 1 when no earlier source entry (KA, then RB, then RC) names product P */
    char d[OM_GID_MAX + 1];
    uint32_t i;
    for (i = 0; i < s_ka.n && (list_i > 0 || i < idx); i++) { product_of(s_ka.v[i], d); if (!strcmp(d, P)) return 0; }
    if (list_i == 0) return 1;
    for (i = 0; i < s_rb.n && (list_i > 1 || i < idx); i++) { product_of(s_rb.v[i], d); if (!strcmp(d, P)) return 0; }
    if (list_i == 1) return 1;
    for (i = 0; i < s_rc.n && i < idx; i++) { product_of(s_rc.v[i] + 1, d); if (!strcmp(d, P)) return 0; }
    return 1;
}

/* GENLIST.TXT (pgsrun.h OM_PGS_GENLIST): its "R"/"A" lines kept, the "I" line = the old one plus s_gimp (a state file,
 * replaced with om_sdw_replace; a cut before it: the next import notes the same versions as present) */
static int genlist_add(void)
{
    static const char hx[] = "0123456789ABCDEF";
    uint32_t len = 0, n = 0, i, k, v, ids = 0;
    uint8_t *o = E->chk;
    int rc = om_sdw_read_file(OM_PGS_GENLIST, E->rec, OM_PGS_REC, &len);
    if (rc && rc != OM_SDW_NOFILE) return rc;
    if (rc) len = 0;
    o[n++] = '\n';
    for (i = 0; i < len; ) {                          /* every line but "I", kept */
        uint32_t a = i;
        while (i < len && E->rec[i] != '\n') i++;
        if (i > a && E->rec[a] != 'I' && n + (i - a) + 1 < OM_PGS_REC / 2) { memcpy(o + n, E->rec + a, i - a); n += i - a; o[n++] = '\n'; }
        if (i > a && E->rec[a] == 'I')                /* its ids join this run's */
            for (k = a + 1; k + 9 <= i && E->rec[k] == ' ' && s_ngimp < GIMP_MAX; k += 9) {
                uint32_t t, j;
                for (v = 0, j = 0; j < 8; j++) {
                    char c = (char)E->rec[k + 1 + j];
                    v = v << 4 | (uint32_t)(c >= 'A' ? c - 'A' + 10 : c - '0');
                }
                for (t = 0; t < s_ngimp && s_gimp[t] != v; t++) ;
                if (t == s_ngimp) s_gimp[s_ngimp++] = v;
            }
        i++;
    }
    o[n++] = 'I';
    for (i = 0; i < s_ngimp; i++, ids++) {
        o[n++] = ' ';
        for (k = 0; k < 8; k++) o[n++] = (uint8_t)hx[(s_gimp[i] >> (28 - 4 * k)) & 15u];
    }
    o[n++] = '\n';
    if ((rc = sd_mkdir("/aether/SAVES")) != 0) return rc;
    return om_sdw_replace(OM_PGS_GENLIST, o, n, E->scr, E->gap, 0);
}

static int import_all(void)
{
    char (*pool)[NAMEW] = (char (*)[NAMEW])E->lib;
    char d[OM_GID_MAX + 1], f[OM_GID_MAX + 1];
    uint32_t i, k, slot;
    int rc;
    s_ka.v = pool;                 s_ka.cap = NKA; s_ka.n = s_ka.over = 0;
    s_rb.v = pool + NKA;           s_rb.cap = NRB; s_rb.n = s_rb.over = 0;
    s_rc.v = pool + NKA + NRB;     s_rc.cap = NRC; s_rc.n = s_rc.over = 0;
    s_vv.v = pool + NKA + NRB + NRC; s_vv.cap = NVV; s_vv.n = s_vv.over = 0;
    progress("SAVES: READING KAI/RINGS FOLDERS");
    if ((rc = list(OM_PGS_KAI_ROOT, add_dir, &s_ka)) != 0) return rc;
    for (i = k = 0; i < s_ka.n; i++)                  /* Kai writes its folders sanitized: anything else is not one */
        if (!om_pgs_imp_folder(s_ka.v[i], f) && !strcmp(f, s_ka.v[i])) { if (k != i) memmove(s_ka.v[k], s_ka.v[i], NAMEW); k++; }
        else {                                       /* 24c: each of its saves counted and named */
            char p[PATHMAX];
            jp(p, OM_PGS_KAI_ROOT, s_ka.v[i]);
            if ((rc = skip_tree(p, s_ka.v[i][0] == '_' ? "general-set-backup" : "not-a-game-folder", 1)) != 0) return rc;
        }
    s_ka.n = k;
    if ((rc = list(OM_PGS_RINGS_ROOT, add_dir, &s_rb)) != 0) return rc;
    for (i = k = 0; i < s_rb.n; i++) {
        if ((slot = om_pgs_rings_slot(s_rb.v[i])) != 0) {
            if (slot <= 19) {                        /* its game folders, listed now (one listing at a time); 24c:
                                                      * 10..19 too (place_high) */
                names t = { pool + NKA + NRB + NRC, 0, NVV, 0 };        /* VV's room, borrowed before VV is listed */
                char p[PATHMAX];
                uint32_t j;
                jp(p, OM_PGS_RINGS_ROOT, s_rb.v[i]);
                if ((rc = list(p, add_dir, &t)) != 0) return rc;
                if (t.over) skip_line("too-many-folders", p, t.over);
                for (j = 0; j < t.n; j++) {
                    if (om_pgs_imp_folder(t.v[j], f) || strchr(f, '[') || s_rc.n >= s_rc.cap || strlen(t.v[j]) > NAMEW - 2) {
                        char q[PATHMAX];
                        jp(q, p, t.v[j]);
                        if ((rc = skip_tree(q, "not-a-game-folder", 1)) != 0) return rc;
                        continue;
                    }
                    s_rc.v[s_rc.n][0] = (char)('0' + slot);
                    strcpy(s_rc.v[s_rc.n] + 1, t.v[j]);
                    s_rc.n++;
                }
            } else {
                char p[PATHMAX];
                jp(p, OM_PGS_RINGS_ROOT, s_rb.v[i]);
                if ((rc = skip_tree(p, "no-such-slot", 1)) != 0) return rc;
            }
            continue;
        }
        if (om_pgs_imp_folder(s_rb.v[i], f) || strchr(f, '[')) {
            char p[PATHMAX];
            jp(p, OM_PGS_RINGS_ROOT, s_rb.v[i]);
            if ((rc = skip_tree(p, s_rb.v[i][0] == '_' ? "general-set-backup" : "not-a-game-folder", 1)) != 0) return rc;
            continue;
        }
        if (k != i) memmove(s_rb.v[k], s_rb.v[i], NAMEW);
        k++;
    }
    s_rb.n = k;
    if ((rc = list(OM_PGS_SAVES, add_dir, &s_vv)) != 0) return rc;
    if (s_ka.over) skip_line("too-many-folders", OM_PGS_KAI_ROOT, s_ka.over);
    if (s_rb.over + s_rc.over) skip_line("too-many-folders", OM_PGS_RINGS_ROOT, s_rb.over + s_rc.over);
    {   /* the progress total: the picked games, from the listings above (no further SD call) */
        uint32_t g = 0;
        for (i = 0; i < s_ka.n; i++) { product_of(s_ka.v[i], d); if (d[0] && first_of(d, 0, i) && picked(d)) g++; }
        for (i = 0; i < s_rb.n; i++) { product_of(s_rb.v[i], d); if (d[0] && first_of(d, 1, i) && picked(d)) g++; }
        for (i = 0; i < s_rc.n; i++) { product_of(s_rc.v[i] + 1, d); if (d[0] && first_of(d, 2, i) && picked(d)) g++; }
        s_pg.games_total = g + (s_selt && picked(OM_ISC_GENERAL));
        emit();
    }
    for (i = 0; i < s_ka.n; i++) { product_of(s_ka.v[i], d); if (d[0] && first_of(d, 0, i) && (rc = import_product(d))) return rc; }
    for (i = 0; i < s_rb.n; i++) { product_of(s_rb.v[i], d); if (d[0] && first_of(d, 1, i) && (rc = import_product(d))) return rc; }
    for (i = 0; i < s_rc.n; i++) { product_of(s_rc.v[i] + 1, d); if (d[0] && first_of(d, 2, i) && (rc = import_product(d))) return rc; }
    {   /* Kai's SAVES/LATEST -- saves in no BRAM/<game> folder -- copy-only into _GENERAL (no game
         * id; R1-safe). SAVES/2025 and the other SAVES/ folders (raw BRAM dumps) are never read. */
        srcf l;
        strcpy(l.path, OM_PGS_KAI_LATEST);
        l.kai = 1;
        progress("SAVES: KAI'S LATEST SAVES");
        s_ngimp = 0;
        s_gen_imp = 1;
        if (picked(OM_ISC_GENERAL) && stop()) return OM_PGS_E_CANCEL;
        if (s_selt && picked(OM_ISC_GENERAL)) { strcpy(s_pg.game, OM_ISC_GENERAL); emit(); }
        rc = picked(OM_ISC_GENERAL) ? merge_slot(OM_PGS_GENERAL, 0, &l, 1, 0, 0) : 0;
        if (s_selt && picked(OM_ISC_GENERAL) && s_pg.games_done < s_pg.games_total) { s_pg.games_done++; emit(); }
        s_gen_imp = 0;
        if (rc) return rc;
        if (s_ngimp && (rc = genlist_add()) != 0) return rc;
    }
    return 0;
}

static int summary(om_pgs_imp *r);
int om_pgs_import(const om_pgs_env *e, om_pgs_imp *r, char notice[OM_PGS_NOTICE])
{
    int rc;
    E = e;
    RI = r;
    memset(r, 0, sizeof *r);
    s_made[0] = 0;
    memset(&s_pg, 0, sizeof s_pg);
    s_nskip = s_skipmore = 0;
    s_skip[0] = 0;
    notice[0] = 0;
    r->ran = 1;
    rc = import_all();
    if (rc == OM_PGS_E_CANCEL)                        /* the copies made are kept; no IMPORT.TXT (as the Card */
        r->canceled = 1;                             /* Manager's cancel: ImportResult.cs) */
    else if (rc == 0)
        rc = summary(r);
    r->rc = rc;
    if (rc == OM_PGS_E_CANCEL) {
        strcpy(notice, "IMPORT CANCELED: RUN IT AGAIN");
        return 0;
    }
    if (rc) {
        cat_rc(cat(notice, "IMPORT STOPPED rc="), rc);
        return 0;
    }
    return 0;
}

/* IMPORT.TXT: the counts, then every skipped save named (24c) */
static int summary(om_pgs_imp *r)
{
    char line[160], *o;
    int rc;
    {
        o = cat_u(cat(line, "kai="), r->kai, 1);
        o = cat_u(cat(o, " rings="), r->rings, 1);
        o = cat_u(cat(o, " present="), r->present, 1);
        o = cat_u(cat(o, " skipped="), r->skipped, 1);
        o = cat_u(cat(o, " conflicts="), r->conflicts, 1);
        o = cat_u(cat(o, " older="), r->older, 1);
        o = cat_u(cat(o, " cart_only="), r->cart, 1);
        o = cat_u(cat(o, " alternates="), r->alternates, 1);   /* M6 Task 24c */
        cat(o, "\n");
        {   /* line 1, then every skipped save named (24c) */
            uint32_t n = (uint32_t)strlen(line);
            memcpy(E->rec, line, n);
            memcpy(E->rec + n, s_skip, s_nskip);
            n += s_nskip;
            if (s_skipmore) {
                n = (uint32_t)(cat_u(cat((char *)E->rec + n, "skipped-more "), s_skipmore, 1) - (char *)E->rec);
                E->rec[n++] = '\n';
            }
            if ((rc = sd_mkdir("/aether/SAVES")) == 0)
                rc = om_sdw_replace(OM_PGS_IMPORT_TXT, E->rec, n, E->scr, E->gap, 0);
        }
    }
    return rc;
}

/* the import of the games the picker ticked: IMPORT.SEL checked against the hand-off's gid ("SEL<crc>") --
 * absent, damaged or stale: nothing is imported (r->rc, notice) -- then removed (a state file), so it is never used
 * twice */
static uint32_t s_selbuf[(4096 + 4) / 4];
int om_pgs_import_sel(const om_pgs_env *e, const char *gid, om_pgs_imp *r, char notice[OM_PGS_NOTICE])
{
    uint32_t len = 0;
    int rc;
    E = e;
    rc = om_sdw_read_file(OM_ISC_SEL_PATH, (uint8_t *)s_selbuf, sizeof s_selbuf, &len);
    if (!rc) rc = om_isel_check((const char *)s_selbuf, len, gid);
    if (rc) {
        memset(r, 0, sizeof *r);
        r->ran = 1;
        r->rc = rc;
        strcpy(notice, "IMPORT LIST IS STALE: NOTHING COPIED");
        return 0;
    }
    s_selt = (const char *)s_selbuf;
    s_sell = len;
    om_pgs_import(e, r, notice);
    s_selt = 0;
    {   /* used: removed with its ".NEW" (sdwrite.h) */
        char q[PATHMAX];
        cat(cat(q, OM_ISC_SEL_PATH), ".NEW");
        rc = s_unlink(al(q));
        rc = s_unlink(al(OM_ISC_SEL_PATH));
        (void)rc;
    }
    return 0;
}

const char *om_pgs_import_first_skip(void)
{   /* "skipped <why> <path>" -> <path> */
    static char t[PATHMAX];
    const char *a = s_skip, *b;
    uint32_t n;
    t[0] = 0;
    if (!s_nskip || !(a = strchr(a, ' ')) || !(a = strchr(a + 1, ' '))) return t;
    a++;
    b = strchr(a, '\n');
    n = b ? (uint32_t)(b - a) : (uint32_t)strlen(a);
    if (n >= sizeof t) n = sizeof t - 1;
    memcpy(t, a, n);
    t[n] = 0;
    return t;
}

/* =================================================================================================================
 * BACKUP
 * ================================================================================================================= */
typedef struct { uint32_t n; int idx[OM_PGS_LIST]; uint32_t body, crc; uint8_t hdr[OM_PGS_BAK_HDR]; } bk_t;
static bk_t s_bk;
static uint8_t s_rhdr[OM_BUP_HDR];

/* a record's read after a chance for the launch's eye (yield.h): the backup reads every record, twice */
static int bk_read(const char nm[12], uint8_t *buf, uint32_t cap, om_bup_dir *d) { om_yield(); return om_bram_read(nm, buf, cap, d); }

/* the readable non-marker records: s_bk.idx; the table into E->rec after the header; body length and CRC */
static int bk_collect(om_pgs_bak *r)
{
    uint32_t n, i, pad;
    static const uint8_t z[4] = { 0, 0, 0, 0 };
    int rc;
    om_bup_dir d;
    char nm[12];
    s_bk.n = s_bk.body = s_bk.crc = 0;
    om_yield();
    if ((rc = om_bram_list(E->list, OM_PGS_LIST, &n)) != 0) return rc;
    for (i = 0; i < n; i++) {
        uint8_t *t;
        if (!om_bup_name_norm(nm, E->list[i].name) || !strcmp(nm, OM_PGS_MARKER)) continue;
        rc = bk_read(nm, E->chk, OM_PGS_REC, &d);
        if (rc == OM_BUP_BROKEN || rc == OM_BUP_NOTFOUND) { r->torn++; continue; }
        if (rc) return rc;
        if (OM_PGS_BAK_HDR + (s_bk.n + 1u) * OM_PGS_BAK_ENT > OM_PGS_REC) return OM_E_TABLESIZE;
        t = E->rec + OM_PGS_BAK_HDR + s_bk.n * OM_PGS_BAK_ENT;
        memset(t, 0, OM_PGS_BAK_ENT);
        memcpy(t, nm, 12);
        memcpy(t + 12, d.comment, 11);
        t[23] = d.language;
        put32(t + 24, d.date);
        put32(t + 28, d.datasize);
        om_bup_encode_hdr(&d, s_rhdr);
        s_bk.crc = om_crc32(s_bk.crc, s_rhdr, OM_BUP_HDR);
        s_bk.crc = om_crc32(s_bk.crc, E->chk, d.datasize);
        pad = (4u - (d.datasize & 3u)) & 3u;
        s_bk.crc = om_crc32(s_bk.crc, z, pad);
        s_bk.body += OM_BUP_HDR + d.datasize + pad;
        s_bk.idx[s_bk.n++] = (int)i;
    }
    return 0;
}

static void bk_header(uint32_t seq, const char *date)
{
    uint8_t *h = E->rec;
    char nm[12], m[40];
    om_bup_dir d;
    memset(h, 0, OM_PGS_BAK_HDR);
    memcpy(h, OM_PGS_BAK_MAGIC, 8);
    h[8] = 0; h[9] = 1;
    h[10] = (uint8_t)(s_bk.n >> 8); h[11] = (uint8_t)s_bk.n;
    put32(h + 12, s_bk.body);
    put32(h + 16, s_bk.crc);
    put32(h + 20, seq);
    memcpy(h + 24, date, strlen(date) < 15 ? strlen(date) : 15);
    memset(nm, 0, 12);
    memcpy(nm, OM_PGS_MARKER, 11);
    memset(m, 0, sizeof m);
    if (bk_read(nm, (uint8_t *)m, sizeof m - 1, &d) == 0) {
        uint32_t k;
        for (k = 0; k < 23 && m[k] && m[k] != '\n'; k++) h[40 + k] = (uint8_t)m[k];
    }
}

/* the streaming source: header + table (E->rec), then each record's .BUP image (re-read into E->chk) */
typedef struct { const uint8_t *p; uint32_t left; uint32_t rec; int stage; uint32_t crc; int bad; } bsrc;
static int32_t bk_src(void *ctx, uint8_t *dst, uint32_t cap)
{
    bsrc *b = (bsrc *)ctx;
    static const uint8_t z[4] = { 0, 0, 0, 0 };
    uint32_t n;
    while (!b->left) {
        om_bup_dir d;
        char nm[12];
        int rc;
        if (b->stage == 0) {                          /* header + table done: the first record */
            b->stage = 1;
            b->rec = 0;
        } else if (b->stage == 3)                     /* a record's padding done: the next record */
            { b->stage = 1; b->rec++; }
        if (b->rec >= s_bk.n) return 0;
        if (b->stage == 1) {
            if (!om_bup_name_norm(nm, E->list[s_bk.idx[b->rec]].name)) return OM_E_FIELD;
            if ((rc = bk_read(nm, E->chk, OM_PGS_REC, &d)) != 0) return rc;
            om_bup_encode_hdr(&d, s_rhdr);
            b->crc = om_crc32(b->crc, s_rhdr, OM_BUP_HDR);
            b->crc = om_crc32(b->crc, E->chk, d.datasize);
            b->p = s_rhdr; b->left = OM_BUP_HDR;
            b->stage = 2;
            s_bk.hdr[0] = 0;
            put32(s_bk.hdr + 4, d.datasize);
        } else if (b->stage == 2) {
            uint32_t ds = get32(s_bk.hdr + 4), pad = (4u - (ds & 3u)) & 3u;
            if (s_bk.hdr[0] == 0) { b->p = E->chk; b->left = ds; s_bk.hdr[0] = 1; }
            else {
                b->crc = om_crc32(b->crc, z, pad);
                b->p = z; b->left = pad; b->stage = 3;
            }
        }
    }
    n = b->left < cap ? b->left : cap;
    memcpy(dst, b->p, n);
    b->p += n;
    b->left -= n;
    return (int32_t)n;
}

/* generations live in shard folders, _BACKUP/<sss>/<seq>-<date>.BKP (sss = seq / 100, three digits), so
 * no listing holds more than the shard names and one shard's 100 generations; the flat _BACKUP/<seq>... files written
 * before Task 24b are read too. Nothing is ever deleted here. */
typedef struct { uint32_t max, best; int shard; char name[32]; } gl_t;
static int add_gen(void *ctx, const char *nm, int len, int isdir, uint32_t size)
{
    gl_t *g = (gl_t *)ctx;
    int complete;
    uint32_t s;
    (void)size;
    if (isdir) {                                      /* a shard: three digits */
        if (len == 3 && nm[0] >= '0' && nm[0] <= '9' && nm[1] >= '0' && nm[1] <= '9' && nm[2] >= '0' && nm[2] <= '9') {
            int v = (nm[0] - '0') * 100 + (nm[1] - '0') * 10 + (nm[2] - '0');
            if (v > g->shard) g->shard = v;
        }
        return 0;
    }
    if ((uint32_t)len >= 32u || !(s = om_pgs_bak_seq(nm, &complete))) return 0;
    if (s > g->max) g->max = s;
    if (complete && s > g->best) { g->best = s; memcpy(g->name, nm, (uint32_t)len + 1); }
    return 0;
}

/* swap speed, part 2: the backup inside a save swap whose run took env.rel (pgsrun.c: the working folder is /aether)
 * names its folders from there; elsewhere (no swap) absolute. Set by om_pgs_backup from its env. */
static uint8_t s_bk_rel;
static const char *BK(const char *abs) { return s_bk_rel ? abs + (sizeof OM_CARD_ROOT - 1) : abs; }   /* past "/aether/" */
void om_pgs_backup_rel(int on) { s_bk_rel = (uint8_t)(on != 0); }
static void shard_dir(char *out, uint32_t seq)
{
    char *o = cat(cat(out, BK(OM_PGS_BAK_DIR)), "/");
    cat_u(o, seq / 100u, 3);
}

/* a generation's header into s_io: its record count, or < 0. Swap speed (hardware 2026-11-24): one open -- the
 * "unchanged" check compares the header's count, body length and body CRC only, so the table is not read (it was, in
 * 2 KB pieces, each its own open: every open walks the card from its root) */
static int gen_head(const char *p)
{
    int rc;
    if ((rc = read_head(p, 0, OM_PGS_BAK_HDR)) != 0) return rc;
    if (memcmp(s_io, OM_PGS_BAK_MAGIC, 8)) return OM_E_MAGIC;
    return (int)(((uint32_t)((uint8_t *)s_io)[10] << 8) | ((uint8_t *)s_io)[11]);
}

int om_pgs_backup(const om_pgs_env *e, const char *date, om_pgs_bak *r, char notice[OM_PGS_MSG])
{
    char p[PATHMAX], q[PATHMAX], nm[40], best[PATHMAX];
    gl_t g, sh;
    bsrc b;
    uint32_t max;
    int rc, cnt, k, x;
    E = e;
    memset(r, 0, sizeof *r);
    notice[0] = 0;
    best[0] = 0;
    if ((rc = bk_collect(r)) != 0) goto out;
    memset(&g, 0, sizeof g);
    g.shard = -1;
    if ((rc = list(BK(OM_PGS_BAK_DIR), add_gen, &g)) != 0) goto out;
    max = g.max;
    if (g.best) jp(best, BK(OM_PGS_BAK_DIR), g.name);
    /* the newest shard holds the highest seq; the newest complete generation is in it or (after a cut .TMP) just below */
    for (x = g.shard, k = 0; x >= 0 && k < 4; x--, k++) {
        memset(&sh, 0, sizeof sh);
        sh.shard = -1;
        shard_dir(p, (uint32_t)x * 100u);
        if ((rc = list(p, add_gen, &sh)) != 0) goto out;
        if (sh.max > max) max = sh.max;
        if (sh.best > g.best) { g.best = sh.best; jp(best, p, sh.name); }
        if (sh.best) break;
    }
    r->records = (uint16_t)s_bk.n;
    if (best[0] && (cnt = gen_head(best)) >= 0) {                    /* unchanged since the newest generation */
        uint8_t *h = (uint8_t *)s_io;
        if ((uint32_t)cnt == s_bk.n && get32(h + 12) == s_bk.body && get32(h + 16) == s_bk.crc) {
            r->same = 1;
            r->seq = g.best;
        }
    }
    if (!r->same) {
        if (max >= 99999u) { rc = OM_E_FULL; goto out; }
        r->seq = max + 1u;
        shard_dir(p, r->seq);
        /* swap speed: the folders are made only when a generation is written, and only those the listing did not show
         * (an absent _BACKUP lists nothing; a shard listed exists) -- every mkdir walks the card from its root */
        if (g.shard < 0 && !g.max
            && ((rc = sd_mkdir(BK("/aether/SAVES"))) != 0 || (rc = sd_mkdir(BK(OM_PGS_BAK_DIR))) != 0))
            goto out;
        if ((int)(r->seq / 100u) != g.shard && (rc = sd_mkdir(p)) != 0)
            goto out;
        if ((rc = om_pgs_bak_name(nm, sizeof nm, r->seq, date, ".TMP")) != 0) goto out;
        jp(q, p, nm);
        bk_header(r->seq, date);
        memset(&b, 0, sizeof b);
        b.p = E->rec;
        b.left = OM_PGS_BAK_HDR + s_bk.n * OM_PGS_BAK_ENT;
        if ((rc = om_sdw_write_src(q, bk_src, &b, 1, e->scr, e->gap, 0)) != 0) goto out;
        if (b.crc != s_bk.crc) { rc = OM_E_CRC; goto out; }      /* BRAM changed between the two reads */
        om_pgs_bak_name(nm, sizeof nm, r->seq, date, ".BKP");
        jp(best, p, nm);
        om_yield();
        if ((rc = s_rename(q, best)) != 0) { rc = sd_rc(rc) ? sd_rc(rc) : -1; goto out; }
        r->written = 1;
    }
    rc = 0;
out:
    r->rc = rc;
    if (rc) cat_rc(cat(notice, "SAVES: BRAM BACKUP FAILED rc="), rc);
    return 0;
}
