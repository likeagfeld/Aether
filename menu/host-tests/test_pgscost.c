/* menu/host-tests/test_pgscost.c -- the save swap's Satiator cost per launch (hardware 2026-11-24: with Saves on, on the
 * user's 700-game exFAT card, NiGHTS and other games sat for a very long time in the save swap before booting).
 *
 * Every path command (an open, a named stat, an opendir, a mkdir, a rename, an unlink) makes the Satiator resolve the
 * path from the card's root, which costs O(root names) on a big card (perf-bigcard.md: the 698-name root is ~106 KB of
 * exFAT entries; one root walk L >= ~27 ms, plausibly ~180 ms, never measured). So the launch cost is dominated by the
 * number of path commands. This test runs the real pgsrun.c/pgsbak.c (with the always-on backup, as lseq.c does) over a
 * card shaped like the user's -- a general set of 24 records in BRAM, a Kai LATEST import of them in _GENERAL, NiGHTS
 * and Guardian Heroes imported from Rings, ~100 other imported game folders -- and counts every Satiator command for
 * each launch of a realistic sequence, then checks the counts against fixed budgets. BSD-3-Clause. */
#pragma GCC diagnostic ignored "-Wformat-truncation"
#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "th.h"
#include "om_fmt.h"
#include "satiator.h"
#include "bupfile.h"
#include "pgsrun.h"
#include "pgsbak.h"

extern int fake_paths, fake_mkdir_calls, fake_reads, fake_nopath, fake_r1_on, fake_addonly, fake_addonly_violations,
    fake_r1_violations;
extern int fake_c_open, fake_c_stat, fake_c_dirent, fake_c_opendir, fake_c_write, fake_c_rename, fake_c_unlink,
    fake_c_close;
extern long fake_rbytes, fake_wbytes;
extern int fake_bram_lists, fake_bram_reads, fake_empty_dir;
extern uint64_t fake_us;
extern int fake_bram_calls, fake_trace, fake_cost_on, fake_root_names, fake_chdir_fails;
extern long fake_comps, fake_scanned;
int s_chdir(const char *f);
extern int fake_dead, fake_mut_n, fake_mut_cut, fake_c_chdir, fake_chdir_at, fake_chdir_rc, fake_chdir_moves;
int fake_bram_count(void);
const om_bup_dir *fake_bram_dir(int i);
const uint8_t *fake_bram_data(int i, int *broken);
void fake_reboot(void);
void fake_bram_reset(void);
void fake_bram_put(const char *name, const uint8_t *data, uint32_t size, uint32_t date);
void fake_bram_del(const char *name);
int fake_bram_find(const char *name);

static char root[] = "/tmp/omsat_pgscost_XXXXXX";
static uint32_t arena32[OM_PGS_ARENA / 4 + 4];
static om_pgs_env env;
static int s_gaps;
static void gap(void) { s_gaps++; }
/* the per-step breakdown (PGSCOST_STEPS=1): the path commands between two progress calls of a new step */
static char s_stage[16];
static int s_stage_p0, s_verbose;
static long s_stage_s0;
extern long fake_scanned;
static uint32_t s_step = 0;
static int paths_now(void)
{
    return fake_c_open + fake_c_stat + fake_c_opendir + fake_mkdir_calls + 2 * fake_c_rename + fake_c_unlink
           + fake_c_chdir;
}
static void prog(const om_pgs_prog *p)
{
    if (p->step == s_step) return;
    if (s_verbose && s_stage[0])
        printf("      step %-12s %4d path cmds %6ld scanned\n", s_stage, paths_now() - s_stage_p0, fake_scanned - s_stage_s0);
    s_step = p->step;
    s_stage_s0 = fake_scanned;
    strncpy(s_stage, p->stage, sizeof s_stage - 1);
    s_stage_p0 = paths_now();
}
static void say(const char *t)
{
    if (s_verbose) printf("      [%s] at %d path cmds\n", t, paths_now());
}
static uint32_t s_nonce;
static uint32_t nonce(void) { return 0xC0570000u + ++s_nonce; }

#define NIGHTS "MK-81020@U"
#define GHERO  "GS-9031@U"

static uint8_t *content(uint32_t size, uint32_t seed)
{
    static uint8_t b[4][40000];
    static int k;
    uint8_t *o = b[k++ & 3];
    uint32_t i;
    for (i = 0; i < size; i++) o[i] = (uint8_t)((i * 31u + seed * 7u) ^ (i >> 5));
    return o;
}

static void mkdirs_abs(char *p)
{
    char *s;
    for (s = p + strlen(root) + 1; *s; s++)
        if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    mkdir(p, 0755);
}

static void sd_bup(const char *dir, const char *name, uint32_t size, uint32_t seed, uint32_t date)
{
    char p[700];
    uint8_t h[OM_BUP_HDR];
    om_bup_dir d;
    FILE *f;
    memset(&d, 0, sizeof d);
    strncpy(d.name, name, 11);
    memcpy(d.comment, "TEST", 5);
    d.language = 1;
    d.date = date;
    d.datasize = size;
    om_bup_encode_hdr(&d, h);
    snprintf(p, sizeof p, "%s/aether/SAVES/%s", root, dir);
    mkdirs_abs(p);
    snprintf(p, sizeof p, "%s/aether/SAVES/%s/%s.BUP", root, dir, name);
    f = fopen(p, "wb");
    fwrite(h, 1, sizeof h, f);
    fwrite(content(size, seed), 1, size, f);
    fclose(f);
}

static void sd_text(const char *rel, const char *text)
{
    char p[600];
    FILE *f;
    snprintf(p, sizeof p, "%s/aether/SAVES/%s", root, rel);
    f = fopen(p, "wb");
    fputs(text, f);
    fclose(f);
}

/* the general set: s_ngen records -- 24 of 150..1,530 B (about 20 KB of a 32 KB BRAM: the user's Rings _BACKUP holds
 * 20), or the worst case, 120 one-block records (a BRAM full of small saves) */
static int s_ngen = 24;
static void gen_name(char out[12], int i) { snprintf(out, 12, "GAME%03d_SAV", i); }
static uint32_t gen_size(int i) { return s_ngen > 24 ? 40u + (uint32_t)(i % 7) : 150u + (uint32_t)i * 60u; }

static void card(void)
{
    char cmd[400], nm[12], line[4096];
    int i, n = 0;
    snprintf(cmd, sizeof cmd, "rm -rf %s/aether", root);
    if (system(cmd)) {}
    snprintf(cmd, sizeof cmd, "%s/aether/SAVES", root);
    mkdirs_abs(cmd);
    snprintf(cmd, sizeof cmd, "%s/Some Game (USA)", root);
    mkdirs_abs(cmd);
    {   /* the launcher's own file: pgsrun.c checks /aether with it (env.rel) */
        FILE *f;
        snprintf(cmd, sizeof cmd, "%s/aether/LAUNCHER.BIN", root);
        if ((f = fopen(cmd, "wb"))) { fputs("OMSATLNC", f); fclose(f); }
    }
    fake_reboot();
    fake_bram_reset();
    for (i = 0; i < s_ngen; i++) {
        gen_name(nm, i);
        fake_bram_put(nm, content(gen_size(i), (uint32_t)i), gen_size(i), 1000u + (uint32_t)i);
    }
    fake_bram_put("NIGHTS_DATA", content(1200, 500), 1200, 2000);   /* NiGHTS played from the general set before */
    /* Kai's SAVES/LATEST imported into _GENERAL (pgsbak.c, 24c): 5 in 6 of the records as BRAM holds them, the rest
     * older versions (alternates) */
    n = snprintf(line, sizeof line, "\nI");
    for (i = 0; i < s_ngen; i++) {
        uint32_t seed = i % 6 != 5 ? (uint32_t)i : 300u + (uint32_t)i, date = i % 6 != 5 ? 1000u + (uint32_t)i : 900u;
        gen_name(nm, i);
        sd_bup("_GENERAL", nm, gen_size(i), seed, date);
        {
            char nn[12];
            memset(nn, 0, 12);
            memcpy(nn, nm, 11);
            n += snprintf(line + n, sizeof line - (size_t)n, " %08X",
                          om_pgs_rec_id(nn, date, gen_size(i), om_crc32(0, content(gen_size(i), seed), gen_size(i))));
        }
    }
    snprintf(line + n, sizeof line - (size_t)n, "\n");
    sd_text("GENLIST.TXT", line);
    /* Rings' imports: NiGHTS (its own older copy) and Guardian Heroes, and ~100 other games' folders */
    sd_bup("MK-81020[U]", "NIGHTS_DATA", 1200, 400, 1500);
    sd_bup("GS-9031[U]", "GHEROES_001", 1000, 600, 1600);
    sd_bup("GS-9031[U]", "GHEROES_CFG", 300, 601, 1601);
    for (i = 0; i < 100; i++) {
        char d[32];
        snprintf(d, sizeof d, "T-%05dG[J]", 10000 + i);
        sd_bup(d, "OTHER__SAVE", 500, 700u + (uint32_t)i, 1700);
    }
}

typedef struct { int paths, open, stat, opendir, dirent, mkdir, rename, unlink, write, close, gaps, bram; long rb, wb;
                 long comps, scanned; int blists, breads; } cost;
static void zero(void)
{
    fake_paths = fake_mkdir_calls = fake_reads = 0;
    fake_c_open = fake_c_stat = fake_c_dirent = fake_c_opendir = fake_c_write = fake_c_rename = fake_c_unlink = 0;
    fake_c_close = fake_c_chdir = 0;
    fake_rbytes = fake_wbytes = 0;
    fake_bram_calls = fake_bram_lists = fake_bram_reads = 0;
    fake_comps = fake_scanned = 0;
    s_gaps = 0;
}
static cost take(void)
{
    cost c;
    c.open = fake_c_open; c.stat = fake_c_stat; c.opendir = fake_c_opendir; c.dirent = fake_c_dirent;
    c.mkdir = fake_mkdir_calls; c.rename = fake_c_rename; c.unlink = fake_c_unlink; c.write = fake_c_write;
    c.close = fake_c_close; c.gaps = s_gaps; c.bram = fake_bram_calls; c.rb = fake_rbytes; c.wb = fake_wbytes;
    /* every command that resolves a path from the root: a rename resolves two */
    c.paths = c.open + c.stat + c.opendir + c.mkdir + 2 * c.rename + c.unlink + fake_c_chdir;
    c.blists = fake_bram_lists;
    c.breads = fake_bram_reads;
    c.comps = fake_comps;
    c.scanned = fake_scanned;
    return c;
}

/* seconds on the user's card: a path's lookup costs the folder entries the card scans for it (fake_satiator.c: each
 * component's folder, the root counted as its ~700 names), L ms per 700 entries -- one root walk (27 = the boot scan's
 * lower bound, 180 = the 296 KB/s plausibility); 1 ms per command, 296 KB/s reads, one V-blank (16.7 ms) per write gap,
 * 3 ms per backup-library call (all unmeasured but the read rate: a model, as perf-bigcard.md's) */
#define ROOT_NAMES 700
static double secs(const cost *c, double L)
{
    return (c->scanned * L / ROOT_NAMES + (c->paths + c->dirent + c->write + c->close) * 1.0 + c->rb / 296.0
            + c->gaps * 16.7 + c->bram * 3.0) / 1000.0;
}
static uint8_t s_rel_want = 1;                       /* env.rel for this sequence; r.rel checked against it */
static uint8_t s_quiet, s_rel_any;                    /* the cut sweep: no lines, r.rel not checked */

static cost launch(const char *what, uint8_t saves, const char *gid, int *rc_out)
{
    om_pgs_result r;
    cost c;
    int rc;
    static uint32_t game[16];
    strcpy((char *)game, "/Some Game (USA)");
    s_chdir((const char *)game);                     /* lseq.c step 2: the launch starts in the game's folder */
    zero();
    fake_trace = getenv("PGSCOST_TRACE") && strstr(what, getenv("PGSCOST_TRACE")) != NULL;
    s_step = 0; s_stage[0] = 0; s_stage_p0 = 0;
    rc = om_pgs_run(&env, saves, 0, gid, &r);
    c = take();
    if (!s_rel_any && !fake_dead) CHECK_INT(r.rel, s_rel_want);
    s_chdir((const char *)game);                     /* lseq.c save_swap puts it back */
    if (s_verbose && s_stage[0]) printf("      step %-12s %4d path cmds (to the end)\n", s_stage, paths_now() - s_stage_p0);
    if (!s_quiet) printf("  %-47s path cmds %4d (open %3d stat %3d dir %3d mkdir %3d ren %2d unl %2d)  components %4ld  scanned %6ld"
           "  read %6ld B  wrote %6ld B  ~%5.1f s (L 27) %6.1f s (L 180)%s%s\n",
           what, c.paths, c.open, c.stat, c.opendir, c.mkdir, c.rename, c.unlink, c.comps, c.scanned, c.rb, c.wb,
           secs(&c, 27), secs(&c, 180), r.notice[0] ? "  notice: " : "", r.notice);
    if (rc_out) *rc_out = rc;
    return c;
}

static void play(const char *name, uint32_t size, uint32_t seed, uint32_t date)
{   /* the game saved: its record has a new version in BRAM */
    fake_bram_del(name);
    fake_bram_put(name, content(size, seed), size, date);
}

/* a launch that moves a general set of n records costs O(n) path commands, never O(n^2): each record is written at
 * most once (open, verify open, a rename: 4) and read back a few times (VERIFY, the carry, CHECK_FITS) */
static int linear(const cost *c, int n) { return c->paths <= 11 * n + 120; }

/* the user's sequence on the 24-record card; out[] = each named launch's cost */
#define NSEQ 9
static const char *const k_seq[NSEQ] = {
    "1 NiGHTS, first launch (general set)", "3 NiGHTS again, nothing changed", "4 NiGHTS again, the game saved",
    "5 Guardian Heroes (NiGHTS changed: archive)", "6 NiGHTS (GH unchanged: no archive)",
    "10 NiGHTS again after 10 generations, unchanged", "11 saves off: the general set back",
    "13 saves off again, nothing changed", "14 NiGHTS from the general set, unchanged" };
static void sequence(cost out[NSEQ])
{
    cost c;
    int rc, k, i = 0;
    card();
    out[i++] = c = launch(k_seq[0], 1, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    CHECK(linear(&c, s_ngen));
    c = launch("2 NiGHTS again (the backup takes the new set)", 1, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    CHECK(c.paths <= 22);
    CHECK(c.rename <= 1 && c.unlink == 0);            /* the backup's one new file, nothing else */
    out[i++] = c = launch(k_seq[1], 1, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    CHECK(c.paths <= 21);                             /* the common case: a handful of reads, not one write */
    CHECK_INT(c.write, 0);
    CHECK_INT(c.mkdir, 0);
    play("NIGHTS_DATA", 1200, 501, 2100);
    out[i++] = c = launch(k_seq[2], 1, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    CHECK(c.paths <= 28);                             /* ACTIVE.TXT takes the new version */
    out[i++] = c = launch(k_seq[3], 1, GHERO, &rc);
    CHECK_INT(rc, 0);
    CHECK(c.paths <= 90);
    out[i++] = c = launch(k_seq[4], 1, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    CHECK(c.paths <= 55);
    launch("7 NiGHTS again (the backup takes the new set)", 1, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    c = launch("8 NiGHTS again, nothing changed", 1, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    CHECK(c.paths <= 21);
    CHECK_INT(c.write, 0);
    for (k = 0; k < 8; k++) {                         /* a history: eight more swaps, NiGHTS saved each time */
        cost a, b;
        play("NIGHTS_DATA", 1200, 510u + (uint32_t)k, 2200u + (uint32_t)k);
        a = launch("  ... GH (NiGHTS archived)", 1, GHERO, &rc);
        CHECK_INT(rc, 0);
        b = launch("  ... NiGHTS", 1, NIGHTS, &rc);
        CHECK_INT(rc, 0);
        CHECK(a.paths <= 92 && b.paths <= 56);        /* the history costs nothing per launch */
        if (env.rel && !fake_chdir_fails) CHECK(a.scanned <= 12000 && b.scanned <= 7000);
    }
    launch("9 NiGHTS again", 1, NIGHTS, &rc);
    out[i++] = c = launch(k_seq[5], 1, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    CHECK(c.paths <= 21);
    CHECK_INT(c.write, 0);
    out[i++] = c = launch(k_seq[6], 0, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    CHECK(c.paths <= 100);
    c = launch("12 saves off again (the backup takes it)", 0, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    out[i++] = c = launch(k_seq[7], 0, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    CHECK(c.paths <= 9);
    CHECK_INT(c.write, 0);
    out[i++] = c = launch(k_seq[8], 1, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    CHECK(c.paths <= 80);
    CHECK_INT(fake_addonly_violations, 0);
    CHECK_INT(fake_r1_violations, 0);
}

/* the card as text -- every file name under aether/SAVES, and the size and CRC of each but ACTIVE.TXT and the
 * backups (they hold the session nonce, which differs from run to run) -- for "both modes leave the same card" */
static char *card_text(void)
{
    char cmd[600], *buf = (char *)malloc(1 << 20);
    FILE *f;
    size_t n;
    snprintf(cmd, sizeof cmd, "cd %s/aether/SAVES && find . -type f | LC_ALL=C sort && find . -type f ! -name ACTIVE.TXT"
             " ! -name '*.BKP' | LC_ALL=C sort | xargs -d '\n' cksum", root);
    f = popen(cmd, "r");
    n = f ? fread(buf, 1, (1 << 20) - 1, f) : 0;
    if (f) pclose(f);
    buf[n] = 0;
    return buf;
}

/* BRAM as text: each non-marker record's name, size and data CRC, and whether it is torn */
static char *bram_text(void)
{
    static char b[16384];
    int i, n = 0;
    b[0] = 0;
    for (i = 0; i < fake_bram_count(); i++) {
        const om_bup_dir *d = fake_bram_dir(i);
        int br;
        const uint8_t *x = fake_bram_data(i, &br);
        if (!strncmp(d->name, OM_PGS_MARKER, 11)) { n += snprintf(b + n, sizeof b - (size_t)n, "MARKER\n"); continue; }
        n += snprintf(b + n, sizeof b - (size_t)n, "%.11s %u %08x%s\n", d->name, d->datasize,
                      om_crc32(0, x, d->datasize), br ? " TORN" : "");
    }
    return b;
}

/* the review of 3b511db: a power cut in one naming mode, the resume in the other. The state before: NiGHTS loaded from
 * the general set and saved since; the launch: Guardian Heroes (an archive of NiGHTS, a new generation). A cut after
 * each mutating call k of that launch in mode a, a reboot, the same launch in mode b: it ends as the clean run does
 * (BRAM the same, every check of the fake card -- add-only, R1 -- clean) */
static void cut_resume(int a_rel, int b_rel)
{
    char clean[16384];
    int k, K, rc, bad = 0;
    s_quiet = 1;
    s_rel_any = 1;
    card();
    env.rel = 1;
    launch("x", 1, NIGHTS, &rc);
    play("NIGHTS_DATA", 1200, 777, 3000);
    env.rel = (uint8_t)a_rel;
    k = fake_mut_n;
    launch("x", 1, GHERO, &rc);
    CHECK_INT(rc, 0);
    K = fake_mut_n - k;
    strcpy(clean, bram_text());
    for (k = 1; k <= K; k++) {
        card();
        env.rel = 1;
        launch("x", 1, NIGHTS, &rc);
        play("NIGHTS_DATA", 1200, 777, 3000);
        env.rel = (uint8_t)a_rel;
        fake_mut_cut = fake_mut_n + k;
        launch("x", 1, GHERO, &rc);
        fake_mut_cut = -1;
        fake_reboot();
        env.rel = (uint8_t)b_rel;
        launch("x", 1, GHERO, &rc);
        if (rc || strcmp(clean, bram_text())) {
            if (!bad++) printf("FAIL cut %d (of %d), %s then %s: rc %d\n%s---\n%s", k, K, a_rel ? "relative" : "absolute",
                               b_rel ? "relative" : "absolute", rc, clean, bram_text());
        }
    }
    CHECK_INT(bad, 0);
    CHECK_INT(fake_addonly_violations, 0);
    CHECK_INT(fake_r1_violations, 0);
    printf("test_pgscost: a cut after each of %d mutating calls %s, the resume %s: %d differ\n", K,
           a_rel ? "relative" : "absolute", b_rel ? "relative" : "absolute", bad);
    s_quiet = 0;
    s_rel_any = 0;
}

/* the review of option A: a chdir that fails in the middle of a run -- each chdir of the first launch from the general set
 * (the deepest one) in turn answers "no such folder", a timeout that did not move, or a timeout that moved anyway.
 * The run goes on with absolute names (or stays where it was) and leaves the very card and BRAM the clean run does */
static void chdir_faults(void)
{
    static const int rcs[3] = { -FR_NO_PATH, SAT_ETIMEDOUT, SAT_ETIMEDOUT };
    char *clean_c, clean_b[16384];
    int k, K, m, rc, bad = 0, runs = 0;
    s_quiet = 1;
    s_rel_any = 1;
    env.rel = 1;
    card();
    s_nonce = 0;
    fake_c_chdir = 0;
    launch("x", 1, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    K = fake_c_chdir - 1;                             /* launch() zeroes it before the run; - its own restore */
    clean_c = card_text();
    strcpy(clean_b, bram_text());
    for (m = 0; m < 3; m++)
        for (k = 1; k <= K; k++) {
            char *t;
            card();
            s_nonce = 0;
            fake_chdir_at = k;                        /* counted from the run's start (launch() zeroes it) */
            fake_chdir_rc = rcs[m];
            fake_chdir_moves = m == 2;
            launch("x", 1, NIGHTS, &rc);
            fake_chdir_at = 0;
            t = card_text();
            runs++;
            if (rc || strcmp(t, clean_c) || strcmp(clean_b, bram_text())) {
                if (!bad++) printf("FAIL chdir %d fails (rc %d, moved %d): rc %d\n", k, rcs[m], m == 2, rc);
            }
            free(t);
        }
    free(clean_c);
    CHECK(K >= 4);                                    /* home, the save folder, home at the journal, ... */
    CHECK_INT(bad, 0);
    CHECK_INT(fake_addonly_violations, 0);
    CHECK_INT(fake_r1_violations, 0);
    printf("test_pgscost: each of %d chdirs failing three ways: %d runs, %d differ\n", K, runs, bad);
    s_quiet = 0;
    s_rel_any = 0;
}

/* the hardware timing hook (pgsrun.h env.now_us): the run, a stat from the root and one from /aether, timed */
static uint32_t fake_now(void) { return (uint32_t)fake_us; }
static void timing(void)
{
    om_pgs_result r;
    static uint32_t g[16];
    card();
    strcpy((char *)g, "/Some Game (USA)");
    s_chdir((const char *)g);
    env.rel = 1;
    env.now_us = fake_now;
    CHECK_INT(om_pgs_run(&env, 1, 0, NIGHTS, &r), 0);
    env.now_us = 0;
    s_chdir((const char *)g);
    printf("test_pgscost: timed: run %u ms, a stat from the root %u us, from /aether %u us (fake clock)\n", r.t_ms,
           r.t_root_us, r.t_rel_us);
    CHECK(r.t_ms > 0 && r.t_root_us > 0 && r.t_rel_us > 0);
}

int main(void)
{
    cost c, rel[NSEQ], abs_[NSEQ], fb[NSEQ], flat[NSEQ], nodot[2][NSEQ];
    char *t_rel, *t_abs;
    int rc, i;
    if (!mkdtemp(root)) { printf("FAIL mkdtemp\n"); return 1; }
    setenv("FAKE_SD_ROOT", root, 1);
    om_pgs_arena_split(&env, (uint8_t *)arena32);
    env.gap = gap;
    env.prog = prog;
    env.say = say;
    s_verbose = getenv("PGSCOST_STEPS") != NULL;
    env.nonce = nonce;
    env.backup = 1;                                  /* lseq.c: the always-on BRAM backup */
    env.date = "";
    fake_nopath = 1;
    fake_r1_on = 1;
    fake_addonly = 1;
    fake_cost_on = 1;
    fake_root_names = ROOT_NAMES;                    /* the user's card root */

    printf("test_pgscost: absolute names (no env.rel), a card like the user's (24 general records)\n");
    env.rel = 0; s_rel_want = 0; s_nonce = 0;
    sequence(abs_);
    t_abs = card_text();
    printf("test_pgscost: the same launches named from /aether (env.rel, the launcher's mode)\n");
    env.rel = 1; s_rel_want = 1; s_nonce = 0;
    sequence(rel);
    t_rel = card_text();
    CHECK(!strcmp(t_abs, t_rel));                     /* the same card either way */
    printf("test_pgscost: the same, option A off (no chdir into a save folder)\n");
    env.deep = 255; s_nonce = 0;
    sequence(flat);
    env.deep = 0;
    CHECK(!strcmp(t_abs, card_text()));
    for (i = 1; i <= 2; i++) {                        /* opendir("") refused, or answering with the card root */
        printf("test_pgscost: the same, opendir(\"\") %s: the swap keeps the absolute name\n",
               i == 1 ? "refused" : "listing the root instead");
        fake_empty_dir = i; s_nonce = 0;
        sequence(nodot[i - 1]);
        fake_empty_dir = 0;
        CHECK(!strcmp(t_abs, card_text()));
    }
    printf("test_pgscost: env.rel with a chdir that fails: absolute names, as without it\n");
    fake_chdir_fails = 1; s_rel_want = 0; s_nonce = 0;
    sequence(fb);
    fake_chdir_fails = 0;
    CHECK(!strcmp(t_abs, card_text()));
    printf("test_pgscost: BRAM library calls per launch (listings, record reads, all)\n");
    for (i = 0; i < NSEQ; i++)
        printf("  %-47s lists %4d  reads %5d  all %5d\n", k_seq[i], rel[i].blists, rel[i].breads, rel[i].bram);
    CHECK(rel[0].bram <= 200 && rel[0].blists <= 6);   /* was 273 calls, 34 listings, before the listing was kept */
    CHECK(rel[8].bram <= 120);                         /* was 141 */
    for (i = 0; i < NSEQ; i++) CHECK(rel[i].blists <= 6);
    printf("test_pgscost: summary (path commands; folder entries scanned and seconds at L 27..180 ms per root walk: absolute -> from /aether -> + option A)\n");
    for (i = 0; i < NSEQ; i++) {
        printf("  %-47s %4d cmds  scanned %6ld -> %6ld -> %6ld  ~%5.1f..%5.1f s -> %4.1f..%5.1f s -> %4.1f..%5.1f s\n",
               k_seq[i], rel[i].paths, abs_[i].scanned, flat[i].scanned, rel[i].scanned, secs(&abs_[i], 27),
               secs(&abs_[i], 180), secs(&flat[i], 27), secs(&flat[i], 180), secs(&rel[i], 27), secs(&rel[i], 180));
        CHECK_INT(flat[i].paths, abs_[i].paths + 2);                /* + the chdir and the check's stat */
        CHECK(rel[i].scanned <= flat[i].scanned + 400);           /* option A never costs more than a chdir */
        CHECK(rel[i].scanned <= nodot[0][i].scanned && rel[i].scanned <= nodot[1][i].scanned);
        CHECK(nodot[0][i].paths <= rel[i].paths + 1 && nodot[1][i].paths <= rel[i].paths + 1);   /* the try, once */
        CHECK_INT(fb[i].scanned, abs_[i].scanned);
        CHECK(rel[i].scanned * 3 < abs_[i].scanned);              /* the gain: under a third of the entries */
    }

    timing();
    chdir_faults();
    cut_resume(1, 0);
    cut_resume(0, 1);
    cut_resume(1, 1);

    s_ngen = 120;                                     /* the worst case: a BRAM full of small saves */
    s_rel_want = 1;
    printf("test_pgscost: the worst case, %d general records (env.rel)\n", s_ngen);
    card();
    c = launch("W1 NiGHTS, first launch (general set)", 1, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    CHECK(linear(&c, s_ngen));
    launch("W2 NiGHTS again (the backup takes the new set)", 1, NIGHTS, &rc);
    c = launch("W3 NiGHTS again, nothing changed", 1, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    CHECK(c.paths <= 21);
    CHECK_INT(c.write, 0);
    c = launch("W4 saves off: the general set back", 0, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    CHECK(linear(&c, s_ngen));
    c = launch("W5 NiGHTS from the general set, unchanged", 1, NIGHTS, &rc);
    CHECK_INT(rc, 0);
    CHECK(linear(&c, s_ngen));
    CHECK_INT(fake_addonly_violations, 0);
    CHECK_INT(fake_r1_violations, 0);
    {
        char cmd[400];
        snprintf(cmd, sizeof cmd, "rm -rf %s", root);
        if (system(cmd)) {}
    }
    return th_done("test_pgscost");
}
