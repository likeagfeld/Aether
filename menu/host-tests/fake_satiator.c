/* menu/host-tests/fake_satiator.c -- host stand-in for libsatiator over a directory ($FAKE_SD_ROOT).
 * Enforces the real API's limits: <= 2048 B per s_read/s_write (satiator.c:186-202), and 4-byte-aligned buffers and
 * names, because buffer_xfer walks them as uint32_t* (satiator.c:114-125). A violation aborts the test.
 * s_rename/s_unlink/s_mkdir with FatFs semantics (rename onto an existing name is -FR_EXIST, a missing
 * name -FR_NO_FILE), a call log, an open-file count, a counted fault hook, a stored-byte corruption and a "power cut"
 * (every call fails until fake_reboot). s_rename's names are not alignment-checked: satiator.c copies both into its
 * own aligned buffer (UPSTREAM.md). */
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "satiator.h"

#define MAXFD 8
static FILE *s_fds[MAXFD];
static DIR *s_dir;
static char s_dirpath[1100];
int fake_reads, fake_max_read;
/* test hook for menu/app/satfs.c's sat_list "a timeout is never
 * a legitimate end-of-readdir result" branch. This fake replaces libsatiator at the s_* layer (below
 * where exec_cmd/CDB_REG_HIRQ live), so it has no HIRQ register to simulate a real data-phase timeout
 * with (menu/host-tests/test_sattimeout.c does that, against the real satiator.c, for s_read/s_stat in
 * isolation) -- this hook instead makes the NEXT s_stat(NULL,...) call return SAT_ETIMEDOUT directly,
 * one-shot (consumed on use), so menu/host-tests/test_card.c can drive that same return value through the
 * real menu/app/satfs.c code it already links. */
int fake_force_timeout;

/* M6 Task 21 hooks. fake_log: one token per call ("O" open for write, "o" open for read, "W<n>", "S", "K<off>" seek,
 * "C", "N" rename, "U" unlink, "M" mkdir); reads and stats are not logged. fake_fail_*: the (skip+1)-th call of op
 * (one of the letters above; 'O' = an open for write) and the next times-1 calls of it return rc. fake_cut_*: once
 * the (skip+1)-th call of op has completed, every call returns -FR_NOT_READY until fake_reboot (the power cut).
 * 'M' (s_mkdir of a folder that does not exist yet) can be failed too (M6 Task 23 fix round 2).
 * fake_corrupt_at: the stored byte at that file offset is flipped by any write that covers it. 'T' = a named s_stat.
 * fake_rename_overwrites (Task 21 fix round 1, review I1): rename onto an existing name replaces it instead of the
 * FatFs -FR_EXIST refusal, the other firmware behavior sdwrite.c must survive (FW163's is unverified). */
char fake_log[8192];
int fake_rename_overwrites;
int fake_open_now, fake_open_max;
/* big-card stutter (2026-11-23): every call that makes the Satiator resolve a path -- s_open, a named s_stat,
 * s_opendir -- walks the card's directories from the root (O(root names) on the card), so a test can count them */
int fake_paths;
/* the save swap's cost (test_pgscost.c, hardware 2026-11-24: NiGHTS sat minutes in the swap): every command by kind
 * (fake_paths keeps its meaning: an open, a named stat, an opendir) */
int fake_c_open, fake_c_stat, fake_c_dirent, fake_c_opendir, fake_c_write, fake_c_rename, fake_c_unlink, fake_c_close;
int fake_c_chdir;
long fake_rbytes, fake_wbytes;
int fake_trace;                                    /* 1: every path command printed (test_pgscost PGSCOST_TRACE) */
#define TRACE(k, f) do { if (fake_trace) printf("        %s %s\n", k, f); } while (0)
/* boot-animation gaps (2026-11-23): a virtual clock of the Satiator's time -- a read costs its length at the diag
 * page's 296 KB/s (progress.md, FW163 hardware), every other command FAKE_CMD_US (unmeasured; a model) */
#define FAKE_READ_BPS 296000u
#define FAKE_CMD_US   1000u
uint64_t fake_us;
int fake_fail_op, fake_fail_skip, fake_fail_times, fake_fail_rc;
int fake_cut_op, fake_cut_skip, fake_dead;
long fake_corrupt_at = -1;
/* M6 Task 23: one counter over every mutating call of this fake and of fake_bram.c (an open for write, a write, a
 * rename, an unlink, a mkdir; a BRAM write or delete), so a test can cut the power or fail a call at any step of a save
 * swap. fake_mutate() is called before each: fake_mut_fail's call returns fake_mut_fail_rc instead of running;
 * fake_mut_cut's call completes (fake_bram tears it) and then every call fails until fake_reboot. */
int fake_mut_n, fake_mut_cut = -1, fake_mut_fail = -1, fake_mut_fail_rc;
int fake_mutate(void)
{
    int k = ++fake_mut_n;
    if (k == fake_mut_fail) return fake_mut_fail_rc;
    return k == fake_mut_cut ? 1 : 0;
}
#define MUT(dead_after) do { int _m = fake_mutate(); if (_m < 0) return _m; if (_m) dead_after = 1; } while (0)

static void logf_(const char *tok, long v)
{
    size_t n = strlen(fake_log);
    if (n + 24 < sizeof fake_log)
        snprintf(fake_log + n, sizeof fake_log - n, v < 0 ? "%s " : "%s%ld ", tok, v);
}

static int inject(int op)
{
    if (fake_fail_op != op) return 0;
    if (fake_fail_skip > 0) { fake_fail_skip--; return 0; }
    if (fake_fail_times > 0) { fake_fail_times--; return fake_fail_rc; }
    return 0;
}

static void cut_after(int op)
{
    if (fake_cut_op != op) return;
    if (fake_cut_skip > 0) { fake_cut_skip--; return; }
    fake_dead = 1;
    fake_cut_op = 0;
}


/* the global R1 property, checked by the fake card itself. With fake_r1_on, every
 * delete (s_unlink), overwrite (s_open FA_CREATE_ALWAYS of an existing file) and rename-over of a RECORD FILE -- a
 * "*.BUP" that decodes, or a "*.BKP" generation -- first looks for a copy of each record it holds ELSEWHERE ON THE CARD
 * (every .BUP and every other .BKP under $FAKE_SD_ROOT; console memory does not count): the same name, comment and
 * language, and a later date, or the same date and the same data -- or, between two generations, a later generation
 * (a later capture). A record with no such copy is a violation: fake_r1_violations counts them, the first are printed. */
/* M6 Task 24b (user decision 2026-11-22 (a)/(c)): the ADD-ONLY property. With fake_addonly, the launcher may never
 * delete, overwrite (an open for write of an existing file), move (rename away) or rename over a "*.BUP" or "*.BKP"
 * at all: each is an add-only violation (fake_addonly_violations, the first printed), whatever copies exist. Only
 * Tidy (fake_tidy_ok, set around its calls) may delete one, and only a byte-identical copy (the same name, comment,
 * language, date and data -- no later date counts) with another such copy left on the card: else a violation too. */
#include "bupfile.h"
int fake_r1_on, fake_r1_violations;
int fake_addonly, fake_addonly_violations, fake_tidy_ok;

typedef struct { char name[12], comment[11]; uint8_t lang; uint32_t date, size; const uint8_t *data; } r1rec;

static uint8_t *r1_load(const char *p, long *n)
{
    FILE *f = fopen(p, "rb");
    uint8_t *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)*n + 1);
    if (fread(b, 1, (size_t)*n, f) != (size_t)*n) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static int r1_ext(const char *p, const char *e)
{
    size_t n = strlen(p);
    return n > 4 && p[n - 4] == '.' && (p[n - 3] | 32) == e[0] && (p[n - 2] | 32) == e[1] && (p[n - 1] | 32) == e[2];
}

/* the records of a file: a decodable .BUP (one), a whole .BKP (its count); -1 for neither */
static int r1_recs(const char *p, const uint8_t *b, long n, r1rec *out, int cap)
{
    om_bup_dir d;
    if (r1_ext(p, "bup")) {
        if (!b || om_bup_decode_hdr(b, (uint32_t)n, &d)) return -1;
        memcpy(out[0].name, d.name, 12); memcpy(out[0].comment, d.comment, 11); out[0].lang = d.language;
        out[0].date = d.date; out[0].size = d.datasize; out[0].data = b + 64;
        return 1;
    }
    if (r1_ext(p, "bkp") && b && n >= 64 && !memcmp(b, "OMSATBKP", 8)) {
        uint32_t cnt = ((uint32_t)b[10] << 8) | b[11], off = 64 + cnt * 32, k;
        int m = 0;
        for (k = 0; k < cnt && m < cap; k++) {
            if (off + 64 > (uint32_t)n || om_bup_decode_hdr(b + off, (uint32_t)n - off, &d)) return -1;   /* partial */
            memcpy(out[m].name, d.name, 12); memcpy(out[m].comment, d.comment, 11); out[m].lang = d.language;
            out[m].date = d.date; out[m].size = d.datasize; out[m].data = b + off + 64;
            m++;
            off += 64 + d.datasize + ((4 - (d.datasize & 3)) & 3);
        }
        return m;
    }
    return -1;
}

static unsigned r1_seq(const char *p)
{
    const char *b = strrchr(p, '/');
    unsigned v = 0;
    int k;
    b = b ? b + 1 : p;
    for (k = 0; k < 5 && b[k] >= '0' && b[k] <= '9'; k++) v = v * 10u + (unsigned)(b[k] - '0');
    return k == 5 ? v : 0;
}

static int r1_found;
static int r1_strict;
static void r1_walk(const char *dir, const char *self, const r1rec *r, int self_bkp, unsigned self_seq)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return;
    while (!r1_found && (e = readdir(d))) {
        char p[1400];
        struct stat sb;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (stat(p, &sb)) continue;
        if (S_ISDIR(sb.st_mode)) { r1_walk(p, self, r, self_bkp, self_seq); continue; }
        if (!strcmp(p, self) || !(r1_ext(p, "bup") || r1_ext(p, "bkp"))) continue;
        {
            static r1rec c[600];
            long n = 0;
            uint8_t *b = r1_load(p, &n);
            int m = r1_recs(p, b, n, c, 600), k;
            int later_gen = self_bkp && r1_ext(p, "bkp") && r1_seq(p) > self_seq;
            for (k = 0; k < m && !r1_found; k++) {
                if (memcmp(c[k].name, r->name, 12) || memcmp(c[k].comment, r->comment, 11) || c[k].lang != r->lang) continue;
                if ((!r1_strict && (c[k].date > r->date || (later_gen && c[k].date == r->date))) ||
                    (c[k].date == r->date && c[k].size == r->size && !memcmp(c[k].data, r->data, r->size)))
                    r1_found = 1;
            }
            free(b);
        }
    }
    closedir(d);
}

static void r1_check(const char *p, const char *what)
{
    static r1rec recs[600];
    long n = 0;
    uint8_t *b;
    int m, k;
    const char *root = getenv("FAKE_SD_ROOT");
    if (!(r1_ext(p, "bup") || r1_ext(p, "bkp"))) return;
    if (fake_addonly && !(fake_tidy_ok && !strcmp(what, "unlink"))) {
        if (fake_addonly_violations++ < 8) printf("ADD-ONLY VIOLATION: %s %s\n", what, p);
        return;
    }
    if (!fake_r1_on && !fake_tidy_ok) return;
    b = r1_load(p, &n);
    m = r1_recs(p, b, n, recs, 600);
    r1_strict = fake_tidy_ok;
    if (fake_tidy_ok && m < 0 && r1_ext(p, "bup") && b && n >= 64) {      /* Tidy: a record file it cannot decode */
        if (fake_addonly_violations++ < 8) printf("TIDY VIOLATION: unlink of a non-record %s\n", p);
    }
    for (k = 0; k < m; k++) {
        r1_found = 0;
        r1_walk(root ? root : ".", p, &recs[k], r1_ext(p, "bkp"), r1_seq(p));
        if (!r1_found && fake_tidy_ok) {
            if (fake_addonly_violations++ < 8)
                printf("TIDY VIOLATION: unlink %s has no byte-identical copy of %.11s left\n", p, recs[k].name);
        } else if (!r1_found) {
            if (fake_r1_violations++ < 8)
                printf("R1 VIOLATION: %s %s drops the last card copy of %.11s (date %u, %u B)\n", what, p,
                       recs[k].name, recs[k].date, recs[k].size);
        }
    }
    free(b);
}

/* Card Manager finding: real FatFs reports a name under a folder that does not exist as FR_NO_PATH,
 * not FR_NO_FILE (f_stat / f_open / f_unlink / f_rename walk the path first). fake_nopath = 1 gives that answer for a
 * missing parent folder; 0 (the default) keeps FR_NO_FILE for every missing name, as before. */
int fake_nopath;
static int missing(const char *p)
{
    char d[1400];
    struct stat sb;
    char *sl;
    if (!fake_nopath) return -FR_NO_FILE;
    snprintf(d, sizeof d, "%s", p);
    if ((sl = strrchr(d, '/')) != NULL) *sl = 0;
    return stat(d, &sb) || !S_ISDIR(sb.st_mode) ? -FR_NO_PATH : -FR_NO_FILE;
}

static void aligned(const void *p, const char *what)
{
    if ((uintptr_t)p & 3) { printf("FAIL fake_satiator: unaligned %s %p\n", what, p); exit(3); }
}

/* the working folder (real libsatiator: s_chdir is the firmware's FatFs f_chdir, satiator.c; a name without a leading
 * '/' is resolved from it, as FatFs does with FF_FS_RPATH -- the launcher's s_emulate("out.desc") after s_chdir(dir)
 * relies on that on hardware). Card-relative, "/" or "/a/b"; a reboot (a power cut) starts again at the root. */
static char s_cwd[600] = "/";
int fake_chdir_fails;                              /* 1: s_chdir answers -FR_NO_PATH (the fallback's test) */
/* the review of option A: the fake_chdir_at-th s_chdir (counted in fake_c_chdir) answers fake_chdir_rc instead --
 * after moving when fake_chdir_moves (a timeout that took effect) */
int fake_chdir_at, fake_chdir_rc, fake_chdir_moves;
/* s_opendir(""): FatFs opens the working folder (0, the default); 1: the firmware refuses it (-FR_INVALID_NAME);
 * 2: it lists the card's root instead -- the save swap must notice both and keep absolute names */
int fake_empty_dir;
long fake_chdir_deep;                              /* chdirs that took into a folder below /aether */
/* the save swap's path cost (test_pgscost.c, fake_cost_on): every path a command resolves -- its components, and the
 * folder entries the card scans to find them: each component's folder's entry count, the root's counted as
 * fake_root_names when that is larger (the user's card root holds ~700 names) */
int fake_cost_on, fake_root_names;
long fake_comps, fake_scanned;

static long dir_entries(const char *abs)
{
    DIR *d = opendir(abs);
    struct dirent *e;
    long n = 0;
    if (!d) return 0;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
    closedir(d);
    return n;
}

/* the card-relative absolute form of p ("/a/b"), from the working folder for a relative name */
static void card_path(char *out, size_t cap, const char *p)
{
    if (p[0] == '/') snprintf(out, cap, "%s", p);
    else if (!strcmp(s_cwd, "/")) snprintf(out, cap, "/%s", p);
    else snprintf(out, cap, "%s/%s", s_cwd, p);
}

static void cost(const char *p)
{
    const char *r = getenv("FAKE_SD_ROOT");
    char at[1400], comp[300];
    const char *c = p;
    int root = p[0] == '/';
    if (!fake_cost_on) return;
    if (root || !strcmp(s_cwd, "/")) snprintf(at, sizeof at, "%s", r ? r : ".");
    else snprintf(at, sizeof at, "%s%s", r ? r : ".", s_cwd);
    while (*c) {
        const char *e;
        size_t k;
        long n;
        while (*c == '/') c++;
        if (!*c) break;
        for (e = c; *e && *e != '/'; e++) ;
        k = (size_t)(e - c);
        if (k >= sizeof comp) k = sizeof comp - 1;
        memcpy(comp, c, k);
        comp[k] = 0;
        n = dir_entries(at);
        if (root && n < fake_root_names) n = fake_root_names;   /* the user's card root */
        root = 0;
        fake_comps++;
        fake_scanned += n;
        if (strlen(at) + k + 2 < sizeof at) { strcat(at, "/"); strcat(at, comp); }
        c = e;
    }
}

static void full(char *out, const char *p)
{
    const char *r = getenv("FAKE_SD_ROOT");
    char cp[900];
    cost(p);
    card_path(cp, sizeof cp, p);
    snprintf(out, 1100, "%s/%s", r ? r : ".", cp + 1);
}

int s_chdir(const char *f)
{
    char cp[600], abs[1100];
    struct stat sb;
    aligned(f, "chdir name");
    if (fake_dead) return -FR_NOT_READY;
    fake_paths++;
    fake_c_chdir++;
    TRACE("chdir", f);
    fake_us += FAKE_CMD_US;
    if (fake_chdir_fails) return -FR_NO_PATH;
    if (fake_chdir_at && fake_c_chdir == fake_chdir_at && !fake_chdir_moves) return fake_chdir_rc;
    full(abs, f);
    if (stat(abs, &sb) || !S_ISDIR(sb.st_mode)) return -FR_NO_PATH;
    card_path(cp, sizeof cp, f);
    {   /* no trailing '/', but the root's */
        size_t n = strlen(cp);
        while (n > 1 && cp[n - 1] == '/') cp[--n] = 0;
    }
    strcpy(s_cwd, cp);
    if (!strncmp(cp, "/aether/", 8)) fake_chdir_deep++;
    if (fake_chdir_at && fake_c_chdir == fake_chdir_at) return fake_chdir_rc;   /* moved, and said it failed */
    return 0;
}
/* the test's view of the working folder */
const char *fake_cwd(void) { return s_cwd; }

int s_open(const char *f, int flags)
{
    char p[1100];
    int i, r, cut = 0;
    aligned(f, "filename");
    if (fake_dead) return -FR_NOT_READY;
    fake_paths++;
    fake_c_open++;
    TRACE((flags & FA_WRITE) ? "open-w" : "open", f);
    fake_us += FAKE_CMD_US;
    full(p, f);
    if ((flags & FA_WRITE) && (r = inject('O')) != 0) return r;
    if (flags & FA_WRITE) MUT(cut);
    for (i = 0; i < MAXFD && s_fds[i]; i++) ;
    if (i == MAXFD) return -FR_TOO_MANY_OPEN_FILES;
    if ((flags & FA_WRITE) && (flags & FA_CREATE_ALWAYS)) {
        struct stat sb;
        if (!stat(p, &sb) && (sb.st_size || fake_addonly)) r1_check(p, "overwrite");
    } else if ((flags & FA_WRITE) && fake_addonly) {
        struct stat sb;
        if (!stat(p, &sb)) r1_check(p, "write into");
    }
    s_fds[i] = fopen(p, (flags & FA_WRITE) ? ((flags & FA_CREATE_ALWAYS) ? "wb" : "r+b") : "rb");
    if (!s_fds[i] && (flags & FA_OPEN_ALWAYS)) s_fds[i] = fopen(p, "w+b");
    if (!s_fds[i]) return missing(p);
    if (++fake_open_now > fake_open_max) fake_open_max = fake_open_now;
    logf_((flags & FA_WRITE) ? "O" : "o", -1);
    if (flags & FA_WRITE) cut_after('O');
    if (cut) fake_dead = 1;
    return i;
}

/* the power cut is over: every handle is dropped (what was already written stays on the "card") */
void fake_reboot(void)
{
    int i;
    for (i = 0; i < MAXFD; i++)
        if (s_fds[i]) { fclose(s_fds[i]); s_fds[i] = NULL; }
    fake_open_now = 0;
    fake_dead = 0;
    strcpy(s_cwd, "/");                            /* a fresh boot: the firmware starts at the root */
}

int s_rename(const char *old, const char *new)
{
    char a[1100], b[1100];
    struct stat sb;
    int r, cut = 0;
    if (fake_dead) return -FR_NOT_READY;
    fake_c_rename++;
    TRACE("rename", old);
    if ((r = inject('N')) != 0) return r;
    MUT(cut);
    full(a, old);
    full(b, new);
    if (stat(a, &sb)) return missing(a);
    if (fake_addonly && !S_ISDIR(sb.st_mode)) r1_check(a, "move");
    if (!stat(b, &sb)) {
        if (fake_addonly) r1_check(b, "rename over");
        if (!fake_rename_overwrites) return -FR_EXIST;
        if (!fake_addonly) r1_check(b, "rename over");
        remove(b);
    }
    if (rename(a, b)) return -FR_DENIED;
    logf_("N", -1);
    cut_after('N');
    if (cut) fake_dead = 1;
    return 0;
}

int s_unlink(const char *f)
{
    char p[1100];
    struct stat sb;
    int r, cut = 0;
    aligned(f, "unlink name");
    if (fake_dead) return -FR_NOT_READY;
    fake_c_unlink++;
    TRACE("unlink", f);
    if ((r = inject('U')) != 0) return r;
    MUT(cut);
    full(p, f);
    if (stat(p, &sb)) return missing(p);
    if (!S_ISDIR(sb.st_mode)) r1_check(p, "unlink");
    if (S_ISDIR(sb.st_mode) ? rmdir(p) : remove(p)) return -FR_DENIED;   /* FatFs: a non-empty directory is denied */
    logf_("U", -1);
    cut_after('U');
    if (cut) fake_dead = 1;
    return 0;
}

int fake_mkdir_calls;                              /* every s_mkdir, made or not (the import's cost test) */
int s_mkdir(const char *f)
{
    char p[1100];
    struct stat sb;
    int cut = 0, r;
    fake_mkdir_calls++;
    TRACE("mkdir", f);
    aligned(f, "mkdir name");
    if (fake_dead) return -FR_NOT_READY;
    full(p, f);
    if (!stat(p, &sb)) return -FR_EXIST;
    if ((r = inject('M')) != 0) return r;          /* mkdir can fail */
    MUT(cut);
    if (mkdir(p, 0755)) return -FR_NO_PATH;
    logf_("M", -1);
    if (cut) fake_dead = 1;
    return 0;
}

/* review-final-m3 I2/I4: one-shot fault hooks (consumed on use, like fake_force_timeout) for sd.c's write path --
 * the next s_sync / s_close returns this FRESULT-style negative code; fake_syncs counts s_sync calls. */
int fake_fail_sync, fake_fail_close, fake_syncs;

int s_close(int fd)
{
    int f = fake_fail_close;
    if (fake_dead) return -FR_NOT_READY;
    if (fd < 0 || fd >= MAXFD || !s_fds[fd]) return -FR_INVALID_OBJECT;
    fake_us += FAKE_CMD_US;
    fake_c_close++;
    if (!f) f = inject('C');
    fclose(s_fds[fd]);
    s_fds[fd] = NULL;
    fake_open_now--;
    fake_fail_close = 0;
    logf_("C", -1);
    cut_after('C');
    return f ? f : 0;
}

int s_sync(int fd)
{
    int f = fake_fail_sync;
    if (fake_dead) return -FR_NOT_READY;
    if (fd < 0 || fd >= MAXFD || !s_fds[fd]) return -FR_INVALID_OBJECT;
    fake_syncs++;
    fake_fail_sync = 0;
    if (!f) f = inject('S');
    if (f) return f;
    fflush(s_fds[fd]);
    logf_("S", -1);
    cut_after('S');
    return (int)ftell(s_fds[fd]);
}

int s_seek(int fd, int off, int whence)
{
    if (fake_dead) return -FR_NOT_READY;
    fake_us += FAKE_CMD_US;
    if (whence == C_SEEK_SET) logf_("K", off);
    if (fseek(s_fds[fd], off, whence == C_SEEK_SET ? SEEK_SET : whence == C_SEEK_CUR ? SEEK_CUR : SEEK_END)) return -FR_INT_ERR;
    return (int)ftell(s_fds[fd]);
}

int s_read(int fd, void *buf, int len)
{
    if (fake_dead) return -FR_NOT_READY;
    if (len > S_MAXBUF || len < 0) return -FR_INVALID_PARAMETER;
    aligned(buf, "read buffer");
    fake_reads++;
    fake_us += (uint64_t)len * 1000000u / FAKE_READ_BPS;
    if (len > fake_max_read) fake_max_read = len;
    {
        int got = (int)fread(buf, 1, (size_t)len, s_fds[fd]);
        fake_rbytes += got;                         /* what came back (the Satiator sends only that) */
        return got;
    }
}

int s_write(int fd, const void *buf, int len)
{
    long at;
    int r, cut = 0;
    if (fake_dead) return -FR_NOT_READY;
    if (len > S_MAXBUF || len < 0) return -FR_INVALID_PARAMETER;
    aligned(buf, "write buffer");
    if ((r = inject('W')) != 0) return r;
    MUT(cut);
    fake_c_write++;
    fake_wbytes += len;
    at = ftell(s_fds[fd]);
    logf_("W", len);
    if (fake_corrupt_at >= at && fake_corrupt_at < at + len) {
        static uint8_t tmp[S_MAXBUF];
        memcpy(tmp, buf, (size_t)len);
        tmp[fake_corrupt_at - at] ^= 0x5A;
        buf = tmp;
    }
    r = (int)fwrite(buf, 1, (size_t)len, s_fds[fd]);
    cut_after('W');
    if (cut) fake_dead = 1;
    return r;
}

int s_opendir(const char *f)
{
    aligned(f, "dirname");
    if (fake_dead) return -FR_NOT_READY;
    fake_paths++;
    fake_c_opendir++;
    TRACE("opendir", f);
    fake_us += FAKE_CMD_US;
    if (!f[0] && fake_empty_dir == 1) return -FR_INVALID_NAME;      /* a firmware that refuses "" */
    if (!f[0] && fake_empty_dir == 2) {                              /* one that opens the root for it */
        const char *r = getenv("FAKE_SD_ROOT");
        snprintf(s_dirpath, sizeof s_dirpath, "%s", r ? r : ".");
    } else
        full(s_dirpath, f);
    if (s_dir) closedir(s_dir);
    s_dir = opendir(s_dirpath);
    return s_dir ? 0 : -FR_NO_PATH;
}

int s_stat(const char *f, s_stat_t *st, int sz)
{
    struct stat sb;
    char p[1400];
    const char *name;
    size_t n;
    aligned(st, "stat buffer");
    if (fake_dead) return -FR_NOT_READY;
    fake_us += FAKE_CMD_US;
    if (!f) {
        struct dirent *d;
        fake_c_dirent++;
        if (fake_force_timeout) { fake_force_timeout = 0; return SAT_ETIMEDOUT; }
        d = s_dir ? readdir(s_dir) : NULL;
        if (!d) return -FR_NO_FILE;
        snprintf(p, sizeof p, "%s/%s", s_dirpath, d->d_name);
        name = d->d_name;
    } else {
        int r;
        aligned(f, "stat name");
        fake_paths++;
        fake_c_stat++;
        TRACE("stat", f);
        if ((r = inject('T')) != 0) return r;
        full(p, f);
        name = strrchr(f, '/') ? strrchr(f, '/') + 1 : f;
    }
    if (stat(p, &sb)) return f ? missing(p) : -FR_NO_FILE;
    st->size = (uint32_t)sb.st_size;
    st->date = st->time = 0;
    st->attrib = S_ISDIR(sb.st_mode) ? AM_DIR : 0;
    n = strlen(name);
    if ((int)n > sz - 9) n = (size_t)(sz - 9);
    memcpy(st->name, name, n);
    return (int)n;
}
