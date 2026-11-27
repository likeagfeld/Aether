/* menu/launcher/pgsrun.c -- see pgsrun.h (contract/SPEC.md §5.17). Runs once
 * per launch, never per frame. SD calls: one file or one listing open at a time (a folder is listed in full before any
 * file in it is opened, as menu/app/satfs.c does); names go to libsatiator from 4-aligned buffers (satiator.c
 * buffer_xfer walks them as uint32_t*; s_rename copies both names itself). BSD-3-Clause. */
#include <string.h>
#include "carddir.h"
#include "om_fmt.h"
#include "pgsrun.h"
#include "pgsbak.h"
#include "savegen.h"
#include "satiator.h"
#include "yield.h"

#define ROOT OM_CARD_ROOT
/* swap speed, part 2 (2026-11-24): the folder names are resolved from /aether, not from the card's root. With
 * env.rel the run starts with s_chdir("/aether") (the caller puts the game folder back: lseq.c save_swap), checks it
 * with a relative stat of LAUNCHER.BIN (only /aether holds it), and then names every path relative to it ("SAVES/...",
 * s_rootn 0): each command then scans /aether's few names instead of the root's ~700 first. A failed chdir or check,
 * or no env.rel: absolute names, as before (s_rootn = strlen(ROOT)). The journal and ACTIVE.TXT hold folder names relative to
 * /aether either way, so a resume finds everything whatever the working folder was. */
static uint32_t s_rootn = sizeof ROOT - 1;
/* swap speed, part 3 (option A, 2026-11-24): while ARCHIVE, VERIFY and plan_gen work on one save folder (and CHECK_FITS
 * or a LOAD reads the general set), the working folder is that save folder itself (s_cwd, a folder name below
 * /aether; "" = /aether), so each of their many commands names "G000/G00002/X.BUP" and scans only that folder's
 * names, not SAVES's ~100 game folders as well. A name below s_cwd is named from it; any other name (the journal,
 * ACTIVE.TXT, another folder, s_cwd itself -- FatFs's f_stat refuses "" -- ) absolutely, which is always right; and the
 * run goes back to /aether before the journal and every other op (cd_home). Never ".." (exFAT folders have no dot
 * entries). A chdir that fails with "no such folder" left the folder as it was (FatFs); any other failure (a timeout:
 * it may have moved) makes the rest of the run name everything absolutely (s_rootn), so no name can ever be
 * resolved from a folder the run does not know. */
static char s_cwd[OM_PGS_DIR];
/* an absolute "/aether/SAVES/<f>" fixed name (JOURNAL.TXT, ACTIVE.TXT, GENLIST.TXT, SAVES itself) as this run names
 * it: from /aether, or absolutely while s_cwd is a save folder (none of them is below one) */
static const char *F(const char *abs) { return s_rootn || s_cwd[0] ? abs : abs + (sizeof ROOT - 1); }
#ifdef __sh__
_Static_assert(OM_PGS_ARENA <= 0x28000u, "launcher.ld's heap floor counts 0x28000 for the save swap's arena");
#endif
#define PATHMAX 256u

static const om_pgs_env *E;
static om_pgs_result *R;
static uint32_t s_al[PATHMAX / 4];            /* 4-aligned name for s_stat/s_unlink/s_mkdir/s_opendir */
static uint32_t s_st[(300 + 4) / 4];          /* s_stat_t + name */
static uint8_t s_cov[OM_PGS_LIST];            /* covered[] of the coverage gate; seen[] of verify_dir */
static uint32_t s_ctr;
/* torn BRAM records met by this run, and their card copies kept current (carry_torn) */
static uint32_t s_torn_seen, s_torn_kept;
static char s_label[OM_GID_MAX + 16];

#define AL4(a) ((uint8_t *)(((uintptr_t)(a) + 3u) & ~(uintptr_t)3u))
void om_pgs_arena_split(om_pgs_env *e, uint8_t *a)
{   /* each step rounded up to 4: at most 7 x 3 B, inside OM_PGS_ARENA's 64 spare bytes */
    e->lib = a;                           a = AL4(a + OM_BRAM_LIB_BYTES + OM_BRAM_GUARD);
    e->work = a;                          a = AL4(a + OM_BRAM_WORK_BYTES + OM_BRAM_GUARD);
    e->list = (om_bup_dir *)a;            a = AL4(a + OM_PGS_LIST * sizeof(om_bup_dir));
    e->taken = (char (*)[OM_PGS_FNAME])a; a = AL4(a + OM_PGS_LIST * OM_PGS_FNAME);
    e->ents = (om_pgs_ent *)a;            a = AL4(a + OM_PGS_ENTS * sizeof(om_pgs_ent));
    e->rec = a;                           a = AL4(a + OM_PGS_REC);
    e->chk = a;                           a = AL4(a + OM_PGS_REC);
    e->scr = a;
}

/* --- strings and messages --------------------------------------------------------------------------------------- */
static char *cat(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }
static char *cat_i(char *o, int v)
{
    char t[12];
    int k = 0;
    uint32_t u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0) *o++ = '-';
    do { t[k++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while (k) *o++ = t[--k];
    *o = 0;
    return o;
}

static void path(char out[PATHMAX], const char *dir, const char *suffix, const char *file)
{   /* /aether/<dir><suffix>[/<file>] as this run names it: from s_cwd when below it, from /aether (rel), or
     * absolutely -- every part is bounded (OM_PGS_DIR, OM_PGS_ENAME) */
    char v[PATHMAX];
    uint32_t c = (uint32_t)strlen(s_cwd);
    char *o = cat(cat(v, dir), suffix);
    if (file) cat(cat(o, "/"), file);
    if (!s_rootn && c && !strncmp(v, s_cwd, c) && v[c] == '/') { strcpy(out, v + c + 1); return; }
    cat(cat(out, s_rootn || c ? ROOT : ""), v);
}
/* a name path() made, back to the folder name below /aether it stands for */
static int unpath(const char *q, char out[OM_PGS_DIR])
{
    uint32_t c = (uint32_t)strlen(s_cwd);
    if (q[0] == '/') {
        if (strncmp(q, ROOT, sizeof ROOT - 1) || strlen(q) - (sizeof ROOT - 1) >= OM_PGS_DIR) return OM_E_FIELD;
        strcpy(out, q + sizeof ROOT - 1);
    } else if (c) {
        if (c + 1 + strlen(q) >= OM_PGS_DIR) return OM_E_FIELD;
        strcpy(cat(cat(out, s_cwd), "/"), q);
    } else {
        if (strlen(q) >= OM_PGS_DIR) return OM_E_FIELD;
        strcpy(out, q);
    }
    return 0;
}

static void copy_cap(char *dst, const char *line, uint32_t cap)
{
    uint32_t n = (uint32_t)strlen(line);
    if (n >= cap) n = cap - 1;
    memcpy(dst, line, n);
    dst[n] = 0;
}
static void copy_line(char *dst, const char *line) { copy_cap(dst, line, OM_PGS_MSG); }
static void set_notice(const char *line) { copy_cap(R->notice, line, OM_PGS_NOTICE); }

static int fail(int op, int rc, const char *line)
{
    char *o;
    if (R->rc) return R->rc;                      /* the first failure is the one reported */
    R->rc = rc ? rc : -1;
    R->op = (uint8_t)op;
    if (line)
        copy_line(R->line, line);
    else if ((rc >= OM_BUP_NON && rc <= OM_BUP_BROKEN) || (rc <= OM_BRAM_E_NOLIB && rc >= OM_BRAM_E_SMPC))
        om_bup_msg(R->line, OM_PGS_MSG, om_pgs_op_name((uint32_t)op), rc);
    else {
        char t[64];
        o = cat(cat(cat(t, "SAVES: "), om_pgs_op_name((uint32_t)op)), " FAILED rc=");
        cat_i(o, rc);
        copy_line(R->line, t);
    }
    return R->rc;
}

static void say(const char *s) { if (E->say) E->say(s); }

/* the swap's live progress (pgsbak.h om_pgs_prog): the step, the file written now */
static om_pgs_prog s_sp;
static void sp_emit(void) { if (E->prog) E->prog(&s_sp); }
static void sp_file(const char *fn, uint32_t len, int done)
{
    strncpy(s_sp.file, fn, sizeof s_sp.file - 1);
    s_sp.file[sizeof s_sp.file - 1] = 0;
    s_sp.file_total = len;
    s_sp.file_done = done ? len : 0;
    s_sp.beat++;
    sp_emit();
}

/* --- SD helpers --------------------------------------------------------------------------------------------------- */
static const char *al(const char *p)
{
    uint32_t n = (uint32_t)strlen(p);
    if (n >= PATHMAX) n = PATHMAX - 1;
    memcpy(s_al, p, n);
    ((char *)s_al)[n] = 0;
    return (const char *)s_al;
}

static int sd_rc(int rc) { return rc < 0 ? OM_SDW_ERR(rc) : 0; }
static int absent(int rc) { return rc == -FR_NO_FILE || rc == -FR_NO_PATH; }

static int sd_exists(const char *p)            /* 1, 0, or < 0 */
{
    int rc;
    om_yield();                                 /* the launch's eye (yield.h): before each path command */
    rc = s_stat(al(p), (s_stat_t *)s_st, (int)sizeof s_st - 4);
    if (rc >= 0) return 1;
    return absent(rc) ? 0 : sd_rc(rc);
}

static int sd_mkdir(const char *p)
{
    int rc;
    om_yield();
    rc = s_mkdir(al(p));
    return rc == -FR_EXIST ? 0 : sd_rc(rc);
}

/* swap speed (hardware 2026-11-24, test_pgscost.c): every path command -- an open, a named stat, an opendir, a mkdir, a
 * rename, an unlink -- makes the Satiator walk the card from its root, ~700 names on the user's exFAT card, so each one
 * costs tens of milliseconds there. SAVES is made once a run, not before every journal line. */
static uint8_t s_saves_ok;
static int saves_dir(void)
{
    int rc;
    if (s_saves_ok) return 0;
    if ((rc = sd_mkdir(F(OM_PGS_SAVES))) == 0) s_saves_ok = 1;
    return rc;
}

static int sd_unlink(const char *p)
{
    int rc;
    om_yield();
    rc = s_unlink(al(p));
    return absent(rc) ? 0 : sd_rc(rc);
}

static int is_bup(const om_pgs_ent *e)
{
    uint32_t n = (uint32_t)strlen(e->name);
    const char *x = e->name + n - 4;
    return !e->isdir && n > 4 && x[0] == '.' && (x[1] | 32) == 'b' && (x[2] | 32) == 'u' && (x[3] | 32) == 'p';
}

/* each listed entry's size (swap speed: has_copy reads only the files of the very size it looks for) */
static uint32_t s_esz[OM_PGS_ENTS];

/* swap speed: the folder scan_dir last read into E->ents (its headers and duplicate rule), kept until anything lists
 * another folder into E->ents (list_dir) or writes a file (put_file): a scan of the same folder again reads nothing */
static char s_scd[OM_PGS_DIR];
static uint32_t s_scn;
static uint8_t s_sc_ok, s_sc_strict;

/* swap speed, part 5 (2026-11-24): a listing of the working folder itself (option A's save folder) is named "" -- FatFs
 * opens the origin folder for an empty name, where an absolute name walks the card's root again. Untried on the
 * Satiator, so it is tried once a run against the absolute listing: the same entries (names and sizes, in order) and
 * it is used for the rest of the run; any difference or error, and the run keeps the absolute name (s_dot) */
static uint8_t s_dot;                           /* 0 untried this run, 1 "" lists the working folder, 2 it does not */
static uint32_t dot_crc(uint32_t c, const char *name, uint32_t size)
{
    c = om_crc32(c, name, (uint32_t)strlen(name) + 1u);
    return om_crc32(c, &size, sizeof size);
}
/* the absolute name of the working folder itself? (path() names it so: FatFs's f_stat refuses "") */
static int is_cwd(const char *p)
{
    return !s_rootn && s_cwd[0] && !strncmp(p, ROOT, sizeof ROOT - 1) && !strcmp(p + sizeof ROOT - 1, s_cwd);
}
/* the try: the absolute listing is in E->ents (n entries, crc); then "" drained without storing */
static void dot_try(uint32_t n, uint32_t crc)
{
    static const uint32_t empty = 0;
    s_stat_t *st = (s_stat_t *)s_st;
    uint32_t m = 0, c = 0, k = 0;
    int len;
    s_dot = 2;
    om_yield();
    if (s_opendir((const char *)&empty) < 0) return;
    while ((len = s_stat(NULL, st, (int)sizeof s_st - 4)) > 0) {
        if (!(++k & 15u)) om_yield_draw();      /* inside a listing: a frame, no SD command (yield.h) */
        st->name[len] = 0;
        if (!strcmp(st->name, ".") || !strcmp(st->name, "..")) continue;
        c = dot_crc(c, st->name, st->size);
        m++;
    }
    if (len == SAT_ETIMEDOUT) return;
    if (m == n && c == crc) s_dot = n ? 1 : 0;      /* an empty folder proves nothing: tried again later */
}

/* the folder's entries into E->ents ("." and ".." dropped), their sizes into s_esz; *n = 0 for an absent folder */
static int list_dir(const char *p, uint32_t *n)
{
    static const uint32_t empty = 0;
    s_stat_t *st = (s_stat_t *)s_st;
    int len, rc, self = is_cwd(p), probe = self && s_dot == 0;
    uint32_t crc = 0;
    *n = 0;
    s_sc_ok = 0;
    om_yield();
    if ((rc = s_opendir(self && s_dot == 1 ? (const char *)&empty : al(p))) < 0)
        return absent(rc) ? 0 : sd_rc(rc);
    while ((len = s_stat(NULL, st, (int)sizeof s_st - 4)) > 0) {
        om_pgs_ent *e;
        if (!(*n & 15u)) om_yield_draw();
        st->name[len] = 0;
        if (!strcmp(st->name, ".") || !strcmp(st->name, ".."))
            continue;
        if ((uint32_t)len >= OM_PGS_ENAME) return OM_E_FIELD;
        if (*n >= OM_PGS_ENTS) return OM_E_TABLESIZE;
        s_esz[*n] = st->size;
        e = &E->ents[(*n)++];
        memcpy(e->name, st->name, (uint32_t)len + 1);
        e->isdir = (st->attrib & AM_DIR) != 0;
        e->win = 0;
        if (probe) crc = dot_crc(crc, st->name, st->size);
    }
    if (len == SAT_ETIMEDOUT)                       /* satiator.h: a timeout is never "no more entries" (satfs.c) */
        return sd_rc(len);
    if (probe) dot_try(*n, crc);
    return 0;
}

/* swap speed: the last folder chain mk_all made this run (as pgsbak.c's import does): a folder made, or found, once in
 * a run is not asked for again -- the launcher never removes one, and every s_mkdir walks the card from its root */
static char s_made[PATHMAX];
static int mk_all(const char *dir)
{   /* every folder of /aether/<dir>, from SAVES down; s_made and s_cwd (and so every folder above it) exist */
    char v[OM_PGS_DIR], p[PATHMAX];
    uint32_t i, n = (uint32_t)strlen(dir), c = (uint32_t)strlen(s_cwd);
    int rc;
    if (n >= OM_PGS_DIR) return OM_E_FIELD;
    strcpy(v, dir);
    for (i = 1; i <= n; i++)
        if (v[i] == '/' || !v[i]) {
            char ch = v[i];
            v[i] = 0;
            if ((!strncmp(s_made, v, i) && (s_made[i] == '/' || !s_made[i]))
                || (c >= i && !strncmp(s_cwd, v, i) && (s_cwd[i] == '/' || !s_cwd[i])))
                rc = 0;
            else {
                path(p, v, "", 0);
                rc = sd_mkdir(p);
            }
            v[i] = ch;
            if (rc) return rc;
        }
    strcpy(s_made, dir);
    return 0;
}

/* option A: the working folder (above). to: a save folder below /aether, or "" for /aether */
static int cd_name(const char *to)
{
    static uint32_t b[(PATHMAX + 4) / 4];
    char *o = (char *)b;
    uint32_t c = (uint32_t)strlen(s_cwd), n = (uint32_t)strlen(to);
    int rc;
    if (s_rootn || !strcmp(s_cwd, to)) return 0;
    if (n >= OM_PGS_DIR) return 0;
    if (!n) strcpy(o, OM_CARD_DIR);                               /* home: absolutely (no "..") */
    else if (!c) strcpy(o, to);                                   /* from /aether */
    else if (!strncmp(to, s_cwd, c) && to[c] == '/') strcpy(o, to + c + 1);   /* deeper */
    else cat(cat(o, ROOT), to);
    om_yield();
    rc = s_chdir(o);
    if (rc == 0) { strcpy(s_cwd, to); return 0; }
    if (rc == -FR_NO_PATH || rc == -FR_NO_FILE) return 0;         /* no such folder: it stayed where it was */
    s_rootn = sizeof ROOT - 1;                                    /* anything else: the folder is not known */
    s_cwd[0] = 0;
    R->rel = 0;                                                   /* this run ends with absolute names */
    return 0;
}
static void cd_home(void) { cd_name(""); }
/* into save folder dir for an op's many commands -- only when they are many (the chdir in and the one home cost about
 * two commands from /aether): n, the records or files it will name, at least E->deep (default 4) */
static int cur_of(const char *dir, char out[OM_PGS_DIR], uint32_t *g);
static void cd_slot(const char *dir, uint32_t n)
{
    uint32_t min = E->deep ? E->deep : 4u;
    if (n >= min && min != 255u) {
        char o[OM_PGS_DIR];
        uint32_t g;
        (void)cur_of(dir, o, &g);                   /* found from here (the folder itself is not named from inside it);
                                                     * the result is deliberately ignored: kept for the run, and every
                                                     * caller asks cur_of again, so an error is met where it matters */
        cd_name(dir);
    }
}

/* a save file into buf (OM_PGS_REC bytes): ONE path command (swap speed) -- the open, then 2 KB reads through E->scr
 * (4-aligned, as buffer_xfer wants) until one returns nothing, so no stat for the size (om_sdw_read_file's), as
 * pgsbak.c's read_at does. Only for files a listing just named: an absent one is an error, never a "no file" answer.
 * OM_E_FULL when the file does not fit with 4 bytes to spare (om_sdw_read_file's rule). A short piece is never taken for
 * the end: only a read that returns 0 is. */
static int read_file(const char *p, uint8_t *buf, uint32_t *len)
{
    uint32_t got = 0;
    int fd, r = 0;
    om_yield();
    if ((fd = s_open(al(p), FA_READ | FA_OPEN_EXISTING)) < 0) return sd_rc(fd) ? sd_rc(fd) : -1;
    for (;;) {
        uint32_t n = OM_PGS_REC - got > S_MAXBUF ? S_MAXBUF : OM_PGS_REC - got;
        if (n) om_yield();
        if (!n || (r = s_read(fd, E->scr, (int)n)) <= 0) break;
        memcpy(buf + got, E->scr, (uint32_t)r);
        got += (uint32_t)r;
    }
    s_close(fd);
    if (got + 4u > OM_PGS_REC) return OM_E_FULL;   /* the file reached the buffer's slack: too big */
    if (r < 0) return sd_rc(r);
    *len = got;
    return 0;
}

/* M6 Task 24b: /aether/<dir>'s current set (savegen.h: its newest complete generation, else its own files), as a
 * folder below /aether/; *g = the generation (0 = <dir> itself) */
static om_gen_scr s_gs;
/* swap speed: a folder's current set is found once a run (two or three listings and a stat each time) and remembered
 * until this run makes a generation complete (op_verify's DONE.TXT: cur_forget) -- nothing else in a run changes it */
#define CUR_N 6u
static struct { char dir[OM_PGS_DIR], out[OM_PGS_DIR]; uint32_t g; } s_cur[CUR_N];
static uint32_t s_ncur;
static void cur_forget(void) { s_ncur = 0; }
static int cur_of(const char *dir, char out[OM_PGS_DIR], uint32_t *g)
{
    char p[PATHMAX], q[PATHMAX];
    uint32_t k;
    int rc;
    for (k = 0; k < s_ncur; k++)
        if (!strcmp(s_cur[k].dir, dir)) { strcpy(out, s_cur[k].out); *g = s_cur[k].g; return 0; }
    path(p, dir, "", 0);
    if ((rc = om_gen_cur(p, q, g, &s_gs)) != 0) return rc;
    if ((rc = unpath(q, out)) != 0) return rc;
    if (strlen(dir) < OM_PGS_DIR) {
        k = s_ncur < CUR_N ? s_ncur++ : CUR_N - 1u;
        strcpy(s_cur[k].dir, dir);
        strcpy(s_cur[k].out, out);
        s_cur[k].g = *g;
    }
    return 0;
}

/* --- the quarantine (R2/R3: a BRAM Aether did not leave is copied into a new SAVES/_QUARANTINE/<n>) ------------- */
static int q_next(uint32_t *n)
{   /* the first number above every folder in SAVES/_QUARANTINE (1 when it is absent or empty). The listing
     * is streamed, never stored -- the add-only card keeps every quarantine folder */
    char p[PATHMAX];
    s_stat_t *st = (s_stat_t *)s_st;
    uint32_t max = 0, max_k = 0;
    int len = 0, rc;
    path(p, OM_PGS_QUAR, "", 0);
    om_yield();
    if ((rc = s_opendir(al(p))) < 0) {
        if (!absent(rc)) return sd_rc(rc);
    } else
        while ((len = s_stat(NULL, st, (int)sizeof s_st - 4)) > 0) {
            const char *c = st->name;
            uint32_t v = 0;
            if (!(++max_k & 15u)) om_yield_draw();   /* inside a listing: a frame, no SD command (yield.h) */
            st->name[len] = 0;
            while (*c >= '0' && *c <= '9' && v < 100000u) v = v * 10u + (uint32_t)(*c++ - '0');
            if (!*c && c != st->name && v > max) max = v;
        }
    if (len == SAT_ETIMEDOUT) return sd_rc(len);
    if (max >= 65535u) return fail(OM_OP_QUARANTINE, OM_E_FULL, "SAVES: _QUARANTINE FULL, CLEAN IT ON A PC");
    *n = max + 1u;
    return 0;
}

/* --- the journal and ACTIVE.TXT --------------------------------------------------------------------------------- */
static om_pgs_journal s_j;

static int j_put(uint8_t op, uint8_t ph, uint8_t undo)
{
    char line[200];
    int n, rc;
    s_j.op = op; s_j.ph = ph; s_j.undo = undo;
    if ((n = om_pgs_journal_fmt(line, sizeof line, &s_j)) < 0) return n;
    cd_home();                                      /* the journal is not below a save folder */
    if ((rc = saves_dir()) != 0) return rc;
    return om_sdw_replace(F(OM_PGS_JOURNAL), (const uint8_t *)line, (uint32_t)n, E->scr, E->gap, 0);
}

/* swap speed: ACTIVE.TXT and GENLIST.TXT are read up to five times a run (the marker check, the deleted-versions list,
 * the refresh), each read a stat and an open from the card's root -- so each is read once a run and kept here (small:
 * a few ids; a larger file is read each time), until this run replaces or deletes it (txt_forget) */
#define TXT_CAP 2048u
static struct { const char *path; int rc; uint32_t len; uint8_t ok; uint32_t b[TXT_CAP / 4]; } s_txt[2] = {
    { OM_PGS_ACTIVE, 0, 0, 0, { 0 } }, { OM_PGS_GENLIST, 0, 0, 0, { 0 } } };
static void txt_forget(const char *p)
{
    uint32_t k;
    for (k = 0; k < 2; k++)
        if (!p || !strcmp(p, s_txt[k].path)) s_txt[k].ok = 0;
}
/* om_sdw_read_file of ACTIVE.TXT or GENLIST.TXT into E->chk (OM_PGS_REC) */
static int txt_read(const char *p, uint32_t *len)
{
    uint32_t k;
    int rc;
    for (k = 0; k < 2 && strcmp(p, s_txt[k].path); k++) ;
    if (k < 2 && s_txt[k].ok) {
        if (!s_txt[k].rc) { memcpy(E->chk, s_txt[k].b, s_txt[k].len); *len = s_txt[k].len; }
        return s_txt[k].rc;
    }
    rc = om_sdw_read_file(F(p), E->chk, OM_PGS_REC, len);
    if (k < 2 && (rc == OM_SDW_NOFILE || (!rc && *len <= TXT_CAP))) {
        s_txt[k].rc = rc;
        s_txt[k].len = rc ? 0 : *len;
        if (!rc) memcpy(s_txt[k].b, E->chk, *len);
        s_txt[k].ok = 1;
    }
    return rc;
}

static int unlink_both(const char *p)
{   /* "<p>.NEW" first: with p still there, a cut in between leaves the line that was current */
    char q[PATHMAX];
    int rc;
    txt_forget(p);
    cat(cat(q, F(p)), ".NEW");
    if ((rc = sd_unlink(q)) != 0) return rc;
    return sd_unlink(F(p));
}

/* --- BRAM helpers ----------------------------------------------------------------------------------------------- */
/* swap speed, part 4 (2026-11-24): BRAM is listed once a run and each record's state (intact or torn) learned once --
 * the first launch from a general set of 24 records made 34 listings and 209 record reads. Only this run writes BRAM
 * while it runs, and every write and delete goes through bwrite/bdelete, which forget the listing (bram_dirty); so do
 * the library's Init and the backup (which lists into the same table). Record DATA is never cached: every comparison
 * that decides what is kept still reads BRAM's bytes. */
static uint8_t s_bl_ok, s_bst[OM_PGS_LIST];      /* s_bst: 0 not read yet, 1 intact, 2 torn */
static uint32_t s_bl_n;
static void bram_dirty(void) { s_bl_ok = 0; }
static int bram_list(uint32_t *n)
{
    int rc;
    if (s_bl_ok) { *n = s_bl_n; return 0; }
    om_yield();
    rc = om_bram_list(E->list, OM_PGS_LIST, n);
    if (!rc) { s_bl_ok = 1; s_bl_n = *n; memset(s_bst, 0, sizeof s_bst); }
    return rc;
}
/* every backup-library call after a chance for the launch's eye (yield.h): a record's read, write or delete is
 * milliseconds, and the swap's loops make dozens of them in a row */
static int bwrite(const om_bup_dir *d, const uint8_t *data) { int rc; om_yield(); rc = om_bram_write(d, data); bram_dirty(); return rc; }
static int bdelete(const char nm[12]) { int rc; om_yield(); rc = om_bram_delete(nm); bram_dirty(); return rc; }
static int bread(const char nm[12], uint8_t *buf, uint32_t cap, om_bup_dir *d) { om_yield(); return om_bram_read(nm, buf, cap, d); }
static int is_res(const char name[12]) { return om_pgs_reserved(name); }
static void name12(char nm[12], const char *s) { memset(nm, 0, 12); memcpy(nm, s, strlen(s)); }

static int bram_has(const char *s)
{
    char nm[12];
    uint8_t b[4];
    om_bup_dir d;
    name12(nm, s);
    return bread(nm, b, 0, &d) != OM_BUP_NOTFOUND;     /* OM_E_FULL (cap 0) = present */
}

typedef struct { uint8_t has; char gid[OM_GID_MAX]; uint8_t slot; uint32_t nonce; } line_t;

static int read_marker(line_t *m)
{   /* 0 and m->has; OM_E_FIELD for a marker that does not parse (treated as foreign) */
    uint8_t buf[64];
    om_bup_dir d;
    char nm[12];
    int rc;
    memset(m, 0, sizeof *m);
    name12(nm, OM_PGS_MARKER);
    rc = bread(nm, buf, sizeof buf, &d);
    if (rc == OM_BUP_NOTFOUND) return 0;
    if (rc == OM_E_FULL || rc == OM_BUP_BROKEN) { m->has = 1; return OM_E_FIELD; }
    if (rc) return rc;
    m->has = 1;
    return om_pgs_marker_parse(buf, d.datasize, m->gid, &m->slot, &m->nonce, NULL) ? OM_E_FIELD : 0;
}

static int read_active(line_t *a)
{   /* 0 and a->has; OM_E_FIELD for a file that does not parse */
    uint32_t len = 0, fp;
    int rc;
    memset(a, 0, sizeof *a);
    rc = txt_read(OM_PGS_ACTIVE, &len);
    if (rc == OM_SDW_NOFILE) return 0;
    if (rc) return rc;
    if (!len) return 0;                             /* an interrupted first write: no ACTIVE.TXT */
    a->has = 1;
    {   /* line 1; line 2 ("R ...", the loaded versions) is read by dl_load */
        uint32_t k;
        for (k = 0; k < len && E->chk[k] != '\n'; k++) ;
        if (k < len) len = k + 1;
    }
    return om_pgs_marker_parse(E->chk, len, a->gid, &a->slot, &a->nonce, &fp) ? OM_E_FIELD : 0;
}

static int bram_fp(uint32_t *fp, uint16_t *cnt)
{
    uint32_t n, c;
    int rc = bram_list(&n);
    if (rc) return rc;
    *fp = om_pgs_fp(E->list, n, sizeof E->list[0], &c);
    *cnt = (uint16_t)c;
    return 0;
}

/* E->list[i]'s record into E->chk, its directory entry into s_bgd; 0, OM_BUP_BROKEN or an rc */
static om_bup_dir s_bgd;
static int bram_get(uint32_t i, char nm[12])
{
    int rc;
    if (!om_bup_name_norm(nm, E->list[i].name)) return OM_E_FIELD;
    rc = bread(nm, E->chk, OM_PGS_REC, &s_bgd);
    if (s_bl_ok && i < OM_PGS_LIST && (!rc || rc == OM_BUP_BROKEN)) s_bst[i] = rc ? 2 : 1;
    return rc;
}
/* only whether E->list[i] is intact (0) or torn (OM_BUP_BROKEN), learned once a listing; E->chk is NOT filled */
static int bram_state(uint32_t i, char nm[12])
{
    if (!om_bup_name_norm(nm, E->list[i].name)) return OM_E_FIELD;
    if (s_bl_ok && i < OM_PGS_LIST && s_bst[i]) return s_bst[i] == 2 ? OM_BUP_BROKEN : 0;
    return bram_get(i, nm);
}

/* --- one folder's records ------------------------------------------------------------------------------------- */
/* /aether/<dir>'s "*.BUP" headers into E->ents[].hdr, the SPEC §5.16 duplicate rule applied (win = 1 for the file
 * restored), reserved names skipped (R4). *n = entries listed. strict: a file that does not decode fails the call, its
 * name in the message; else it is left out (win 0, hdr zeroed). */
static int scan_dir(int op, const char *dir, uint32_t *n, int strict)
{
    char p[PATHMAX];
    uint32_t i, k, len;
    int rc;
    om_bup_dir d;
    if (s_sc_ok && !strcmp(s_scd, dir) && (s_sc_strict || !strict)) { *n = s_scn; return 0; }   /* read already */
    path(p, dir, "", 0);
    if ((rc = list_dir(p, n)) != 0) return rc;
    for (i = 0; i < *n; i++) {
        om_pgs_ent *e = &E->ents[i];
        memset(e->hdr, 0, OM_BUP_HDR);
        if (!is_bup(e)) continue;
        path(p, dir, "", e->name);
        rc = read_file(p, E->rec, &len);
        if (!rc) rc = om_bup_decode_hdr(E->rec, len, &d);
        if (rc) {
            char m[OM_PGS_MSG + OM_PGS_ENAME];
            if (!strict) continue;
            if (rc != OM_E_FULL && rc != OM_E_SHORT && rc != OM_E_MAGIC && rc != OM_E_FIELD) return rc;
            cat(cat(m, rc == OM_E_FULL ? "SAVES: TOO BIG " : "SAVES: BAD FILE "), e->name);
            return fail(op, rc, m);
        }
        memcpy(e->hdr, E->rec, OM_BUP_HDR);
        e->win = !is_res(d.name);
    }
    for (i = 0; i < *n; i++)                                 /* duplicates: the losers drop out */
        for (k = 0; k < *n && E->ents[i].win; k++) {
            om_pgs_ent *a = &E->ents[i], *b = &E->ents[k];
            if (k == i || !b->win || memcmp(a->hdr + 0x10, b->hdr + 0x10, 11)) continue;
            if (!om_bup_prefer(a->hdr, a->name, b->hdr, b->name)) a->win = 0;
        }
    if (strlen(dir) < OM_PGS_DIR) {
        strcpy(s_scd, dir);
        s_scn = *n;
        s_sc_strict = (uint8_t)strict;
        s_sc_ok = 1;
    }
    return 0;
}

/* scan_dir over /aether/<dir>'s current set (savegen.h); cd = that folder */
static int scan_cur(int op, const char *dir, uint32_t *n, int strict, char cd[OM_PGS_DIR])
{
    uint32_t g;
    int rc;
    if ((rc = cur_of(dir, cd, &g)) != 0) return rc;
    return scan_dir(op, cd, n, strict);
}

/* E->rec (len) as the NEW file /aether/<dir>/<fn> -- "<name>.TMP" first (verify on), then renamed onto
 * the absent name, so a cut never leaves part of a ".BUP" (a ".TMP" a cut left is written again) */
static int put_file(const char *dir, const char *fn, uint32_t len)
{
    char p[PATHMAX], t[PATHMAX];
    uint32_t n;
    int rc;
    s_sc_ok = 0;                                    /* the folder gains a file */
    path(p, dir, "", fn);
    n = (uint32_t)strlen(p);
    memcpy(t, p, n - 4);
    memcpy(t + n - 4, ".TMP", 5);
    sp_file(fn, len, 0);
    if ((rc = om_sdw_write_file(t, E->rec, len, 1, E->scr, E->gap, 0)) != 0) return rc;
    om_yield();
    if ((rc = s_rename(t, p)) != 0) return sd_rc(rc) ? sd_rc(rc) : -1;
    sp_file(fn, len, 1);
    return 0;
}

/* the BRAM entry (index in E->list) holding the record a decoded header names, or -1 */
static int list_find(const uint8_t *hdr, uint32_t nb)
{
    char nm[12];
    uint32_t k;
    for (k = 0; k < nb; k++)
        if (om_bup_name_norm(nm, E->list[k].name) && !is_res(nm) && !memcmp(nm, hdr + 0x10, 11) && !nm[11])
            return (int)k;
    return -1;
}

typedef struct { uint8_t hdr[OM_BUP_HDR]; char file[OM_PGS_FNAME]; uint32_t blocks; } slotrec;
static slotrec s_sw[OM_PGS_ENTS];

/* 1 when /aether/<dir> holds a "*.BUP" byte-identical to E->rec (len); reads into E->chk, lists into E->ents */
#define OM_PGS_ALT "ALT"                 /* a generation's alternates (I7): never loaded */
static int has_copy(const char *dir, uint32_t len);
/* has_copy in /aether/<dir> or its ALT/ */
static int has_copy2(const char *dir, uint32_t len)
{
    char alt[OM_PGS_DIR + 4];
    if (has_copy(dir, len)) return 1;
    cat(cat(alt, dir), "/" OM_PGS_ALT);
    return has_copy(alt, len);
}
static int has_copy(const char *dir, uint32_t len)
{
    char p[PATHMAX];
    uint32_t n, i, l;
    path(p, dir, "", 0);
    if (list_dir(p, &n)) return 0;
    for (i = 0; i < n; i++) {
        if (!is_bup(&E->ents[i]) || s_esz[i] != len) continue;      /* another size: other bytes (swap speed) */
        path(p, dir, "", E->ents[i].name);
        if (!read_file(p, E->chk, &l) && l == len && !memcmp(E->chk, E->rec, len)) return 1;
    }
    return 0;
}

/* 1 when E->ents[i] (a winner of a current set, its file read into E->rec, len)
 * must stay current although BRAM does not hold it as it is: BRAM has no intact copy of that record (never loaded --
 * an import into the game BRAM holds --, or torn: fix round 3, N6), or the card's copy is preferred over BRAM's by
 * SPEC �5.16 (om_bup_prefer; BRAM's copy named as the archive would name it) and is not the same bytes. *torn: BRAM's
 * copy is torn. < 0 on an error */
/* M6 Task 24b fix (in-game deletes stick). s_dl: the record versions BRAM held when the set being archived or
 * relaunched was loaded (ACTIVE.TXT line 2). A record BRAM no longer has at all, at a version in that list, was deleted
 * by the game or the user: it is never restored nor carried (its file stays in the card's history). Mode 0 (no line
 * 2: a first launch, an older ACTIVE.TXT): every card record is new. Mode 2 (_GENERAL: no import ever goes there):
 * every record BRAM lacks was deleted. A torn BRAM record is not missing: its card copy is restored. */
static uint32_t s_dl[OM_PGS_LIST], s_ndl;
static uint8_t s_dl_mode;
/* alternates already announced ("IMPORTED SAVE DIFFERS - KEPT BOTH" at the launch where the console's copy
 * won, LOAD_NEWER): ACTIVE.TXT's optional third line "A <id> ...". s_an: read with the list (dl_load); s_ann: this
 * swap's, written by WRITE_MARKER. An archive never repeats an announced alternate's notice on a later game's screen */
#define AN_MAX 64u
static uint32_t s_an[AN_MAX], s_nan, s_ann[AN_MAX], s_nann;
/* versions imported into _GENERAL and never loaded (GENLIST.TXT's "I" line, pgsbak.c) */
static uint32_t s_imp[AN_MAX], s_nimp;

static int hexv(const uint8_t *d, uint32_t *v)
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

/* s_dl for /aether/<dir> (the marker's folder, or _GENERAL); reads ACTIVE.TXT into E->chk */
static int dl_load(const char *dir)
{
    uint32_t len = 0, k, v;
    int rc;
    s_ndl = 0;
    s_nan = 0;
    s_dl_mode = 0;
    s_nimp = 0;
    if (!strcmp(dir, OM_PGS_GENERAL)) {
        /* the general set's own list, SAVES/GENLIST.TXT ("\nR <id> ...\n", written when the general set
         * goes back into BRAM: DELETE_MARKER) -- Kai's SAVES/LATEST is imported into _GENERAL now, so "everything BRAM
         * lacks was deleted" no longer holds; with no list, every card record is new (R1) */
        rc = txt_read(OM_PGS_GENLIST, &len);
        if (rc == OM_SDW_NOFILE) return 0;
        if (rc) return rc;
        {   /* fix I9: the "I" line, the imported versions never loaded, wherever it is */
            uint32_t a, b;
            for (a = 0; a < len; a = b + 1) {
                for (b = a; b < len && E->chk[b] != '\n'; b++) ;
                if (b > a && E->chk[a] == 'I')
                    for (k = a + 1; k + 9 <= b && E->chk[k] == ' ' && s_nimp < AN_MAX; k += 9)
                        if (hexv(E->chk + k + 1, &v)) s_imp[s_nimp++] = v;
            }
        }
        k = 0;
    } else {
        rc = txt_read(OM_PGS_ACTIVE, &len);
        if (rc == OM_SDW_NOFILE) return 0;
        if (rc) return rc;
        for (k = 0; k < len && E->chk[k] != '\n'; k++) ;
    }
    if (!strcmp(dir, OM_PGS_GENERAL))               /* fix I9: GENLIST's "R" line may follow its "I" line */
        while (k + 1 < len && E->chk[k + 1] != 'R') {
            for (k++; k < len && E->chk[k] != '\n'; k++) ;
        }
    if (k + 3 > len || E->chk[k + 1] != 'R' || (E->chk[k + 2] != ' ' && E->chk[k + 2] != '\n')) return 0;
    s_dl_mode = 1;
    for (k += 2; k + 9 <= len && E->chk[k] == ' ' && s_ndl < OM_PGS_LIST; k += 9)
        if (hexv(E->chk + k + 1, &v)) s_dl[s_ndl++] = v;
    while (k < len && E->chk[k] != '\n') k++;
    if (k + 2 < len && E->chk[k + 1] == 'A')        /* line 3: the announced alternates (24c) */
        for (k += 2; k + 9 <= len && E->chk[k] == ' ' && s_nan < AN_MAX; k += 9)
            if (hexv(E->chk + k + 1, &v)) s_an[s_nan++] = v;
    return 0;
}

static int dl_has(uint32_t id)
{
    uint32_t k;
    for (k = 0; k < s_ndl && s_dl[k] != id; k++) ;
    return k < s_ndl;
}

/* E->rec (len), a card file of a record BRAM has no entry for: 1 = deleted (never restored) */
static int deleted(uint32_t len)
{
    char nm[12];
    if (s_dl_mode == 2) return 1;
    if (s_dl_mode == 0 || len < OM_BUP_HDR) return 0;
    memcpy(nm, E->rec + 0x10, 11);
    nm[11] = 0;
    if (!om_bup_name_norm(nm, nm)) return 0;
    return dl_has(om_pgs_rec_id(nm, om_be32(E->rec + 0x28), om_be32(E->rec + 0x2C),
                                om_crc32(0, E->rec + OM_BUP_HDR, len - OM_BUP_HDR)));
}

/* M6 Task 24b fix 4 (no date decides over the console). What a card version (E->rec, len) of a
 * current set does, given BRAM now:
 *   BRAM has no entry for the record: ACT_MAIN (it fills it: an import, a record new to the card) -- unless deleted();
 *   BRAM's copy is torn: ACT_MAIN for the set's own copy (N6), ACT_ALT for an alternate;
 *   BRAM holds that very version: ACT_SKIP;
 *   BRAM holds another version, intact: the console's copy is kept, always. A version this session never loaded (an
 *   import, another menu's copy: not on ACTIVE.TXT's list) is kept on the card as an ALTERNATE (ACT_ALT: carried into
 *   the generation's ALT/ sub-folder, never loaded, so never dropped); a listed one, or any with no list, is the
 *   session's own older copy: ACT_SKIP (it stays in the card's history).
 * A card version never overwrites an intact BRAM version; dates order and display, never decide. */
#define ACT_SKIP 0
#define ACT_MAIN 1
#define ACT_ALT  2
static int cand_act(uint32_t len, int is_main, int *torn)
{
    char nm[12], cn[12];
    uint32_t nb;
    int k, rc;
    om_bup_dir d;
    *torn = 0;
    if (len < OM_BUP_HDR) return ACT_SKIP;
    if ((rc = bram_list(&nb)) != 0) return rc;
    if ((k = list_find(E->rec, nb)) < 0) return deleted(len) ? ACT_SKIP : ACT_MAIN;
    rc = bram_get((uint32_t)k, nm);
    if (rc == OM_BUP_BROKEN) { *torn = 1; return is_main ? ACT_MAIN : ACT_ALT; }
    if (rc) return rc;
    d = s_bgd;                                      /* (it read the record once: E->chk) */
    if (len == OM_BUP_HDR + d.datasize && om_be32(E->rec + 0x28) == d.date
        && !memcmp(E->rec + OM_BUP_HDR, E->chk, d.datasize))
        return ACT_SKIP;                            /* the same version */
    memcpy(cn, E->rec + 0x10, 11);
    cn[11] = 0;
    if (!om_bup_name_norm(cn, cn)) return ACT_SKIP;
    {
        uint32_t id = om_pgs_rec_id(cn, om_be32(E->rec + 0x28), om_be32(E->rec + 0x2C),
                                    om_crc32(0, E->rec + OM_BUP_HDR, len - OM_BUP_HDR)), q;
        for (q = 0; q < s_nimp && s_imp[q] != id; q++) ;
        if (q < s_nimp) return ACT_ALT;             /* fix I9: an import into _GENERAL, never loaded: kept both */
        if (s_dl_mode != 1) return ACT_SKIP;       /* no list: the session's own older copy (C2) */
        if (dl_has(id)) return ACT_SKIP;            /* loaded this session: BRAM's copy is newer (C2) */
    }
    return ACT_ALT;                                 /* an import that differs: kept both (I7) */
}

/* "<gid>:" on the first 40-column row, the rest on the second (lseq.c shows a notice over two rows) */
static void differs_notice(const char *gid)
{
    char t[OM_PGS_NOTICE];
    uint32_t n;
    if (R->notice[0]) return;
    n = (uint32_t)strlen(gid);
    if (n > 38) n = 38;
    memcpy(t, gid, n);
    t[n++] = ':';
    while (n < 40) t[n++] = ' ';
    strcpy(t + 40, "IMPORTED SAVE DIFFERS - KEPT BOTH");
    set_notice(t);
}

/* the candidates of a current set /aether/<cur>: its winners, then its ALT/ files; into s_sw (file, blocks = 1 for
 * an alternate). *n: how many */
static int cands(const char *cur, uint32_t *nc)
{
    char p[PATHMAX], alt[OM_PGS_DIR + 4];
    uint32_t n, i;
    int rc;
    *nc = 0;
    if ((rc = scan_dir(OM_OP_ARCHIVE_BRAM_TO, cur, &n, 0)) != 0) return rc;
    for (i = 0; i < n && *nc < OM_PGS_ENTS; i++) {
        if (!E->ents[i].win || strlen(E->ents[i].name) >= OM_PGS_FNAME) continue;
        strcpy(s_sw[*nc].file, E->ents[i].name);
        s_sw[(*nc)++].blocks = 0;
    }
    cat(cat(alt, cur), "/" OM_PGS_ALT);
    path(p, alt, "", 0);
    if ((rc = list_dir(p, &n)) != 0) return rc;
    for (i = 0; i < n && *nc < OM_PGS_ENTS; i++) {
        if (!is_bup(&E->ents[i]) || strlen(E->ents[i].name) >= OM_PGS_FNAME) continue;
        strcpy(s_sw[*nc].file, E->ents[i].name);
        s_sw[(*nc)++].blocks = 1;
    }
    return 0;
}
static void cand_path(char p[PATHMAX], const char *cur, uint32_t i)
{
    char alt[OM_PGS_DIR + 4];
    cat(cat(alt, cur), "/" OM_PGS_ALT);
    path(p, s_sw[i].blocks ? alt : cur, "", s_sw[i].file);
}

/* fix round 3 N6's torn carry generalized: every record of the set the new
 * generation follows (/aether/<carry>, the current one) is copied in as cand_act says (a fill; an alternate), so no record of
 * the current set (an imported one, a torn one's card copy, a newer card copy) drops out of "current" */
/* E->rec (len)'s version id, and whether its alternate was announced already (24c) */
static uint32_t rec_id_of(uint32_t len)
{
    char cn[12];
    memcpy(cn, E->rec + 0x10, 11);
    cn[11] = 0;
    om_bup_name_norm(cn, cn);
    return om_pgs_rec_id(cn, om_be32(E->rec + 0x28), om_be32(E->rec + 0x2C),
                         om_crc32(0, E->rec + OM_BUP_HDR, len - OM_BUP_HDR));
}
static int announced(uint32_t len)
{
    uint32_t k, id = rec_id_of(len);
    for (k = 0; k < s_nan && s_an[k] != id; k++) ;
    return k < s_nan;
}

static int carry_cur(const char *dir, const char *carry, uint32_t *t)
{
    char p[PATHMAX], nm[12], fn[OM_PGS_FNAME], alt[OM_PGS_DIR + 4];
    uint32_t i, j, nc, len;
    int rc, torn, act;
    cat(cat(alt, dir), "/" OM_PGS_ALT);
    {   /* the names already in ALT/ (a resume after a cut) are taken too, so a rename never lands on one */
        uint32_t na, k;
        path(p, alt, "", 0);
        if ((rc = list_dir(p, &na)) != 0) return rc;
        for (k = 0; k < na && *t < OM_PGS_LIST; k++)
            if (!E->ents[k].isdir && strlen(E->ents[k].name) < OM_PGS_FNAME) strcpy(E->taken[(*t)++], E->ents[k].name);
    }
    if ((rc = cands(carry, &nc)) != 0) return rc;
    for (i = 0; i < nc; i++) {
        cand_path(p, carry, i);
        if ((rc = read_file(p, E->rec, &len)) != 0) return rc;
        memcpy(s_sw[i].hdr, E->rec, OM_BUP_HDR);
        if ((act = cand_act(len, !s_sw[i].blocks, &torn)) < 0) return act;
        if (act == ACT_MAIN)                        /* one fill per record: a second copy is an alternate */
            for (j = 0; j < i; j++)
                if (s_sw[j].hdr[0x3F] == ACT_MAIN && !memcmp(s_sw[j].hdr + 0x10, E->rec + 0x10, 11)) act = ACT_ALT;
        s_sw[i].hdr[0x3F] = (uint8_t)act;           /* the header copy's last byte: this candidate's act */
        if (act == ACT_SKIP) continue;
        if (torn && act == ACT_MAIN) s_torn_kept++;
        if (act == ACT_ALT && !s_sw[i].blocks && !announced(len))
            differs_notice(!strcmp(carry, OM_PGS_GENERAL) || !strncmp(carry, OM_PGS_GENERAL "/", sizeof OM_PGS_GENERAL)
                           ? "GENERAL SAVES" : s_j.s.marker_gid[0] ? s_j.s.marker_gid : "SAVES");
        if (has_copy(act == ACT_MAIN ? dir : alt, len)) continue;       /* carried already (a resume) */
        if (act == ACT_ALT && (rc = mk_all(alt)) != 0) return rc;
        memcpy(nm, E->rec + 0x10, 11);
        nm[11] = 0;
        if ((rc = om_pgs_file_name(fn, nm, (const char (*)[OM_PGS_FNAME])E->taken, *t)) != 0) return rc;
        if (*t < OM_PGS_LIST) strcpy(E->taken[(*t)++], fn);
        /* re-read: has_copy used E->chk only, E->rec still holds the file */
        if ((rc = put_file(act == ACT_MAIN ? dir : alt, fn, len)) != 0) return rc;
    }
    return 0;
}

/* --- the ops ---------------------------------------------------------------------------------------------------- */
/* every non-reserved, readable BRAM record into /aether/<dir> as a new file (verify on). Files already there are kept
 * and a record already there byte for byte is not written again (Task 24b: add-only -- a resume continues where a cut
 * stopped). carry: see carry_cur */
static int copy_bram(const char *dir, const char *carry)
{
    char p[PATHMAX], nm[12];
    uint32_t n, i, k, t = 0, ne = 0;
    int rc;
    om_bup_dir d;
    if ((rc = scan_dir(OM_OP_QUARANTINE, dir, &ne, 0)) != 0) return rc;
    for (k = 0; k < ne && t < OM_PGS_LIST; k++)
        if (strlen(E->ents[k].name) < OM_PGS_FNAME) strcpy(E->taken[t++], E->ents[k].name);
    if ((rc = bram_list(&n)) != 0) return rc;
    for (i = 0; i < n; i++) {
        char fn[OM_PGS_FNAME];
        int dup = 0;
        if (!om_bup_name_norm(nm, E->list[i].name)) return OM_E_FIELD;
        if (is_res(nm)) continue;
        /* read by the listed name: a name the library cannot read back (e.g. a non-11-character one, ST-162-R1
         * pp.46-47) fails here, before anything is cleared */
        rc = bread(nm, E->rec + OM_BUP_HDR, OM_PGS_REC - OM_BUP_HDR - 4u, &d);
        if (rc == OM_BUP_BROKEN) { s_torn_seen++; continue; }   /* a torn record: its data is gone (carry_cur) */
        if (rc) return rc;
        for (k = 0; k < ne && !dup; k++) {
            const uint8_t *h = E->ents[k].hdr;
            uint32_t len;
            if (!h[0] || memcmp(h + 0x10, nm, 11) || om_be32(h + 0x28) != d.date || om_be32(h + 0x2C) != d.datasize)
                continue;
            path(p, dir, "", E->ents[k].name);
            if (!read_file(p, E->chk, &len) && len == OM_BUP_HDR + d.datasize
                && !memcmp(E->chk + OM_BUP_HDR, E->rec + OM_BUP_HDR, d.datasize))
                dup = 1;
        }
        if (dup) continue;
        if ((rc = om_pgs_file_name(fn, nm, (const char (*)[OM_PGS_FNAME])E->taken, t)) != 0) return rc;
        if (t < OM_PGS_LIST) strcpy(E->taken[t++], fn);
        memcpy(d.name, nm, 12);
        if ((rc = om_bup_encode_hdr(&d, E->rec)) != 0) return rc;
        if ((rc = put_file(dir, fn, OM_BUP_HDR + d.datasize)) != 0) return rc;
    }
    return carry ? carry_cur(dir, carry, &t) : 0;
}

/* the archive writes generation s_j.g of <dir> (chosen with the plan, journaled), never touching a file
 * already on the card; s_j.g = 0: <dir>'s current set already equals BRAM, nothing is written */
static int op_archive(const char *dir)
{
    char gd[OM_PGS_DIR], cd[OM_PGS_DIR];
    uint32_t cg;
    int rc;
    if (!strcmp(dir, OM_PGS_GENERAL)) strcpy(s_label, "GENERAL SAVES");
    else strcpy(s_label, s_j.s.marker_gid[0] ? s_j.s.marker_gid : "SAVES");
    if (!s_j.g) return 0;
    if ((rc = dl_load(dir)) != 0) return rc;
    if ((rc = cur_of(dir, cd, &cg)) != 0) return rc;    /* the set this generation follows */
    om_gen_dir(gd, dir, s_j.g);
    if ((rc = mk_all(gd)) != 0) return rc;
    cd_slot(dir, s_j.fn);                           /* option A: every file of this archive is below it */
    return copy_bram(gd, cg == s_j.g ? 0 : cd);
}

static int op_quarantine(const char *dir)
{
    int rc;
    if ((rc = mk_all(dir)) != 0) return rc;
    return copy_bram(dir, 0);
}

static int verify_dir2(const char *nd, const char *carry, int part)
{   /* /aether/<nd> holds every intact non-reserved BRAM record byte for byte (data and directory fields), and every
     * other "*.BUP" there decodes and is a byte-identical copy of a file of /aether/<carry> -- the current set it
     * follows (carry_cur; carry == nd: the current set itself, nothing to compare).
     * part: the BRAM records are not all needed yet (an archive a cut stopped) */
    char p[PATHMAX], nm[12];
    uint32_t nb, nf, i, k, len, recs = 0, got = 0, u = 0;
    int rc;
    om_bup_dir d, b;
    if ((rc = bram_list(&nb)) != 0) return rc;
    path(p, nd, "", 0);
    if ((rc = list_dir(p, &nf)) != 0) return rc;
    memset(s_cov, 0, sizeof s_cov);
    for (i = 0; i < nb; i++) {                       /* torn records are not archived (copy_bram), so not counted */
        if (is_res(E->list[i].name)) { s_cov[i] = 1; continue; }
        rc = bram_state(i, nm);
        if (rc && rc != OM_BUP_BROKEN) return rc;
        if (rc) s_cov[i] = 1;
        recs += !rc;
    }
    for (i = 0; i < nf; i++) {
        int hit = 0;
        if (!is_bup(&E->ents[i])) continue;
        path(p, nd, "", E->ents[i].name);
        if ((rc = read_file(p, E->rec, &len)) != 0) return rc == OM_E_FULL ? OM_E_CRC : rc;
        if ((rc = om_bup_decode_hdr(E->rec, len, &d)) != 0 || is_res(d.name)) return OM_E_CRC;
        for (k = 0; k < nb && !hit; k++) {
            if (s_cov[k] || !om_bup_name_norm(nm, E->list[k].name) || memcmp(nm, d.name, 12)) continue;
            if ((rc = bread(nm, E->chk, OM_PGS_REC, &b)) != 0) return rc == OM_BUP_BROKEN ? OM_E_CRC : rc;
            if (b.datasize == d.datasize && len == OM_BUP_HDR + d.datasize
                && !memcmp(E->chk, E->rec + OM_BUP_HDR, d.datasize) && !memcmp(b.comment, d.comment, OM_BUP_COMMENT)
                && b.language == d.language && b.date == d.date)
                s_cov[k] = 1, hit = 1, got++;
        }
        if (!hit) {                                  /* a carried copy: checked below, after this listing */
            if (!carry || u == OM_PGS_LIST || strlen(E->ents[i].name) >= OM_PGS_FNAME) return OM_E_CRC;
            {   /* only a fill (a record BRAM lacks, not deleted; or torn) may sit beside BRAM's records: anything else
                 * (deleted; the session's older copy; an import that differs, I7) keeps the set from staying current,
                 * and a new generation is written with it in ALT/ or in history */
                int torn, w = cand_act(len, 1, &torn);
                if (w < 0) return w;
                if (w != ACT_MAIN) return OM_E_CRC;
                if ((rc = bram_list(&nb)) != 0) return rc;
            }
            strcpy(E->taken[u++], E->ents[i].name);
        }
    }
    if (!part && got != recs) return OM_E_CRC;
    if (carry && strcmp(carry, nd))
        for (i = 0; i < u; i++) {
            char f[OM_PGS_FNAME];
            strcpy(f, E->taken[i]);
            path(p, nd, "", f);
            if ((rc = read_file(p, E->rec, &len)) != 0) return rc == OM_E_FULL ? OM_E_CRC : rc;
            if (!has_copy2(carry, len)) return OM_E_CRC;
        }
    if (carry && strcmp(carry, nd)) {               /* the alternates: each one a copy from the set it follows */
        char alt[OM_PGS_DIR + 4], f[OM_PGS_FNAME];
        uint32_t na;
        cat(cat(alt, nd), "/" OM_PGS_ALT);
        path(p, alt, "", 0);
        if ((rc = list_dir(p, &na)) != 0) return rc;
        for (i = 0, u = 0; i < na && u < OM_PGS_LIST; i++)
            if (is_bup(&E->ents[i]) && strlen(E->ents[i].name) < OM_PGS_FNAME) strcpy(E->taken[u++], E->ents[i].name);
        for (i = 0; i < u; i++) {
            strcpy(f, E->taken[i]);
            path(p, alt, "", f);
            if ((rc = read_file(p, E->rec, &len)) != 0) return rc == OM_E_FULL ? OM_E_CRC : rc;
            if (!has_copy2(carry, len)) return OM_E_CRC;
        }
    }
    return 0;
}

/* the folder ARCHIVE wrote: generation s_j.g of <dir>, or <dir>'s current set when it wrote nothing */
static int arch_dir(const char *dir, char out[OM_PGS_DIR])
{
    uint32_t g;
    if (s_j.g) { om_gen_dir(out, dir, s_j.g); return 0; }
    return cur_of(dir, out, &g);
}

/* swap speed: 1 while this run has verified, byte for byte, that the plan's archive folder (its set from now on) holds
 * every intact non-reserved BRAM record (verify_dir2, every record: plan_gen with nothing to write, or VERIFY's phase
 * 0), and BRAM has not been written since -- nothing before CLEAR writes it. CLEAR's R2 gate (covered) asks exactly
 * that of the archive folder, and VERIFY with nothing written asks what plan_gen just checked: neither reads again.
 * Cleared by CLEAR (every BRAM write comes after it), by foreign() and at the start of a run; a resume sets it only
 * by running that check again itself (VERIFY's phase 0 after a journal line at or before it). */
static uint8_t s_bram_ok;

static int op_verify(const char *dir, uint8_t ph, uint8_t i)
{   /* Task 24b: ph 0 verifies the archive against BRAM; ph 1 makes the generation complete (DONE.TXT, savegen.h) -- the
     * folder's current set from then on. Nothing is moved, renamed or deleted (no ".OLD", no sweep) */
    char gd[OM_PGS_DIR], p[PATHMAX];
    static const uint8_t z[4] = { 0, 0, 0, 0 };
    int rc;
    if (ph == 0 && !s_j.g && s_bram_ok) return 0;   /* plan_gen verified this very set, nothing was written since */
    if (ph == 0 && (rc = dl_load(dir)) != 0) return rc;   /* (from /aether: ACTIVE.TXT is read once a run) */
    cd_slot(dir, s_j.fn);                           /* option A (s_j.fn: BRAM's records when it was planned) */
    if ((rc = arch_dir(dir, gd)) != 0) return rc;
    if (ph == 0) {
        if (s_j.g) {
            path(p, gd, "", 0);
            if ((rc = sd_exists(p)) != 1) return rc < 0 ? rc : OM_E_CRC;
        }
        {   /* the set this generation follows: its carried copies' source */
            char cd[OM_PGS_DIR];
            uint32_t cg;
            if ((rc = cur_of(dir, cd, &cg)) != 0) return rc;
            if ((rc = verify_dir2(gd, cd, 0)) != 0) return rc;
            s_bram_ok = 1;
        }
        if (!s_j.g) return 0;
        if ((rc = j_put(i, 1, 0)) != 0) return rc;
    }
    path(p, gd, "", OM_GEN_DONE);
    cur_forget();                                   /* the generation becomes the folder's current set */
    if ((rc = sd_exists(p)) != 0) return rc < 0 ? rc : 0;
    return om_sdw_write_file(p, z, 0, 0, E->scr, E->gap, 0);   /* empty: a cut write leaves the same empty file */
}

/* M6 Task 24b: the generation the plan's ARCHIVE writes, chosen once and journaled (a resume writes the very same
 * folder): none when the folder's current set already holds exactly BRAM's records (nothing played since, or a retry),
 * else the number above every generation of the folder, complete or not */
static int plan_gen(const om_pgs_op *ops, uint32_t n)
{
    char cd[OM_PGS_DIR], p[PATHMAX];
    uint32_t i, g, max;
    int rc;
    s_j.g = 0;
    for (i = 0; i < n && ops[i].op != OM_OP_ARCHIVE_BRAM_TO; i++) ;
    if (i == n) return 0;
    if ((rc = dl_load(ops[i].dir)) != 0) return rc;
    cd_slot(ops[i].dir, s_j.fn);                    /* option A: home again at the first journal line */
    if ((rc = cur_of(ops[i].dir, cd, &g)) != 0) return rc;
    if ((rc = verify_dir2(cd, cd, 0)) == 0) {     /* nothing to write: every record there stays current (I6) */
        uint32_t nb, k, n;
        char nm[12];
        s_bram_ok = 1;
        if ((rc = bram_list(&nb)) != 0) return rc;
        for (k = 0; k < nb; k++) {
            uint32_t j;
            if (!om_bup_name_norm(nm, E->list[k].name) || is_res(nm) || bram_state(k, nm) != OM_BUP_BROKEN) continue;
            s_torn_seen++;
            if ((rc = scan_dir(OM_OP_ARCHIVE_BRAM_TO, cd, &n, 0)) != 0) return rc;
            for (j = 0; j < n && !(E->ents[j].win && !memcmp(E->ents[j].hdr + 0x10, nm, 11)); j++) ;
            s_torn_kept += j < n;
        }
        return 0;
    }
    if (rc != OM_E_CRC) return rc;
    path(p, ops[i].dir, "", 0);
    if ((rc = om_gen_max(p, &max, &s_gs)) != 0) return rc;
    if (max >= OM_GEN_MAX) return OM_E_FULL;
    s_j.g = max + 1u;
    return 0;
}


static int slot_recs(const char *slotdir);
static int newer_than_slot(uint32_t g);
/* swap speed: the general-set files CHECK_FITS found to replace a slot record (LOAD_NEWER), so LOAD_NEWER reads only
 * those instead of the whole general set again; nothing in a run changes the general set after CHECK_FITS. A resume
 * that starts after CHECK_FITS has none: LOAD_NEWER scans */
#define NW_MAX 32u
static struct { char file[OM_PGS_FNAME]; uint32_t k; } s_nw[NW_MAX];
static uint32_t s_nnw;
static uint8_t s_nw_ok;
static char s_nw_dir[OM_PGS_DIR];
static int fits(const om_pgs_op *ops, uint32_t nops)
{
    om_bram_info inf;
    uint32_t nb, i, k, n, freeb, need = 0;
    int rc, marker = 0;
    char m[OM_PGS_MSG], cd[OM_PGS_DIR];
    if ((rc = om_bram_info_get(0, &inf)) != 0) return rc;
    if ((rc = bram_list(&nb)) != 0) return rc;
    freeb = inf.freeblock;
    for (i = 0; i < nb; i++)
        if (!is_res(E->list[i].name)) freeb += E->list[i].blocks;   /* R4: reserved records stay, occupied */
    for (k = 0; k < nops; k++) {
        marker |= ops[k].op == OM_OP_WRITE_MARKER;
        if (ops[k].op == OM_OP_LOAD_NEWER && k > 0) {  /* a newer copy's blocks instead of the slot's */
            if (s_j.s.has_marker || s_j.s.foreign) continue;   /* newer_than_slot: no general copy replaces one */
            cd_home();
            if ((rc = slot_recs(ops[k - 1].dir)) != 0) return rc;
            if (!strcmp(ops[k].dir, OM_PGS_GENERAL)) cd_slot(ops[k].dir, 8);   /* option A: the general set is big */
            if ((rc = scan_cur(OM_OP_CHECK_FITS, ops[k].dir, &n, 1, cd)) != 0) return rc;
            s_nnw = 0;
            s_nw_ok = strlen(ops[k - 1].dir) < OM_PGS_DIR;
            for (i = 0; i < n; i++) {
                int j;
                uint32_t b;
                if (!E->ents[i].win || (j = newer_than_slot(i)) < 0) continue;
                b = om_pgs_blocks(om_be32(E->ents[i].hdr + 0x2C), (uint32_t)E->ents[i].hdr[0x30] << 8
                                                                  | E->ents[i].hdr[0x31]);
                need = need + b > s_sw[j].blocks ? need + b - s_sw[j].blocks : 0;
                if (s_nnw == NW_MAX || strlen(E->ents[i].name) >= OM_PGS_FNAME) s_nw_ok = 0;
                else {
                    strcpy(s_nw[s_nnw].file, E->ents[i].name);
                    s_nw[s_nnw++].k = (uint32_t)j;
                }
            }
            if (s_nw_ok) strcpy(s_nw_dir, ops[k - 1].dir);
            continue;
        }
        if (ops[k].op != OM_OP_LOAD_DIR_TO_BRAM) continue;
        if (!strcmp(ops[k].dir, OM_PGS_GENERAL)) cd_slot(ops[k].dir, 8);       /* option A: the general set is big */
        else cd_home();
        if ((rc = scan_cur(OM_OP_CHECK_FITS, ops[k].dir, &n, 1, cd)) != 0) return rc;
        for (i = 0; i < n; i++)
            if (E->ents[i].win)
                need += om_pgs_blocks(om_be32(E->ents[i].hdr + 0x2C), (uint32_t)E->ents[i].hdr[0x30] << 8
                                                                     | E->ents[i].hdr[0x31]);
    }
    if (marker && !bram_has(OM_PGS_MARKER))
        need += om_pgs_blocks(OM_GID_MAX + 13u, 0); /* WRITE_MARKER's own record */
    if (need > freeb) {
        om_pgs_fit_msg(m, sizeof m, need, inf.blocksize);
        return fail(OM_OP_CHECK_FITS, OM_E_FULL, m);
    }
    return 0;
}

/* R2 coverage gate: every non-reserved BRAM record is byte-identical to a file in one of the plan's archive or load
 * folders (a torn record counts as covered: its data is gone already). 0, OM_PGS_E_FOREIGN, or an rc. */
static int covered(void)
{
    om_pgs_op ops[OM_PGS_MAX_OPS];
    char p[PATHMAX], nm[12];
    uint32_t nops, nb, nf, i, k, j, len, left = 0;
    int rc;
    if ((rc = om_pgs_plan(&s_j.s, ops, OM_PGS_MAX_OPS, &nops)) != 0) return rc;
    if ((rc = bram_list(&nb)) != 0) return rc;
    for (i = 0; i < nb; i++) {
        s_cov[i] = (uint8_t)is_res(E->list[i].name);
        if (!s_cov[i]) {
            if (!om_bup_name_norm(nm, E->list[i].name)) return OM_PGS_E_FOREIGN;
            rc = bram_state(i, nm);
            if (rc == OM_BUP_BROKEN) s_cov[i] = 1;
            else if (rc) return rc;
        }
        left += !s_cov[i];
    }
    for (k = 0; k < nops && left; k++) {
        char dir[OM_PGS_DIR];                       /* the op folder's current set */
        if (ops[k].op != OM_OP_ARCHIVE_BRAM_TO && ops[k].op != OM_OP_QUARANTINE && ops[k].op != OM_OP_LOAD_DIR_TO_BRAM
            && ops[k].op != OM_OP_LOAD_NEWER)
            continue;
        if ((rc = scan_cur(OM_OP_CLEAR_BRAM_EXCEPT_MARKER, ops[k].dir, &nf, 0, dir)) != 0) return rc;
        if ((rc = bram_list(&nb)) != 0) return rc;
        for (j = 0; j < nf && left; j++) {
            const uint8_t *h = E->ents[j].hdr;
            if (!h[0]) continue;
            for (i = 0; i < nb; i++) {
                if (s_cov[i] || !om_bup_name_norm(nm, E->list[i].name) || memcmp(nm, h + 0x10, 11)
                    || E->list[i].date != om_be32(h + 0x28) || E->list[i].datasize != om_be32(h + 0x2C))
                    continue;
                path(p, dir, "", E->ents[j].name);
                if (read_file(p, E->rec, &len) || len != OM_BUP_HDR + E->list[i].datasize) continue;
                rc = bram_get(i, nm);
                if (rc == OM_BUP_BROKEN || (!rc && !memcmp(E->chk, E->rec + OM_BUP_HDR, E->list[i].datasize))) {
                    s_cov[i] = 1;
                    left--;
                } else if (rc && rc != OM_BUP_BROKEN)
                    return rc;
            }
        }
    }
    return left ? OM_PGS_E_FOREIGN : 0;
}

static int op_clear(void)
{
    uint32_t n, i;
    int rc;
    char nm[12];
    int ok = s_bram_ok;
    s_bram_ok = 0;
    if (!ok && (rc = covered()) != 0) return rc;    /* R2: nothing is deleted that is not on the card */
    if ((rc = bram_list(&n)) != 0) return rc;
    for (i = 0; i < n; i++) {
        if (!om_bup_name_norm(nm, E->list[i].name)) return OM_E_FIELD;
        if (is_res(nm)) continue;                   /* R4 */
        rc = bdelete(nm);
        if (rc && rc != OM_BUP_NOTFOUND) return rc;
    }
    return 0;
}

static int op_load(const char *slot)
{
    char p[PATHMAX], dir[OM_PGS_DIR];
    uint32_t n, i, len;
    int rc;
    om_bup_dir d;
    if (!strcmp(slot, OM_PGS_GENERAL)) cd_slot(slot, 8);   /* option A: the general set's many reads */
    if ((rc = scan_cur(OM_OP_LOAD_DIR_TO_BRAM, slot, &n, 1, dir)) != 0) return rc;   /* its current set */
    for (i = 0; i < n; i++) {
        if (!E->ents[i].win) continue;              /* a duplicate's loser, or a reserved name (R4) */
        path(p, dir, "", E->ents[i].name);
        if ((rc = read_file(p, E->rec, &len)) != 0) return rc;
        if ((rc = om_bup_decode_hdr(E->rec, len, &d)) != 0) return rc;
        if ((rc = bwrite(&d, E->rec + OM_BUP_HDR)) != 0) return rc;   /* owsw OFF: overwrites; verified */
    }
    return 0;
}

/* the slot folder's loaded records (header and file name), held while _GENERAL is listed */
static uint32_t s_nsw;

static int slot_recs(const char *slotdir)
{
    char cd[OM_PGS_DIR];
    uint32_t n, i;
    int rc;
    s_nsw = 0;
    if ((rc = scan_cur(OM_OP_LOAD_DIR_TO_BRAM, slotdir, &n, 1, cd)) != 0) return rc;
    for (i = 0; i < n; i++) {
        if (!E->ents[i].win) continue;
        memcpy(s_sw[s_nsw].hdr, E->ents[i].hdr, OM_BUP_HDR);
        strncpy(s_sw[s_nsw].file, E->ents[i].name, OM_PGS_FNAME - 1);
        s_sw[s_nsw].file[OM_PGS_FNAME - 1] = 0;
        s_sw[s_nsw].blocks = om_pgs_blocks(om_be32(E->ents[i].hdr + 0x2C), (uint32_t)E->ents[i].hdr[0x30] << 8
                                                                              | E->ents[i].hdr[0x31]);
        s_nsw++;
    }
    return 0;
}

/* E->ents[g] (a _GENERAL winner): the slot record it replaces, or -1. M6 Task 24b fix 4 (re-review I7, ruling: no date
 * decides): only when this swap archived the console's set into _GENERAL (case 3: no marker), so _GENERAL's copy IS
 * the console's -- it replaces the slot's (an import) whatever the dates; in case 2 _GENERAL holds no console version
 * and the slot's own copies load. (Fix round 2 ranked them by date.) op_load_newer skips a byte-identical copy */
static int newer_than_slot(uint32_t g)
{
    uint32_t k;
    if (s_j.s.has_marker || s_j.s.foreign) return -1;
    for (k = 0; k < s_nsw; k++)
        if (!memcmp(s_sw[k].hdr + 0x10, E->ents[g].hdr + 0x10, 11)) return (int)k;
    return -1;
}

/* LOAD_NEWER gdir after LOAD slotdir: each record the slot holds whose general-set copy is newer (om_bup_prefer) is
 * written over with that copy; the slot's own file is left in its folder (the next archive's generation supersedes it) */
static int op_load_newer(const char *gslot, const char *slotdir)
{
    char p[PATHMAX], gdir[OM_PGS_DIR];
    uint32_t n, i, len;
    int rc;
    om_bup_dir d;
    int memo;
    if (s_j.s.has_marker || s_j.s.foreign) return 0;   /* newer_than_slot: no general copy replaces a slot record */
    if ((rc = slot_recs(slotdir)) != 0) return rc;
    memo = s_nw_ok && !strcmp(s_nw_dir, slotdir);     /* CHECK_FITS's matches, in this run */
    if (memo) {
        uint32_t g;
        if ((rc = cur_of(gslot, gdir, &g)) != 0) return rc;
        n = s_nnw;
    } else if ((rc = scan_cur(OM_OP_LOAD_DIR_TO_BRAM, gslot, &n, 1, gdir)) != 0)
        return rc;
    for (i = 0; i < n; i++) {
        int k;
        char sd[OM_PGS_DIR];
        uint32_t g, l2 = 0;
        if (memo) k = (int)s_nw[i].k;
        else if (!E->ents[i].win || (k = newer_than_slot(i)) < 0) continue;
        path(p, gdir, "", memo ? s_nw[i].file : E->ents[i].name);
        if ((rc = read_file(p, E->rec, &len)) != 0) return rc;
        if ((rc = om_bup_decode_hdr(E->rec, len, &d)) != 0) return rc;
        if ((rc = cur_of(slotdir, sd, &g)) != 0) return rc;
        path(p, sd, "", s_sw[k].file);
        if (!read_file(p, E->chk, &l2) && l2 == len && !memcmp(E->chk + 0x10, E->rec + 0x10, 0x20)
            && !memcmp(E->chk + OM_BUP_HDR, E->rec + OM_BUP_HDR, len - OM_BUP_HDR))
            continue;                               /* the same version */
        differs_notice(s_j.s.want_gid);             /* the slot's copy is kept (an alternate at the next
                                                     * archive): said at THIS launch, where the console's copy wins */
        if (s_nann < AN_MAX && l2 >= OM_BUP_HDR) {      /* ... and never again at that archive (ACTIVE.TXT line 3) */
            char cn[12];
            memcpy(cn, E->chk + 0x10, 11);
            cn[11] = 0;
            om_bup_name_norm(cn, cn);
            s_ann[s_nann++] = om_pgs_rec_id(cn, om_be32(E->chk + 0x28), om_be32(E->chk + 0x2C),
                                            om_crc32(0, E->chk + OM_BUP_HDR, l2 - OM_BUP_HDR));
        }
        if ((rc = bwrite(&d, E->rec + OM_BUP_HDR)) != 0) return rc;
    }
    return 0;
}

/* ACTIVE.TXT = E->rec's line 1 (len1 bytes) + "R" and the id of every intact non-reserved BRAM record's version (and,
 * keep, every id of s_dl), written by om_sdw_replace (a state file, replaced in place) */
static int active_build(uint32_t len1, int keep, uint32_t *out)
{
    static const char hx[] = "0123456789ABCDEF";
    uint32_t nb, i, n = len1, ids = 0, k;
    char nm[12];
    int rc;
    om_bup_dir d;
    uint8_t *o = E->rec;
    if ((rc = bram_list(&nb)) != 0) return rc;
    o[n++] = 'R';
    for (i = 0; i < nb + (keep ? s_ndl : 0u); i++) {
        uint32_t id;
        if (i < nb) {
            if (!om_bup_name_norm(nm, E->list[i].name) || is_res(nm)) continue;
            if (bread(nm, E->chk, OM_PGS_REC, &d)) continue;       /* torn: no version to list */
            id = om_pgs_rec_id(nm, d.date, d.datasize, om_crc32(0, E->chk, d.datasize));
        } else
            id = s_dl[i - nb];
        for (k = 0; k < ids; k++) {                 /* each id once */
            uint32_t v;
            if (hexv(o + len1 + 2 + k * 9u, &v) && v == id) break;
        }
        if (k < ids || n + 10 > OM_PGS_REC) continue;
        o[n++] = ' ';
        for (k = 0; k < 8; k++) o[n++] = (uint8_t)hx[(id >> (28 - 4 * k)) & 15u];
        ids++;
    }
    o[n++] = '\n';
    {   /* line 3 (24c): the announced alternates -- this game's own (case 1 keeps them), or this swap's (WRITE_MARKER) */
        const uint32_t *a = keep ? s_an : s_ann;
        uint32_t na = keep ? s_nan : s_nann;
        if (na && n + 2 + na * 9u <= OM_PGS_REC) {
            o[n++] = 'A';
            for (i = 0; i < na; i++) {
                o[n++] = ' ';
                for (k = 0; k < 8; k++) o[n++] = (uint8_t)hx[(a[i] >> (28 - 4 * k)) & 15u];
            }
            o[n++] = '\n';
        }
    }
    *out = n;
    return 0;
}

static int active_write(uint32_t len1, int keep)
{
    uint32_t n;
    int rc;
    if ((rc = active_build(len1, keep, &n)) != 0) return rc;
    if ((rc = saves_dir()) != 0) return rc;
    txt_forget(OM_PGS_ACTIVE);
    return om_sdw_replace(F(OM_PGS_ACTIVE), E->rec, n, E->scr, E->gap, 0);
}

/* case 1: ACTIVE.TXT's line 1 kept, line 2 = the versions listed before (a deleted one stays deleted) and BRAM's now;
 * written only when that differs from the file, or a cut replace left ACTIVE.TXT.NEW */
static int active_refresh(void)
{
    uint32_t len = 0, len1, n;
    int rc;
    if ((rc = txt_read(OM_PGS_ACTIVE, &len)) != 0) return rc;
    for (len1 = 0; len1 < len && E->chk[len1] != '\n'; len1++) ;
    if (len1 == len) return 0;
    memcpy(E->rec, E->chk, ++len1);
    if ((rc = active_build(len1, 1, &n)) != 0) return rc;
    if ((rc = sd_exists(F(OM_PGS_ACTIVE ".NEW"))) < 0) return rc;
    if (!rc) {
        uint32_t l2 = 0;
        if ((rc = txt_read(OM_PGS_ACTIVE, &l2)) != 0) return rc;
        if (l2 == n && !memcmp(E->chk, E->rec, n)) return 0;
    }
    txt_forget(OM_PGS_ACTIVE);
    return om_sdw_replace(F(OM_PGS_ACTIVE), E->rec, n, E->scr, E->gap, 0);
}

/* case 1 -- the game BRAM holds is launched again. Every record of its current set
 * that fills a record BRAM lacks (an import into that set; a torn BRAM copy) is written into BRAM -- never over an intact
 * BRAM version (fix 4, I7) -- so the game sees
 * it now and the next archive keeps it. No journal: each write is a whole record (BUP_Write + Verify), the card copy
 * stays, and a cut one is torn in BRAM -- which is filled again at the next launch. When they do not fit, the
 * launch goes on with a notice. */
static int reconcile_load(void)
{
    char d[OM_PGS_DIR], cd[OM_PGS_DIR], p[PATHMAX], m[OM_PGS_MSG];
    uint32_t nc, i, len, need = 0, nb, g;
    int rc, torn, k, act, mode0;
    om_bram_info inf;
    om_bup_dir bd;
    if (om_pgs_slot_dir_po(d, s_j.s.want_gid, s_j.s.want_slot, s_j.s.want_po)) return 0;
    if ((rc = dl_load(d)) != 0) return fail(OM_OP_LOAD_DIR_TO_BRAM, rc, 0);
    mode0 = s_dl_mode == 0;
    if ((rc = cur_of(d, cd, &g)) != 0 || (rc = cands(cd, &nc)) != 0) return fail(OM_OP_LOAD_DIR_TO_BRAM, rc, 0);
    for (k = 0; k < 2; k++) {                       /* pass 0: the blocks they need; pass 1: the writes */
        for (i = 0; i < nc; i++) {
            cand_path(p, cd, i);
            if ((rc = read_file(p, E->rec, &len)) != 0) return fail(OM_OP_LOAD_DIR_TO_BRAM, rc, 0);
            if ((act = cand_act(len, !s_sw[i].blocks, &torn)) < 0) return fail(OM_OP_LOAD_DIR_TO_BRAM, act, 0);
            if (act == ACT_ALT && !s_sw[i].blocks && k == 0 && !announced(len)) {   /* I7: kept both */
                /* said once (swap speed, hardware 2026-11-24: the notice holds the screen ~3 s, and it came back at
                 * every relaunch of the game): ACTIVE.TXT's line 3 keeps it announced (active_refresh) */
                differs_notice(s_j.s.want_gid);
                if (s_nan < AN_MAX) s_an[s_nan++] = rec_id_of(len);
            }
            if (act != ACT_MAIN) {
                /* fix 3 (C2): with no list before, a card version that lost to BRAM's copy is listed (active_refresh),
                 * so the next archive treats it as this session's too */
                if (k == 0 && mode0 && s_ndl < OM_PGS_LIST && (rc = bram_list(&nb)) == 0 && list_find(E->rec, nb) >= 0) {
                    char cn[12];
                    memcpy(cn, E->rec + 0x10, 11);
                    cn[11] = 0;
                    if (om_bup_name_norm(cn, cn))
                        s_dl[s_ndl++] = om_pgs_rec_id(cn, om_be32(E->rec + 0x28), om_be32(E->rec + 0x2C),
                                                      om_crc32(0, E->rec + OM_BUP_HDR, len - OM_BUP_HDR));
                }
                continue;
            }
            if (k == 0) {                           /* a fill: BRAM lacks the record, or holds it torn */
                need += om_pgs_blocks(om_be32(E->rec + 0x2C), (uint32_t)E->rec[0x30] << 8 | E->rec[0x31]);
                continue;
            }
            if ((rc = om_bup_decode_hdr(E->rec, len, &bd)) != 0 || (rc = bwrite(&bd, E->rec + OM_BUP_HDR)) != 0)
                return fail(OM_OP_LOAD_DIR_TO_BRAM, rc, 0);
        }
        if (k == 0) {
            if (!need) return 0;
            if ((rc = om_bram_info_get(0, &inf)) != 0) return fail(OM_OP_CHECK_FITS, rc, 0);
            if (need > inf.freeblock) {             /* torn records' blocks are not counted as freed: conservative */
                om_pgs_fit_msg(m, sizeof m, need, inf.blocksize);
                set_notice(m);
                return 0;
            }
            say("SAVES: LOADING CARD COPIES");
        }
    }
    return 0;
}

static int reconcile(void)
{
    int rc;
    if ((rc = reconcile_load()) != 0) return rc;
    if ((rc = active_refresh()) != 0) return fail(OM_OP_WRITE_MARKER, rc, 0);
    return 0;
}

static uint32_t s_prev;                        /* the previous session's nonce (ACTIVE.TXT), 0 = none */

/* re-review "Nonce": the platform's nonce (launcher: FRC + latched HV counters), chained with the
 * previous session's nonce from ACTIVE.TXT, the set's fingerprint and a counter -- each session's nonce depends on every
 * earlier one, so two sessions repeat a nonce only if the whole chain does */
static uint32_t new_nonce(uint32_t fp)
{
    uint32_t v[4], n;
    v[0] = E->nonce ? E->nonce() : 0u;
    v[1] = s_prev;
    v[2] = fp;
    v[3] = ++s_ctr;
    n = om_crc32(0, v, sizeof v);
    return n ? n : 1u;
}

static int op_write_marker(const char *gid, uint8_t slot)
{
    char line[OM_GID_MAX + 24];
    om_bup_dir d;
    int n, rc;
    uint32_t nonce, fp;
    uint16_t cnt;
    if ((rc = bram_fp(&fp, &cnt)) != 0) return rc;  /* the marker is reserved: the fingerprint is the same after it */
    nonce = new_nonce(fp);
    if ((n = om_pgs_marker_fmt(line, sizeof line, gid, slot, nonce)) < 0) return n;
    memset(&d, 0, sizeof d);
    memcpy(d.name, OM_PGS_MARKER, 11);
    memcpy(d.comment, "VIBEMENU", 8);
    d.language = 1;                                 /* English (ST-162-R1 BupDir language) */
    d.datasize = (uint32_t)n;
    if ((rc = bwrite(&d, (const uint8_t *)line)) != 0) return rc;
    s_prev = nonce;
    if ((n = om_pgs_active_fmt(line, sizeof line, gid, slot, nonce, fp)) < 0) return n;
    memcpy(E->rec, line, (uint32_t)n);
    return active_write((uint32_t)n, 0);         /* + the versions BRAM holds now */
}

static int op_delete_marker(void)
{
    char nm[12];
    int rc;
    {   /* the general set just loaded -- its versions, for the next archive of _GENERAL (dl_load) */
        uint32_t n;
        E->rec[0] = '\n';
        if ((rc = active_build(1, 0, &n)) != 0) return rc;
        if ((rc = saves_dir()) != 0) return rc;
        txt_forget(OM_PGS_GENLIST);
        if ((rc = om_sdw_replace(F(OM_PGS_GENLIST), E->rec, n, E->scr, E->gap, 0)) != 0) return rc;
    }
    name12(nm, OM_PGS_MARKER);
    rc = bdelete(nm);
    if (rc && rc != OM_BUP_NOTFOUND) return rc;
    return unlink_both(OM_PGS_ACTIVE);
}

/* one op; ph: the phase to resume from (VERIFY only); forward: WRITE_MARKER writes want, else (undo) the marker */
static int run_op(const om_pgs_op *ops, uint32_t nops, uint32_t i, uint8_t ph, int forward)
{
    const om_pgs_op *o = &ops[i];
    const om_pgs_state *s = &s_j.s;
    switch (o->op) {
    case OM_OP_ARCHIVE_BRAM_TO:         return op_archive(o->dir);
    case OM_OP_VERIFY_DIR:              return op_verify(o->dir, ph, (uint8_t)i);
    default: break;
    }
    cd_home();                                      /* option A: only ARCHIVE and VERIFY start in a save folder */
    switch (o->op) {
    case OM_OP_QUARANTINE:              return op_quarantine(o->dir);
    case OM_OP_ROTATE_GENERAL:          return 0;   /* declares it: VERIFY_DIR of _GENERAL rotates (R5) */
    case OM_OP_CHECK_FITS:              return fits(ops + i, nops - i);
    case OM_OP_CLEAR_BRAM_EXCEPT_MARKER: return op_clear();
    case OM_OP_LOAD_DIR_TO_BRAM:        return op_load(o->dir);
    case OM_OP_LOAD_NEWER:              return i && ops[i - 1].op == OM_OP_LOAD_DIR_TO_BRAM ?
                                               op_load_newer(o->dir, ops[i - 1].dir) : OM_E_FIELD;
    case OM_OP_WRITE_MARKER:
        return forward ? op_write_marker(s->want_gid, s->want_slot) : op_write_marker(s->marker_gid, s->marker_slot);
    case OM_OP_DELETE_MARKER:           return op_delete_marker();
    }
    return OM_E_FIELD;
}

static int get_list(int undo, om_pgs_op *ops, uint32_t *n)
{
    return undo ? om_pgs_undo(&s_j.s, ops, OM_PGS_MAX_OPS, n) : om_pgs_plan(&s_j.s, ops, OM_PGS_MAX_OPS, n);
}

/* runs s_j's list (forward or undo) from op `from`, phase `ph`; the journal line of op `from` is written unless
 * `resumed` (it is already on the card). 0, or the failing rc with *at = its index.
 * Swap speed (hardware 2026-11-24, test_pgscost.c): a journal line is a replace of JOURNAL.TXT, ~8 path commands that
 * each walk the card from its root, so only the lines a resume needs are written -- the first op's (the plan, BRAM's
 * fingerprint, the generation), CLEAR's (from there on BRAM is not what the plan was made from: a resume checks R2's
 * coverage and runs the undo on a failure) and VERIFY's commit phase (op_verify). A resume from an earlier line runs
 * the ops after it again; each one is a no-op once done (pgsrun.h): ARCHIVE skips the files already written, VERIFY
 * reads them back again, CHECK_FITS only reads, and after CLEAR the plan is run again from CLEAR (the loads overwrite,
 * the marker is written again). An undo list writes its first line only: its CLEAR runs first again. */
static int run_list(int undo, uint32_t from, uint8_t ph, int resumed, uint32_t *at)
{
    om_pgs_op ops[OM_PGS_MAX_OPS];
    uint32_t n, i;
    int rc = get_list(undo, ops, &n);
    *at = from;
    if (rc) return rc;
    for (i = from; i < n; i++) {
        *at = i;
        s_sp.step = i + 1;
        s_sp.steps = n;
        strncpy(s_sp.stage, om_pgs_op_name(ops[i].op), sizeof s_sp.stage - 1);
        s_sp.stage[sizeof s_sp.stage - 1] = 0;
        s_sp.file[0] = 0;
        s_sp.file_done = s_sp.file_total = 0;
        sp_emit();
        if (i == from ? !resumed : !undo && ops[i].op == OM_OP_CLEAR_BRAM_EXCEPT_MARKER)
            if ((rc = j_put((uint8_t)i, 0, (uint8_t)undo)) != 0) return rc;
        if ((rc = run_op(ops, n, i, i == from ? ph : 0, !undo)) != 0)
            return rc;
    }
    return 0;
}

static uint32_t clear_index(void)
{
    om_pgs_op ops[OM_PGS_MAX_OPS];
    uint32_t n = 0, i;
    if (om_pgs_plan(&s_j.s, ops, OM_PGS_MAX_OPS, &n)) return 0;
    for (i = 0; i < n && ops[i].op != OM_OP_CLEAR_BRAM_EXCEPT_MARKER; i++) ;
    return i;
}

/* the forward plan's VERIFY_DIR (OM_PGS_MAX_OPS when it has none: a foreign plan) */
static uint32_t verify_index(void)
{
    om_pgs_op ops[OM_PGS_MAX_OPS];
    uint32_t n = 0, i;
    if (om_pgs_plan(&s_j.s, ops, OM_PGS_MAX_OPS, &n)) return OM_PGS_MAX_OPS;
    for (i = 0; i < n && ops[i].op != OM_OP_VERIFY_DIR; i++) ;
    return i < n ? i : OM_PGS_MAX_OPS;
}

static int op_at(int undo, uint32_t i)
{
    om_pgs_op ops[OM_PGS_MAX_OPS];
    uint32_t n = 0;
    if (get_list(undo, ops, &n)) return 0;
    return i < n ? ops[i].op : 0;
}

/* M6 Task 24 fix round 2: a completed forward swap that loaded the wanted slot stamps its folder (OM_PGS_STAMP), so a
 * later load of it is not a first load. Best effort: a missing stamp only means one more newer-copy comparison. */
static void stamp_loaded(void)
{
    om_pgs_op ops[OM_PGS_MAX_OPS];
    char d[OM_PGS_DIR], p[PATHMAX];
    uint32_t n, i;
    static const uint8_t t[4] = { 0, 0, 0, 0 };   /* written empty: a cut write leaves the same empty file */
    if (!s_j.s.want_pgs || s_j.s.foreign || om_pgs_plan(&s_j.s, ops, OM_PGS_MAX_OPS, &n)) return;
    if (om_pgs_slot_dir_po(d, s_j.s.want_gid, s_j.s.want_slot, s_j.s.want_po)) return;
    for (i = 0; i < n && !(ops[i].op == OM_OP_LOAD_DIR_TO_BRAM && !strcmp(ops[i].dir, d)); i++) ;
    if (i == n && n) return;                        /* n == 0: case 1, BRAM already holds this slot (loaded) */
    if (s_j.s.slot_exists && !s_j.s.first_load) return;   /* resolve_first found the stamp there (swap speed) */
    path(p, d, "", OM_PGS_STAMP);
    if (sd_exists(p) == 0)                          /* only its existence counts: a cut write still marks it */
        om_sdw_write_file(p, t, 0, 0, E->scr, E->gap, 0);
}

/* runs (or resumes) the swap in s_j; a failure at or after CLEAR is undone. OM_PGS_E_FOREIGN: a CLEAR found BRAM not
 * covered by the card (nothing was deleted); the caller quarantines it */
static int swap(int undo, uint32_t from, uint8_t ph, int resumed)
{
    uint32_t at;
    int rc = run_list(undo, from, ph, resumed, &at);
    if (!rc) {
        if (unlink_both(OM_PGS_JOURNAL)) return fail(OM_OP_COUNT, OM_E_FIELD, "SAVES: JOURNAL.TXT NOT DELETED");
        if (!undo) stamp_loaded();                  /* after the journal, so a resume never sees it move */
        return 0;
    }
    if (rc == OM_PGS_E_FOREIGN)
        return rc;
    fail(op_at(undo, at), rc, 0);
    if (undo)                                       /* a failed undo: its journal stays, the next launch retries */
        return R->rc;
    if (op_at(undo, at) == OM_OP_VERIFY_DIR && s_j.ph >= 1)
        return R->rc;                               /* mid-commit: the journal stays, the next launch finishes it */
    if (at < clear_index()) {                       /* BRAM untouched: the marker still says what it holds */
        unlink_both(OM_PGS_JOURNAL);
        return R->rc;
    }
    say("SAVE SWAP FAILED, UNDOING");
    {   /* the error screen names the forward op that failed (R->line describes it), not an undo op */
        uint8_t op = R->op;
        rc = run_list(1, 0, 0, 0, &at);
        if (rc == 0 && unlink_both(OM_PGS_JOURNAL) == 0)
            R->undone = 1;
        R->op = op;
        if (rc == OM_PGS_E_FOREIGN) return rc;      /* the undo's CLEAR found BRAM foreign: quarantine it */
    }
    return R->rc;
}

/* R2/R3: BRAM is not what Aether left -- it is quarantined, the general set is given back and the marker dropped
 * (the foreign plan, journaled like any swap). The notice names the quarantine folder. */
static int foreign(void)
{
    uint32_t q;
    int rc;
    char t[OM_PGS_NOTICE + 8];
    memset(&s_j, 0, sizeof s_j);
    s_bram_ok = 0;
    if ((rc = q_next(&q)) != 0) return rc;
    s_j.s.foreign = 1;
    s_j.s.qnum = (uint16_t)q;
    if ((rc = bram_fp(&s_j.fp, &s_j.fn)) != 0) return fail(OM_OP_COUNT, rc, 0);
    R->foreign = 1;
    say("CONSOLE SAVES CHANGED: KEEPING A COPY");   /* hardware 2026-11-24: clearer than "BRAM CHANGED OUTSIDE" */
    rc = swap(0, 0, 0, 0);
    if (rc == OM_PGS_E_FOREIGN)                     /* cannot happen: Q holds every readable record */
        return fail(OM_OP_QUARANTINE, rc, "SAVES: QUARANTINE DOES NOT COVER BRAM");
    if (rc) return rc;
    /* two 40-column rows (lseq.c splits at 40): what happened, then where the copy is */
    cat_i(cat(t, "CONSOLE SAVES CHANGED SINCE LAST TIME.  COPY KEPT ON THE CARD: _QUARANTINE/"), (int)q);
    set_notice(t);
    return 0;
}

/* Kai's product-only rule (pgsplan.h want_po/marker_po): "SAVES/<product>" is the game's folder while
 * "SAVES/<product>[<areas>]" is absent. Derived from the card at every plan and every resume -- never journaled -- so
 * a resume re-plans the same folders (Aether never creates the product-only name, and never creates the region
 * name while the product-only one decides). */
static int po_of(const char *gid, uint8_t *po)
{
    char d[OM_PGS_DIR], p[PATHMAX];
    int rc;
    *po = 0;
    if (!gid[0] || om_pgs_product_dir(d, gid)) return 0;
    path(p, d, "", 0);
    if ((rc = sd_exists(p)) <= 0) return rc;
    if (om_pgs_slot_dir(d, gid, 0)) return 0;
    path(p, d, "", 0);
    if ((rc = sd_exists(p)) < 0) return rc;
    *po = (uint8_t)!rc;
    return 0;
}

/* s->first_load -- the wanted slot folder exists (s->slot_exists) and holds no OM_PGS_STAMP */
static int resolve_first(om_pgs_state *s)
{
    char d[OM_PGS_DIR], p[PATHMAX];
    int rc;
    s->first_load = 0;
    if (!s->want_pgs || !s->slot_exists || om_pgs_slot_dir_po(d, s->want_gid, s->want_slot, s->want_po)) return 0;
    path(p, d, "", OM_PGS_STAMP);
    if ((rc = sd_exists(p)) < 0) return fail(OM_OP_COUNT, rc, "SAVES: SD CARD UNREADABLE");
    s->first_load = (uint8_t)!rc;
    return 0;
}

static int resolve_po(om_pgs_state *s)
{
    int rc = s->want_pgs ? po_of(s->want_gid, &s->want_po) : (s->want_po = 0);
    if (!rc && s->has_marker && s->want_pgs && !strcmp(s->marker_gid, s->want_gid))
        s->marker_po = s->want_po;                  /* the same game: the same folder rule (swap speed) */
    else if (!rc) rc = s->has_marker ? po_of(s->marker_gid, &s->marker_po) : (s->marker_po = 0);
    return rc ? fail(OM_OP_COUNT, rc, "SAVES: SD CARD UNREADABLE") : 0;
}

/* a resume: only while BRAM is what the journal expects (R2) */
static int resume(void)
{
    uint32_t fp;
    uint16_t cnt;
    int rc, post = s_j.undo || s_j.op >= clear_index();
    if (post)
        rc = covered();
    else if ((rc = bram_fp(&fp, &cnt)) == 0 && (fp != s_j.fp || cnt != s_j.fn))
        rc = OM_PGS_E_FOREIGN;
    else if (!rc && verify_index() < OM_PGS_MAX_OPS && (s_j.op < verify_index() || (s_j.op == verify_index() && s_j.ph == 0))) {
        /* the fingerprint sees names, dates and sizes only; a change of the bytes alone is
         * found by re-verifying the staged copy -- and handled as R2's mismatch, not as an error screen. An
         * archive a cut stopped is checked the same way (every file already in its generation is BRAM's, byte for
         * byte): on a mismatch BRAM is quarantined and the next plan takes a new generation. Swap speed: a journal
         * line at or before ARCHIVE (the first op's, run_list) may have been followed by the whole archive or a part
         * of it -- checked as a part */
        om_pgs_op ops[OM_PGS_MAX_OPS];
        uint32_t n, v = verify_index();
        char nd[OM_PGS_DIR];
        char cd[OM_PGS_DIR];
        uint32_t cg;
        if ((rc = get_list(0, ops, &n)) == 0 && (rc = arch_dir(ops[v].dir, nd)) == 0
            && (rc = dl_load(ops[v].dir)) == 0 && (rc = cur_of(ops[v].dir, cd, &cg)) == 0) {
            rc = verify_dir2(nd, cd, s_j.op < v && s_j.g);   /* g 0: nothing to write */
            if (rc == OM_E_CRC) rc = OM_PGS_E_FOREIGN;
        }
    }
    if (rc && rc != OM_PGS_E_FOREIGN)
        return fail(OM_OP_COUNT, rc, 0);
    if (rc == OM_PGS_E_FOREIGN && !post) {          /* BRAM untouched by Aether since: keep a copy, plan afresh */
        char qd[OM_PGS_DIR], t[48];
        uint32_t q;
        if ((rc = q_next(&q)) != 0 || (rc = om_pgs_q_dir(qd, q)) != 0 || (rc = op_quarantine(qd)) != 0)
            return fail(OM_OP_QUARANTINE, rc, 0);
        if ((rc = unlink_both(OM_PGS_JOURNAL)) != 0)
            return fail(OM_OP_COUNT, rc, "SAVES: JOURNAL.TXT NOT DELETED");
        cat_i(cat(t, "BRAM CHANGED: COPY IN _QUARANTINE/"), (int)q);
        set_notice(t);
        R->foreign = 1;
        return 0;
    }
    if (rc == OM_PGS_E_FOREIGN)
        return foreign();
    rc = swap(s_j.undo, s_j.op, s_j.ph, 1);
    return rc == OM_PGS_E_FOREIGN ? foreign() : rc;
}

static int run_all(const om_pgs_env *e, uint8_t saves, uint8_t slot, const char *gid, om_pgs_result *r)
{
    char p[PATHMAX], d[OM_PGS_DIR];
    om_pgs_op ops[OM_PGS_MAX_OPS];
    uint32_t len = 0, n;
    int rc, have_j, tries, idle = 0;
    line_t m, a;
    memset(&a, 0, sizeof a);
    s_prev = 0;
    s_saves_ok = 0;
    s_dot = 0;
    s_made[0] = 0;
    s_nw_ok = 0;
    txt_forget(0);
    s_bram_ok = 0;
    s_sc_ok = 0;
    cur_forget();
    s_torn_seen = s_torn_kept = 0;
    s_nann = 0;
    s_label[0] = 0;
    E = e;
    R = r;
    memset(r, 0, sizeof *r);
    s_rootn = sizeof ROOT - 1;
    s_cwd[0] = 0;
    if (e->rel) {                                   /* swap speed, part 2: names from /aether (above) */
        static const char vm[] = OM_CARD_DIR, probe[] = "LAUNCHER.BIN";
        om_yield();
        if (s_chdir(al(vm)) == 0) {
            om_yield();
            if (s_stat(al(probe), (s_stat_t *)s_st, (int)sizeof s_st - 4) >= 0) {
                s_rootn = 0;
                r->rel = 1;
            }
        }
        if (e->now_us && r->rel) {                  /* the timing: the probe (from /aether), then from the root */
            static const char abs_[] = OM_CARD_ROOT "LAUNCHER.BIN";
            uint32_t t1, t2;
            om_yield();                             /* outside the timed commands */
            t1 = e->now_us();
            s_stat(al(probe), (s_stat_t *)s_st, (int)sizeof s_st - 4);
            t2 = e->now_us();
            r->t_rel_us = t2 - t1;
            om_yield();
            t2 = e->now_us();
            s_stat(al(abs_), (s_stat_t *)s_st, (int)sizeof s_st - 4);
            r->t_root_us = e->now_us() - t2;
        }
    }
    rc = om_sdw_read_file(F(OM_PGS_JOURNAL), e->chk, OM_PGS_REC, &len);
    if (rc && rc != OM_SDW_NOFILE)
        return fail(OM_OP_COUNT, rc, "SAVES: JOURNAL.TXT UNREADABLE");
    have_j = rc == 0 && len > 0;      /* an empty JOURNAL.TXT: the first journal write was cut before any line (the
                                       * replace creates the path empty, sdwrite.h step 1), so no op had started */
    if (have_j && om_pgs_journal_parse((const char *)e->chk, len, &s_j))
        return fail(OM_OP_COUNT, OM_E_FIELD, "SAVES: JOURNAL.TXT BAD (SEE SPEC 5.17)");
    if (!have_j && !saves) {                        /* the gate (pgsrun.h) */
        rc = txt_read(OM_PGS_ACTIVE, &len);
        if (rc == OM_SDW_NOFILE) idle = 1;
        else if (rc) return fail(OM_OP_COUNT, rc, "SAVES: ACTIVE.TXT UNREADABLE");
    }
    if (idle && !e->backup) return 0;
    r->ran = 1;
    bram_dirty();
    if ((rc = om_bram_init(e->lib, e->work)) == OM_BUP_UNFORMAT) {
        /* an unformatted internal memory holds no save to back up or swap -- the launch goes
         * on with a notice; Aether never formats it (ST-203 §2.0 1): the user, or the game, does). A journal stays
         * for a later launch. */
        set_notice("CONSOLE SAVE MEMORY NOT FORMATTED - SAVES SKIPPED");
        return 0;
    }
    if (rc) {
        if (idle) {                                 /* the backup alone: a notice, never a stopped launch */
            om_bup_msg(r->bak_notice, OM_PGS_MSG, "BACKUP", rc);
            r->bak_rc = rc;
            return 0;
        }
        return fail(OM_OP_COUNT, rc, 0);
    }
    if (e->backup) {                                /* BRAM as the last session left it, before any change */
        om_pgs_bak b;
        say("BRAM BACKUP");
        om_pgs_backup_rel(r->rel);
        om_pgs_backup(e, e->date ? e->date : "", &b, r->bak_notice);
        om_pgs_backup_rel(0);
        bram_dirty();                               /* it listed into E->list */
        r->bak_written = b.written; r->bak_same = b.same; r->bak_records = b.records;
        r->bak_seq = b.seq; r->bak_rc = b.rc;
        if (idle) return 0;
    }
    /* R4: Kai holds a game's set in BRAM -- no swap at all, not even a resume: the journal
     * stays for a later launch, where R2 checks BRAM again */
    if (bram_has(OM_PGS_KAI_MARKER)) {
        r->paused = 1;
        set_notice("SAVES: KAI'S PSEUDO_BRAM IN BRAM, NO SWAP");
        return 0;
    }
    if (have_j) {
        r->resumed = 1;
        say("RESUMING SAVE SWAP");
        if ((rc = resolve_po(&s_j.s)) != 0 || (rc = resolve_first(&s_j.s)) != 0) return rc;
        if ((rc = resume()) != 0) return rc;
    }
    for (tries = 0; tries < 2; tries++) {
        if ((rc = read_marker(&m)) != 0 && rc != OM_E_FIELD) return fail(OM_OP_COUNT, rc, 0);
        if (!rc && (rc = read_active(&a)) != 0 && rc != OM_E_FIELD) return fail(OM_OP_COUNT, rc, 0);
        if (a.has) s_prev = a.nonce;
        /* R3: a marker counts only with its own ACTIVE.TXT; ACTIVE.TXT without a marker means the marker was lost */
        if (rc || (m.has && (!a.has || strcmp(m.gid, a.gid) || m.slot != a.slot || m.nonce != a.nonce))
            || (!m.has && a.has)) {
            if (tries) return fail(OM_OP_COUNT, OM_E_FIELD, "SAVES: MARKER STILL FOREIGN");
            if ((rc = foreign()) != 0) return rc;
            continue;
        }
        memset(&s_j, 0, sizeof s_j);
        s_j.s.has_marker = m.has;
        strcpy(s_j.s.marker_gid, m.gid);
        s_j.s.marker_slot = m.slot;
        s_j.s.want_slot = slot;
        s_j.s.want_pgs = (uint8_t)(saves && gid && !om_gameid_check(gid));
        if (s_j.s.want_pgs) strcpy(s_j.s.want_gid, gid);
        s_j.s.share = saves == 2;
        if ((rc = resolve_po(&s_j.s)) != 0) return rc;
        if (s_j.s.want_pgs && om_pgs_slot_dir_po(d, gid, slot, s_j.s.want_po) == 0) {
            path(p, d, "", 0);
            if ((rc = sd_exists(p)) < 0) return fail(OM_OP_COUNT, rc, "SAVES: SD CARD UNREADABLE");
            s_j.s.slot_exists = (uint8_t)rc;
        }
        if ((rc = resolve_first(&s_j.s)) != 0) return rc;
        if ((rc = om_pgs_plan(&s_j.s, ops, OM_PGS_MAX_OPS, &n)) != 0)
            return fail(OM_OP_COUNT, rc, "SAVES: NO PLAN (SLOT/GAME ID)");
        if (!n) {
            if (s_j.s.want_pgs && s_j.s.has_marker) {    /* case 1: BRAM already holds this slot */
                if ((rc = reconcile()) != 0) return rc;
                stamp_loaded();                          /* a stamp a cut may have missed */
            }
            return 0;
        }
        if ((rc = bram_fp(&s_j.fp, &s_j.fn)) != 0) return fail(OM_OP_COUNT, rc, 0);
        if ((rc = plan_gen(ops, n)) != 0)          /* before the first journal line */
            return fail(OM_OP_ARCHIVE_BRAM_TO, rc, rc == OM_E_FULL ? "SAVES FULL: 99999 GENERATIONS" : 0);
        say("SAVE SWAP - KEEP THE POWER ON");      /* ST-203 §3.0 warning (6) */
        rc = swap(0, 0, 0, 0);
        if (rc != OM_PGS_E_FOREIGN) return rc;
        if (tries) return fail(OM_OP_CLEAR_BRAM_EXCEPT_MARKER, rc, "SAVES: BRAM NOT ON THE CARD");
        if ((rc = foreign()) != 0) return rc;
    }
    return 0;
}

int om_pgs_run(const om_pgs_env *e, uint8_t saves, uint8_t slot, const char *gid, om_pgs_result *r)
{
    int rc;
    uint32_t t0 = e->now_us ? e->now_us() : 0;
    memset(&s_sp, 0, sizeof s_sp);
    rc = run_all(e, saves, slot, gid, r);
    if (e->now_us) r->t_ms = (e->now_us() - t0) / 1000u;
    r->torn = (uint8_t)(s_torn_seen > 255u ? 255u : s_torn_seen);
    r->torn_kept = (uint8_t)(s_torn_kept > 255u ? 255u : s_torn_kept);
    if (s_torn_seen && !r->notice[0]) {             /* a save in BRAM was torn */
        char t[OM_PGS_NOTICE + 16];
        cat(cat(cat(t, s_label[0] ? s_label : "SAVES"), ": DAMAGED SAVE IN CONSOLE MEMORY, "),
            s_torn_kept ? "KEPT THE CARD COPY" : "NO COPY ON THE CARD");
        set_notice(t);
    }
    return rc;
}
