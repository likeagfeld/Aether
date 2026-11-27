#define _GNU_SOURCE                                  /* strcasestr */
/* menu/host-tests/test_pgsrun.c -- the per-game-save executor (menu/launcher/pgsrun.c, SPEC §5.17) over
 * fake_bram.c and fake_satiator.c. Each scenario runs once clean (its expected end state, checked field by field), then
 * again with a power cut after EVERY mutating call k = 0..K (K = the clean run's count) followed by a reboot and the
 * next launch, and with a failed call at every k. Invariants: after a cut, every record the user had is still intact
 * in BRAM or on the card; the next launch ends in exactly the clean end state; after a failed call BRAM holds the old
 * set or the new one, never a mix, and the next launch converges.
 *
 * task-23-review.md C1, I1-I4: the reviewer's probe (probe_c1, the RED for C1); a FOREIGN
 * session -- another menu between the cut and the next launch, five kinds -- after every cut of every scenario; Kai's
 * and Rings' reserved records in BRAM throughout; a stale marker copy; a newer SD copy than BRAM's; two marker losses
 * in a row; repeated launches with no marker; Kai's PSEUDO_BRAM. The invariant everywhere (the "universe"): every
 * record that existed in BRAM or anywhere on the card before a launch still exists afterwards at its newest date
 * (byte for byte), in BRAM or in any .BUP on the card (quarantine and Kai's folders included). BSD-3-Clause. */
/* host-only paths: a d_name (<= 255) into a bounded buffer is truncated, never overflowed */
#pragma GCC diagnostic ignored "-Wformat-truncation"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"   /* scen.nojournal: 0 unless named */
#include <dirent.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "th.h"
#include "om_fmt.h"
#include "satiator.h"
#include "bupfile.h"
#include "pgsrun.h"
#include "pgsbak.h"
#include "pgsbak.h"
/* 1: the launcher's mode -- names from /aether and option A forced on (pgsrun.h env.rel, env.deep); 0: the fallback,
 * absolute names (test_pgsrun_abs.c builds this file so) */
#ifndef PGS_REL
#define PGS_REL 1
#endif

extern int fake_r1_on, fake_r1_violations, fake_addonly, fake_addonly_violations, fake_tidy_ok, fake_nopath;
extern int fake_dead, fake_mut_n, fake_mut_cut, fake_mut_fail, fake_mut_fail_rc;
extern int fake_bram_inits, fake_bram_extra, fake_bram_unformat;
extern int fake_fail_op, fake_fail_skip, fake_fail_times, fake_fail_rc;
void fake_reboot(void);
void fake_bram_reset(void);
void fake_bram_put(const char *name, const uint8_t *data, uint32_t size, uint32_t date);
void fake_bram_del(const char *name);
int fake_bram_count(void);
const om_bup_dir *fake_bram_dir(int i);
const uint8_t *fake_bram_data(int i, int *broken);
int fake_bram_find(const char *name);

static char root[] = "/tmp/omsat_pgs_XXXXXX";
static uint32_t arena32[OM_PGS_ARENA / 4 + 4];
static om_pgs_env env;
static char said[64];
static void say(const char *s) { strncpy(said, s, sizeof said - 1); }
static uint32_t s_nonce;
static uint32_t nonce(void) { return 0x5EED0000u + ++s_nonce; }

/* --- the card and BRAM as text, for exact comparisons ------------------------------------------------------------ */
typedef struct { char *s; size_t n, cap; } sb;
static void sb_add(sb *b, const char *t)
{
    size_t k = strlen(t);
    if (b->n + k + 1 > b->cap) { b->cap = (b->n + k + 1) * 2; b->s = (char *)realloc(b->s, b->cap); }
    memcpy(b->s + b->n, t, k + 1);
    b->n += k;
}
static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void first_two(char *out, const uint8_t *d, size_t n)
{   /* "<gid> <slot>" of a marker or ACTIVE.TXT line: the nonce and fp change with every session */
    size_t i, sp = 0;
    for (i = 0; i < n && i < 40 && d[i] != '\n'; i++) {
        if (d[i] == ' ' && ++sp == 2) break;
        out[i] = (char)d[i];
    }
    out[i] = 0;
}

static void walk(const char *abs, const char *rel, char **lines, int *n)
{
    DIR *d = opendir(abs);
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d))) {
        char a[700], r[700], *l;
        struct stat st;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(a, sizeof a, "%s/%s", abs, e->d_name);
        snprintf(r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", e->d_name);
        stat(a, &st);
        if (S_ISDIR(st.st_mode)) {
            if (strncmp(r, "_QUARANTINE", 11)) {        /* quarantine folders are numbered by when they happen */
                l = (char *)malloc(strlen(r) + 3);
                sprintf(l, "%s/", r);
                lines[(*n)++] = l;
            }
            walk(a, r, lines, n);
        } else {
            FILE *f = fopen(a, "rb");
            static uint8_t buf[70000];
            size_t k = fread(buf, 1, sizeof buf, f);
            fclose(f);
            l = (char *)malloc(strlen(r) + 64);
            if (!strcmp(r, "ACTIVE.TXT")) {
                char t[48];
                first_two(t, buf, k);
                sprintf(l, "ACTIVE.TXT:%s", t);
            } else if (!strncmp(r, "_QUARANTINE/", 12) && strchr(r + 12, '/'))
                sprintf(l, "_QUARANTINE/*/%s:%zu:%08x", strchr(r + 12, '/') + 1, k, om_crc32(0, buf, (uint32_t)k));
            else
                sprintf(l, "%s:%zu:%08x", r, k, om_crc32(0, buf, (uint32_t)k));
            lines[(*n)++] = l;
        }
    }
    closedir(d);
}

static char *state(void)
{   /* "B name:size:crc[!]" per BRAM record (the marker as "<gid> <slot>"), then every path under SAVES/, sorted */
    static char *lines[4096];
    char t[128], abs[300];
    int n = 0, i;
    sb b = { 0, 0, 0 };
    sb_add(&b, "");
    for (i = 0; i < fake_bram_count(); i++) {
        int br;
        const om_bup_dir *d = fake_bram_dir(i);
        const uint8_t *p = fake_bram_data(i, &br);
        char nm[12];
        om_bup_name_norm(nm, d->name);
        if (!strcmp(nm, OM_PGS_MARKER)) {
            char m[48];
            first_two(m, p, d->datasize);
            snprintf(t, sizeof t, "B %s:%s%s", nm, m, br ? "!" : "");
        } else
            snprintf(t, sizeof t, "B %s:%u:%08x%s", nm, d->datasize, om_crc32(0, p, d->datasize), br ? "!" : "");
        lines[n++] = strdup(t);
    }
    snprintf(abs, sizeof abs, "%s/aether/SAVES", root);
    walk(abs, "", lines, &n);
    qsort(lines, (size_t)n, sizeof lines[0], cmp_str);
    for (i = 0; i < n; i++) { sb_add(&b, lines[i]); sb_add(&b, "\n"); free(lines[i]); }
    return b.s;
}

static int sd_exists(const char *rel)
{
    char p[600];
    struct stat st;
    snprintf(p, sizeof p, "%s/aether/SAVES/%s", root, rel);
    return !stat(p, &st);
}

/* M6 Task 24b: the reader rule on the host -- an independent copy of savegen.h's: <dir>'s complete generations
 * (G<sss>/G<nnnnn> holding DONE.TXT), newest first */
static int gens_of(const char *dir, unsigned *g, int cap)
{
    char p[700];
    DIR *d, *d2;
    struct dirent *e, *e2;
    int n = 0, i, j;
    snprintf(p, sizeof p, "%s/aether/SAVES/%s", root, dir);
    if (!(d = opendir(p))) return 0;
    while ((e = readdir(d)))
        if (e->d_name[0] == 'G' && strlen(e->d_name) == 4) {
            char q[800];
            snprintf(q, sizeof q, "%s/%s", p, e->d_name);
            if (!(d2 = opendir(q))) continue;
            while ((e2 = readdir(d2)) && n < cap) {
                char x[900];
                struct stat st;
                if (e2->d_name[0] != 'G' || strlen(e2->d_name) != 6) continue;
                snprintf(x, sizeof x, "%s/%s/DONE.TXT", q, e2->d_name);
                if (!stat(x, &st)) g[n++] = (unsigned)atoi(e2->d_name + 1);
            }
            closedir(d2);
        }
    closedir(d);
    for (i = 1; i < n; i++) for (j = i; j > 0 && g[j] > g[j - 1]; j--) { unsigned t = g[j]; g[j] = g[j - 1]; g[j - 1] = t; }
    return n;
}

/* "DIR/FILE": FILE in DIR's set `back` generations before the current one (0 = the current set; DIR's own files when
 * it has no older complete generation) */
static int set_exists(const char *rel, int back)
{
    char d[300], q[700];
    const char *f = strrchr(rel, '/');
    unsigned g[512];
    int n;
    struct stat st;
    snprintf(d, sizeof d, "%.*s", (int)(f - rel), rel);
    n = gens_of(d, g, 512);
    if (back < n) snprintf(q, sizeof q, "%s/aether/SAVES/%s/G%03u/G%05u%s", root, d, g[back] / 100, g[back], f);
    else if (back == n) snprintf(q, sizeof q, "%s/aether/SAVES/%s", root, rel);
    else return 0;
    return !stat(q, &st);
}
static int cur_exists(const char *rel) { return set_exists(rel, 0); }
static int prev_exists(const char *rel) { return set_exists(rel, 1); }

/* M6 Task 24b: state() as the launcher reads it -- BRAM, the state files, and each save folder's CURRENT set as
 * "<dir>/@/<file>"; files of other generations (a failed archive's incomplete one, the earlier ones) and the
 * generations' DONE.TXT are left out. A failure before CLEAR leaves its incomplete generation on the card (never read;
 * Tidy saves removes it), so a converged card equals the clean run's in this view, not byte for byte. Read from the
 * state text alone (the reader rule over its "D/Gsss/Gnnnnn/DONE.TXT" lines), never from the card. */
static int cv_line(const char **l, char *line, size_t cap)
{
    const char *e;
    size_t n;
    if (!**l) return 0;
    e = strchr(*l, '\n');
    n = e ? (size_t)(e - *l) : strlen(*l);
    if (n >= cap) n = cap - 1;
    memcpy(line, *l, n);
    line[n] = 0;
    *l += n + (e ? 1 : 0);
    return 1;
}
static const char *cv_gen(const char *line)
{   /* the "/Gsss/Gnnnnn/" of a path, or NULL */
    const char *g = line;
    while ((g = strstr(g, "/G")))
        if (strlen(g) > 13 && g[5] == '/' && g[6] == 'G' && g[12] == '/') return g;
        else g++;
    return NULL;
}
static char *cur_view(const char *st)
{
    static char dirs[512][300];
    static unsigned cur[512];
    int nd = 0, i;
    sb b = { 0, 0, 0 };
    const char *l = st;
    char line[800], d[400], out[900];
    sb_add(&b, "");
    while (cv_line(&l, line, sizeof line)) {                 /* the current generation of each folder */
        const char *g = cv_gen(line);
        unsigned n;
        if (!g || strncmp(g + 13, "DONE.TXT:", 9)) continue;
        snprintf(d, sizeof d, "%.*s", (int)(g - line), line);
        n = (unsigned)atoi(g + 7);
        for (i = 0; i < nd && strcmp(dirs[i], d); i++) ;
        if (i == nd && nd < 512) { snprintf(dirs[nd], 300, "%s", d); cur[nd++] = 0; }
        if (i < nd && n > cur[i]) cur[i] = n;
    }
    for (l = st; cv_line(&l, line, sizeof line); ) {
        const char *g = cv_gen(line), *f;
        if (g) {                                             /* D/Gsss/Gnnnnn/<file> */
            snprintf(d, sizeof d, "%.*s", (int)(g - line), line);
            for (i = 0; i < nd && strcmp(dirs[i], d); i++) ;
            if (i == nd || cur[i] != (unsigned)atoi(g + 7) || !strstr(g + 13, ".BUP")) continue;
            snprintf(out, sizeof out, "%s/@%s\n", d, g + 12);
            sb_add(&b, out);
            continue;
        }
        f = strstr(line, "/G");
        if (f && strlen(f) >= 5 && (f[5] == '/' || f[5] == 0)) continue;      /* a shard or generation folder line */
        if (!strncmp(line, "GENLIST.TXT", 11)) continue;   /* 24c: state (the general set's loaded versions), as of
                                                             * the last DELETE_MARKER -- an undo's one included */
        if (line[0] == 'B' || !strncmp(line, "ACTIVE", 6) || !strncmp(line, "_QUARANTINE", 11) || !strstr(line, ".BUP")
            || !(f = strrchr(line, '/'))) {
            sb_add(&b, line); sb_add(&b, "\n");
            continue;
        }
        snprintf(d, sizeof d, "%.*s", (int)(f - line), line);  /* D/<file>: current while D has no generation */
        for (i = 0; i < nd && strcmp(dirs[i], d); i++) ;
        if (i < nd) continue;
        snprintf(out, sizeof out, "%s/@%s\n", d, f);
        sb_add(&b, out);
    }
    return b.s;
}

static long sd_read(const char *rel, char *out, long cap)
{
    char p[600];
    FILE *f;
    long n;
    snprintf(p, sizeof p, "%s/aether/SAVES/%s", root, rel);
    if (!(f = fopen(p, "rb"))) return -1;
    n = (long)fread(out, 1, (size_t)cap - 1, f);
    fclose(f);
    out[n] = 0;
    return n;
}

static void mkdirs_abs(char *p)
{
    char *s;
    for (s = p + strlen(root) + 1; *s; s++)
        if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    mkdir(p, 0755);
}

static void mkdirs(const char *rel)
{
    char p[600];
    snprintf(p, sizeof p, "%s/aether/SAVES/%s", root, rel);
    mkdirs_abs(p);
}

/* record content: deterministic bytes from a seed */
static uint8_t *content(uint32_t size, uint32_t seed)
{
    static uint8_t b[4][40000];
    static int k;
    uint8_t *o = b[k++ & 3];
    uint32_t i;
    for (i = 0; i < size; i++) o[i] = (uint8_t)((i * 31u + seed * 7u) ^ (i >> 5));
    return o;
}

static void bup_at(const char *absdir, const char *file, const char *name, const uint8_t *data, uint32_t size,
                   uint32_t date, uint16_t blocks)
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
    d.blocks = blocks;
    om_bup_encode_hdr(&d, h);
    snprintf(p, sizeof p, "%s", absdir);
    mkdirs_abs(p);
    snprintf(p, sizeof p, "%s/%s", absdir, file);
    f = fopen(p, "wb");
    fwrite(h, 1, sizeof h, f);
    fwrite(data, 1, size, f);
    fclose(f);
}

static void sd_bup(const char *dir, const char *file, const char *name, uint32_t size, uint32_t seed, uint32_t date,
                   uint16_t blocks)
{
    char a[600];
    snprintf(a, sizeof a, "%s/aether/SAVES/%s", root, dir);
    bup_at(a, file, name, content(size, seed), size, date, blocks);
}

static void sd_text(const char *rel, const char *text)
{
    char p[600];
    FILE *f;
    mkdirs("");
    snprintf(p, sizeof p, "%s/aether/SAVES/%s", root, rel);
    f = fopen(p, "wb");
    fputs(text, f);
    fclose(f);
}

static void marker(const char *line) { fake_bram_put(OM_PGS_MARKER, (const uint8_t *)line, (uint32_t)strlen(line), 0); }
static void rec(const char *name, uint32_t size, uint32_t seed, uint32_t date)
{
    fake_bram_put(name, content(size, seed), size, date);
}
/* a session VibeMenu left: the marker and its ACTIVE.TXT with the same nonce */
static void session(const char *gid_slot, const char *nonce8)
{
    char m[64], a[80];
    snprintf(m, sizeof m, "%s %s\n", gid_slot, nonce8);
    snprintf(a, sizeof a, "%s %s 00000000\n", gid_slot, nonce8);
    marker(m);
    sd_text("ACTIVE.TXT", a);
}
/* Kai's and Rings' reserved records (R4): present in every scenario, never moved */
static void reserved(void)
{
    rec("PSEUDO__SAT", 300, 91, 7);
    rec("SAVRING_001", 70, 92, 8);
    rec("_CARTDMPHDR", 40, 93, 9);
}

static void wipe(void)
{
    char cmd[400];
    snprintf(cmd, sizeof cmd, "rm -rf %s/aether %s/SAVES", root, root);
    if (system(cmd)) {}
    snprintf(cmd, sizeof cmd, "%s/aether", root);
    mkdir(cmd, 0755);
    {   /* the launcher's own file (pgsrun.c checks /aether with it, env.rel) and a game folder (lseq.c step 2) */
        FILE *f;
        snprintf(cmd, sizeof cmd, "%s/aether/LAUNCHER.BIN", root);
        if ((f = fopen(cmd, "wb"))) { fputs("OMSATLNC", f); fclose(f); }
        snprintf(cmd, sizeof cmd, "%s/Some Game (USA)", root);
        mkdir(cmd, 0755);
    }
    fake_reboot();
    fake_bram_reset();
    fake_mut_n = 0; fake_mut_cut = -1; fake_mut_fail = -1; fake_mut_fail_rc = 0;
    said[0] = 0;
}

/* --- the universe: every record before a launch, at its newest date, byte for byte ----------------------------- */
typedef struct { char name[12]; uint32_t date, size, crc; } uent;
typedef struct { uent e[256]; int n; } universe;

static void u_add(universe *u, const char nm[12], uint32_t date, uint32_t size, uint32_t crc)
{
    int i, j;
    if (!strcmp(nm, OM_PGS_MARKER)) return;           /* ours: a new nonce every session */
    for (i = 0; i < u->n; i++)
        if (!strcmp(u->e[i].name, nm)) {
            if (date < u->e[i].date) return;
            if (date > u->e[i].date) {                /* a newer version: the older ones no longer count */
                for (j = i; j < u->n; j++)
                    if (!strcmp(u->e[j].name, nm)) { u->e[j] = u->e[--u->n]; j--; }
                break;
            }
            if (u->e[i].crc == crc && u->e[i].size == size) return;
        }
    if (u->n < 256) {
        memcpy(u->e[u->n].name, nm, 12);
        u->e[u->n].date = date; u->e[u->n].size = size; u->e[u->n].crc = crc;
        u->n++;
    }
}

/* every decodable .BUP anywhere under the fake card root (VibeMenu's tree, its quarantine, Kai's SAVES) */
static void u_files(void (*fn)(void *, const char nm[12], uint32_t, uint32_t, uint32_t), void *ctx)
{
    char cmd[400];
    FILE *p;
    snprintf(cmd, sizeof cmd, "find %s -iname '*.BUP' 2>/dev/null", root);
    if (!(p = popen(cmd, "r"))) return;
    {
        char line[700];
        while (fgets(line, sizeof line, p)) {
            static uint8_t buf[70000];
            FILE *f;
            size_t n;
            om_bup_dir d;
            char nm[12];
            line[strcspn(line, "\n")] = 0;
            if (!(f = fopen(line, "rb"))) continue;
            n = fread(buf, 1, sizeof buf, f);
            fclose(f);
            if (om_bup_decode_hdr(buf, (uint32_t)n, &d) != OM_OK || !om_bup_name_norm(nm, d.name)) continue;
            fn(ctx, nm, d.date, d.datasize, om_crc32(0, buf + OM_BUP_HDR, d.datasize));
        }
    }
    pclose(p);
}

static void u_add_cb(void *u, const char nm[12], uint32_t date, uint32_t size, uint32_t crc)
{
    u_add((universe *)u, nm, date, size, crc);
}

static void snap(universe *u)
{
    int i, br;
    u->n = 0;
    for (i = 0; i < fake_bram_count(); i++) {
        const om_bup_dir *d = fake_bram_dir(i);
        const uint8_t *p = fake_bram_data(i, &br);
        char nm[12];
        if (br || !om_bup_name_norm(nm, d->name)) continue;   /* a torn record's data is already gone */
        u_add(u, nm, d->date, d->datasize, om_crc32(0, p, d->datasize));
    }
    u_files(u_add_cb, u);
}

typedef struct { const uent *want; int found; } u_find;
static void u_find_cb(void *ctx, const char nm[12], uint32_t date, uint32_t size, uint32_t crc)
{
    u_find *f = (u_find *)ctx;
    if (!strcmp(nm, f->want->name) && date == f->want->date && size == f->want->size && crc == f->want->crc)
        f->found = 1;
}

/* the number of universe entries missing now (each printed) */
static int u_lost(const universe *u, const char *what, int k)
{
    int i, j, br, lost = 0;
    for (i = 0; i < u->n; i++) {
        u_find f;
        f.want = &u->e[i];
        f.found = 0;
        for (j = 0; j < fake_bram_count() && !f.found; j++) {
            const om_bup_dir *d = fake_bram_dir(j);
            const uint8_t *p = fake_bram_data(j, &br);
            char nm[12];
            if (!br && om_bup_name_norm(nm, d->name))
                u_find_cb(&f, nm, d->date, d->datasize, om_crc32(0, p, d->datasize));
        }
        if (!f.found) u_files(u_find_cb, &f);
        if (!f.found) {
            lost++;
            printf("FAIL %s k=%d: %s (date %u) lost\n", what, k, u->e[i].name, u->e[i].date);
        }
    }
    return lost;
}

/* R4: Kai's and Rings' records are still in BRAM, untouched, and never written to VibeMenu's folders */
static int reserved_ok(void)
{
    static const struct { const char *n; uint32_t size, seed; } r[3] = {
        { "PSEUDO__SAT", 300, 91 }, { "SAVRING_001", 70, 92 }, { "_CARTDMPHDR", 40, 93 } };
    char cmd[400];
    int i, br, bad = 0;
    for (i = 0; i < 3; i++) {
        int k = fake_bram_find(r[i].n);
        if (k < 0 || memcmp(fake_bram_data(k, &br), content(r[i].size, r[i].seed), r[i].size) || br) bad++;
    }
    snprintf(cmd, sizeof cmd, "grep -rlaE 'PSEUDO__SAT|SAVRING_001|_CARTDMPHDR' %s/aether >/dev/null 2>&1", root);
    if (!system(cmd)) bad++;
    return bad == 0;
}

/* --- scenarios --------------------------------------------------------------------------------------------------- */
typedef struct { const char *name; void (*setup)(void); uint8_t saves, slot; const char *gid; uint8_t nojournal; } scen;
/* the records the user had at the start, each must survive any cut: name, size, seed */
typedef struct { const char *name; uint32_t size, seed; } orig;
static orig s_orig[16];
static int s_norig;
static void keep(const char *n, uint32_t size, uint32_t seed)
{
    s_orig[s_norig].name = n; s_orig[s_norig].size = size; s_orig[s_norig].seed = seed; s_norig++;
}

/* A, case 2: BRAM holds Guardian Heroes slot 0 (3 records + marker); its folder has an older generation (one stale
 * record, one gone from BRAM) and slot 1 in sub-folder 01, which must survive the slot-0 commit. Golden Axe's slot 0
 * has 2 records; launching it swaps. */
#define GH "GS-9031@U"
#define GA "GS-9125@U"
static void setup_a(void)
{
    rec("GHEROES_001", 10, 1, 100); rec("GHEROES_002", 3000, 2, 101); rec("GHEROES_CFG", 5000, 3, 102);
    reserved();
    session(GH " 0", "0000A001");
    sd_bup("GS-9031[U]", "GHEROES_001.BUP", "GHEROES_001", 10, 99, 50, 1);
    sd_bup("GS-9031[U]", "GHEROES_OLD.BUP", "GHEROES_OLD", 64, 98, 40, 2);
    sd_bup("GS-9031[U]/01", "GHEROES_001.BUP", "GHEROES_001", 200, 97, 60, 4);
    sd_bup("GS-9125[U]", "GAXEDUEL_01.BUP", "GAXEDUEL_01", 700, 4, 200, 0);
    sd_bup("GS-9125[U]", "GAXEDUEL_02.BUP", "GAXEDUEL_02", 64, 5, 201, 2);
    sd_bup("_GENERAL", "SAKURA_0001.BUP", "SAKURA_0001", 1500, 6, 250, 0);
    s_norig = 0;
    keep("GHEROES_001", 10, 1); keep("GHEROES_002", 3000, 2); keep("GHEROES_CFG", 5000, 3);
    keep("GAXEDUEL_01", 700, 4); keep("GAXEDUEL_02", 64, 5);
}

/* B, case 3 (Move): no marker, the general set in BRAM; _GENERAL and _GENERAL.OLD hold older generations */
static void setup_b(void)
{
    rec("SAKURA_0001", 4100, 11, 300); rec("DAYTONA__00", 128, 12, 301);
    reserved();
    sd_bup("_GENERAL", "SAKURA_0001.BUP", "SAKURA_0001", 2000, 13, 250, 0);
    sd_bup("_GENERAL.OLD", "NIGHTS_0001.BUP", "NIGHTS_0001", 90, 14, 150, 0);
    s_norig = 0;
    keep("SAKURA_0001", 4100, 11); keep("DAYTONA__00", 128, 12);
}

/* D, case 4: Golden Axe slot 2 in BRAM; per-game saves turned off: BRAM gets the general set back */
static void setup_d(void)
{
    rec("GAXEDUEL_01", 900, 21, 400); rec("GAXEDUEL_02", 64, 22, 401);
    reserved();
    session(GA " 2", "0000D002");
    sd_bup("_GENERAL", "SAKURA_0001.BUP", "SAKURA_0001", 4100, 23, 300, 0);
    sd_bup("_GENERAL", "DAYTONA__00.BUP", "DAYTONA__00", 128, 24, 301, 0);
    s_norig = 0;
    keep("GAXEDUEL_01", 900, 21); keep("GAXEDUEL_02", 64, 22); keep("SAKURA_0001", 4100, 23);
    keep("DAYTONA__00", 128, 24);
}

/* G/H (M6 Task 24b fix, review I6): Guardian Heroes in BRAM, its current set G00001 (complete) holding its three
 * records as BRAM does plus KAISAVE_001, imported from Kai after the archive -- BRAM lacks it. G: Guardian Heroes again
 * (case 1: the import goes into BRAM, no journal); H: Golden Axe (case 2: the archive carries it) */
static void setup_i(void)
{
    char p[700];
    FILE *f;
    rec("GHEROES_001", 10, 1, 100); rec("GHEROES_002", 3000, 2, 101); rec("GHEROES_CFG", 5000, 3, 102);
    reserved();
    session(GH " 0", "0000A001");
    sd_bup("GS-9031[U]/G000/G00001", "GHEROES_001.BUP", "GHEROES_001", 10, 1, 100, 1);
    sd_bup("GS-9031[U]/G000/G00001", "GHEROES_002.BUP", "GHEROES_002", 3000, 2, 101, 0);
    sd_bup("GS-9031[U]/G000/G00001", "GHEROES_CFG.BUP", "GHEROES_CFG", 5000, 3, 102, 0);
    sd_bup("GS-9031[U]/G000/G00001", "KAISAVE_001.BUP", "KAISAVE_001", 300, 31, 700, 0);
    snprintf(p, sizeof p, "%s/aether/SAVES/GS-9031[U]/G000/G00001/DONE.TXT", root);
    if ((f = fopen(p, "wb"))) fclose(f);
    sd_bup("GS-9125[U]", "GAXEDUEL_01.BUP", "GAXEDUEL_01", 700, 4, 200, 0);
    sd_bup("_GENERAL", "SAKURA_0001.BUP", "SAKURA_0001", 1500, 6, 250, 0);
    s_norig = 0;
    keep("GHEROES_001", 10, 1); keep("GHEROES_002", 3000, 2); keep("GHEROES_CFG", 5000, 3);
    keep("KAISAVE_001", 300, 31); keep("GAXEDUEL_01", 700, 4);
}

/* I (ruling "in-game deletes stick"): A's card, but ACTIVE.TXT's line 2 lists the versions loaded
 * with Guardian Heroes, GHEROES_OLD's among them: the game deleted it since. The archive must not carry it back; its
 * file stays in the folder's history */
static void setup_del(void)
{
    char a[400], nm[12];
    setup_a();
    memset(nm, 0, sizeof nm);
    memcpy(nm, "GHEROES_OLD", 11);
    snprintf(a, sizeof a, "%s 0000A001 00000000\nR %08X %08X\n", GH " 0",
             om_pgs_rec_id(nm, 40, 64, om_crc32(0, content(64, 98), 64)), 0x12345678u);
    sd_text("ACTIVE.TXT", a);
}

/* J/K: the console clock was reset. Guardian Heroes was loaded with GHEROES_001 V1
 * dated 2000 (ACTIVE.TXT lists it); the player then saved V2, dated 100. J: Guardian Heroes again (case 1); K: Golden
 * Axe (the archive). Neither may put V1 back over V2. */
static void setup_clock(void)
{
    char p[700], a[400], nm[12];
    FILE *f;
    rec("GHEROES_001", 10, 2, 100);                                       /* V2 */
    reserved();
    sd_bup("GS-9031[U]/G000/G00001", "GHEROES_001.BUP", "GHEROES_001", 10, 1, 2000, 1);   /* V1 */
    snprintf(p, sizeof p, "%s/aether/SAVES/GS-9031[U]/G000/G00001/DONE.TXT", root);
    if ((f = fopen(p, "wb"))) fclose(f);
    marker(GH " 0 0000C002\n");
    memset(nm, 0, sizeof nm);
    memcpy(nm, "GHEROES_001", 11);
    snprintf(a, sizeof a, "%s 0000C002 00000000\nR %08X\n", GH " 0", om_pgs_rec_id(nm, 2000, 10, om_crc32(0, content(10, 1), 10)));
    sd_text("ACTIVE.TXT", a);
    sd_bup("GS-9125[U]", "GAXEDUEL_01.BUP", "GAXEDUEL_01", 700, 4, 200, 0);
    s_norig = 0;
    keep("GHEROES_001", 10, 2); keep("GHEROES_001", 10, 1); keep("GAXEDUEL_01", 700, 4);
}

/* L/M: as J/K, but V1 (dated 2000) is a version the session never loaded -- an
 * import, another menu's copy: ACTIVE.TXT's list does not name it. L: relaunch; M: archive. BRAM's V2 is never
 * replaced; V1 is kept as an alternate (ALT/). */
static void setup_impdiff(void)
{
    char a[400];
    setup_clock();
    snprintf(a, sizeof a, "%s 0000C002 00000000\nR %08X\n", GH " 0", 0x12345678u);
    sd_text("ACTIVE.TXT", a);
}

/* N: two alternates of one record in one archive -- the current set's own GHEROES_001 V1 and an older
 * alternate V0 in its ALT/, neither on the list, BRAM holds V2. A cut after the first alternate is written: the resume
 * skips it (a copy is there) and must not pick its name for the second (a rename onto an existing file). */
static void setup_twoalt(void)
{
    setup_impdiff();
    sd_bup("GS-9031[U]/G000/G00001/ALT", "GHEROES_001.BUP", "GHEROES_001", 10, 3, 1500, 1);   /* V0 */
    keep("GHEROES_001", 10, 3);
}

/* O: no marker, BRAM holds the general set (SAKURA_0001 d300); Kai's LATEST import of SAKURA_0001
 * (d3000, never loaded: GENLIST.TXT's "I" line, no "R" list) sits in _GENERAL's current set. The case 3 archive keeps
 * it as an alternate; BRAM's copy is what stays current */
static void setup_genimp(void)
{
    char a[200], nm[12];
    rec("SAKURA_0001", 400, 11, 300);
    reserved();
    sd_bup("_GENERAL", "SAKURA_0001.BUP", "SAKURA_0001", 400, 41, 3000, 0);
    memset(nm, 0, sizeof nm);
    memcpy(nm, "SAKURA_0001", 11);
    snprintf(a, sizeof a, "\nI %08X\n", om_pgs_rec_id(nm, 3000, 400, om_crc32(0, content(400, 41), 400)));
    sd_text("GENLIST.TXT", a);
    s_norig = 0;
    keep("SAKURA_0001", 400, 11); keep("SAKURA_0001", 400, 41);
}

static const scen k_scen[] = {
    { "A case 2", setup_a, 1, 0, GA },
    { "B case 3 move", setup_b, 1, 2, GH },
    { "C case 3 share", setup_b, 2, 2, GH },
    { "D case 4", setup_d, 0, 0, GH },
    { "E case 2 slot", setup_a, 1, 5, GH },
    { "F case 2 share", setup_a, 2, 3, GA },     /* case 2 with Share, Golden Axe's slot 3 new */
    { "G case 1 import", setup_i, 1, 0, GH, 1 }, /* reconcile, no journal */
    { "H case 2 import", setup_i, 1, 0, GA },    /* the archive carries the imported record */
    { "I case 2 deleted", setup_del, 1, 0, GA }, /* a record the game deleted is not carried back */
    { "J case 1 clock", setup_clock, 1, 0, GH, 1 }, /* C2: the session's older version never wins over BRAM */
    { "K case 2 clock", setup_clock, 1, 0, GA },
    { "L case 1 import differs", setup_impdiff, 1, 0, GH, 1 },   /* I7: kept both, BRAM's copy stays */
    { "M case 2 import differs", setup_impdiff, 1, 0, GA },
    { "N case 2 two alternates", setup_twoalt, 1, 0, GA },   /* 24c: ALT/ names after a cut */
    { "O case 3 general import", setup_genimp, 1, 0, GH },   /* 24c fix I9: kept both in _GENERAL */
};
#define NSCEN ((int)(sizeof k_scen / sizeof k_scen[0]))

/* the current-set property, checked around every run(). Every record of every
 * save folder's current set before the launch is, after it (also after a cut or a failed call), in that folder's
 * current set or intact in BRAM at that version or a newer one (SPEC �5.16 om_bup_prefer). Current sets by the
 * host's own copy of the reader rule (gens_of). */
typedef struct { char dir[64], fn[24]; uint8_t hdr[OM_BUP_HDR]; uint32_t crc; } cs_rec;
static cs_rec s_cs[2][600];
static int s_ncs[2], s_cs_viol, s_cs_runs, s_cs_deleted;
/* the ruling "in-game deletes stick": before the run, the versions ACTIVE.TXT's line 2 lists, and BRAM's names (torn
 * ones too). A record BRAM lacked entirely, at a listed version (or in _GENERAL), was deleted: it may leave "current" */
static uint32_t s_pre_ids[600];
static int s_pre_nids;
static char s_pre_bram[600][12];
static int s_pre_nb;
/* the console-side property. Every intact BRAM version before the run is, after
 * it, still in BRAM, or in a current set or _QUARANTINE (byte for byte), or superseded in BRAM by a card version of
 * that record the session never loaded (an import that ranks above it, �5.16) */
typedef struct { char nm[12]; uint32_t date, size, crc; } bv_t;
static bv_t s_pre_bv[600];
static int s_pre_nbv, s_bv_viol, s_bv_runs;
static void cs_pre(void)
{
    char a[8192];
    long n = sd_read("ACTIVE.TXT", a, sizeof a);
    char *l2 = n > 0 ? strchr(a, '\n') : NULL;
    int i;
    s_pre_nids = 0;
    if (l2 && l2[1] == 'R') {
        char *p = l2 + 2;
        unsigned v;
        int c;
        while (s_pre_nids < 600 && sscanf(p, " %8x%n", &v, &c) == 1) { s_pre_ids[s_pre_nids++] = v; p += c; }
    }
    s_pre_nb = 0;
    for (i = 0; i < fake_bram_count() && s_pre_nb < 600; i++) om_bup_name_norm(s_pre_bram[s_pre_nb++], fake_bram_dir(i)->name);
    s_pre_nbv = 0;
    for (i = 0; i < fake_bram_count() && s_pre_nbv < 600; i++) {
        int br;
        const uint8_t *d = fake_bram_data(i, &br);
        bv_t *v = &s_pre_bv[s_pre_nbv];
        if (br || !om_bup_name_norm(v->nm, fake_bram_dir(i)->name) || om_pgs_reserved(v->nm)) continue;
        v->date = fake_bram_dir(i)->date; v->size = fake_bram_dir(i)->datasize; v->crc = om_crc32(0, d, v->size);
        s_pre_nbv++;
    }
}

static int bv_file_walk(const char *abs, const bv_t *v)
{   /* a .BUP holding version v under abs (recursive) */
    DIR *d = opendir(abs);
    struct dirent *e;
    int hit = 0;
    if (!d) return 0;
    while (!hit && (e = readdir(d))) {
        char a[900];
        struct stat st;
        if (e->d_name[0] == '.') continue;
        snprintf(a, sizeof a, "%s/%s", abs, e->d_name);
        if (stat(a, &st)) continue;
        if (S_ISDIR(st.st_mode)) { hit = bv_file_walk(a, v); continue; }
        if (strcasestr(e->d_name, ".BUP")) {
            static uint8_t b[70000];
            om_bup_dir h;
            FILE *f = fopen(a, "rb");
            long n;
            if (!f) continue;
            n = (long)fread(b, 1, sizeof b, f);
            fclose(f);
            hit = !om_bup_decode_hdr(b, (uint32_t)n, &h) && !memcmp(h.name, v->nm, 12) && h.date == v->date
                  && h.datasize == v->size && om_crc32(0, b + 64, h.datasize) == v->crc;
        }
    }
    closedir(d);
    return hit;
}
static int bv_in_cur(const bv_t *v)
{   /* in a save folder's current set (any file of it, not only the winner) */
    char a[600], c[800];
    DIR *d;
    struct dirent *e;
    int hit = 0, k;
    snprintf(a, sizeof a, "%s/aether/SAVES", root);
    if (!(d = opendir(a))) return 0;
    while (!hit && (e = readdir(d))) {
        char r[64];
        if (e->d_name[0] == '.' || (e->d_name[0] == '_' && strcmp(e->d_name, "_GENERAL")) || strchr(e->d_name, '.'))
            continue;
        for (k = 0; k <= 9 && !hit; k++) {
            unsigned g[512];
            int n;
            if (k) snprintf(r, sizeof r, "%s/0%d", e->d_name, k); else snprintf(r, sizeof r, "%s", e->d_name);
            n = gens_of(r, g, 512);
            if (n) snprintf(c, sizeof c, "%s/%s/G%03u/G%05u", a, r, g[0] / 100, g[0]);
            else snprintf(c, sizeof c, "%s/%s", a, r);
            {   /* this folder's own files only (never its slot or generation sub-folders) */
                DIR *dd = opendir(c);
                struct dirent *f;
                if (!dd) continue;
                while (!hit && (f = readdir(dd))) {
                    char q[1000];
                    struct stat st;
                    snprintf(q, sizeof q, "%s/%s", c, f->d_name);
                    if (!strcasestr(f->d_name, ".BUP") || stat(q, &st) || S_ISDIR(st.st_mode)) continue;
                    {
                        static uint8_t b[70000];
                        om_bup_dir h;
                        FILE *fp = fopen(q, "rb");
                        long m;
                        if (!fp) continue;
                        m = (long)fread(b, 1, sizeof b, fp);
                        fclose(fp);
                        hit = !om_bup_decode_hdr(b, (uint32_t)m, &h) && !memcmp(h.name, v->nm, 12)
                              && h.date == v->date && h.datasize == v->size && om_crc32(0, b + 64, h.datasize) == v->crc;
                    }
                }
                closedir(dd);
            }
        }
    }
    closedir(d);
    return hit;
}
static void bv_check(const char *what)
{
    int i;
    s_bv_runs++;
    for (i = 0; i < s_pre_nbv; i++) {
        const bv_t *v = &s_pre_bv[i];
        int k = fake_bram_find(v->nm), ok = 0, br, j;
        char a[600];
        if (k >= 0) {
            const uint8_t *d = fake_bram_data(k, &br);
            const om_bup_dir *bd = fake_bram_dir(k);
            uint32_t crc = br ? 0 : om_crc32(0, d, bd->datasize);
            if (!br && bd->date == v->date && bd->datasize == v->size && crc == v->crc) continue;       /* kept */
            (void)j; (void)br;                     /* fix 4 (I7): no card version may replace it, import or not */
        }
        if (!ok) ok = bv_in_cur(v);
        if (!ok) { snprintf(a, sizeof a, "%s/aether/SAVES/_QUARANTINE", root); ok = bv_file_walk(a, v); }
        if (!ok && s_bv_viol++ < 8)
            printf("CONSOLE-SIDE VIOLATION (%s): BRAM's %s (date %u) is gone from BRAM and every current set\n", what,
                   v->nm, v->date);
    }
}
static int cs_was_deleted(const cs_rec *o)
{
    char nm[12];
    uint32_t id;
    int i;
    memcpy(nm, o->hdr + 0x10, 11);
    nm[11] = 0;
    if (!om_bup_name_norm(nm, nm)) return 0;
    for (i = 0; i < s_pre_nb; i++) if (!memcmp(s_pre_bram[i], nm, 12)) return 0;
    if (!strcmp(o->dir, "_GENERAL")) return 1;
    id = om_pgs_rec_id(nm, om_be32(o->hdr + 0x28), om_be32(o->hdr + 0x2C), o->crc);
    for (i = 0; i < s_pre_nids; i++) if (s_pre_ids[i] == id) return 1;
    return 0;
}

static void cs_dir(int w, const char *rel)
{
    char d[700], q[900];
    unsigned g[512];
    int n = gens_of(rel, g, 512), i;
    DIR *dd;
    struct dirent *e;
    if (n) snprintf(d, sizeof d, "%s/aether/SAVES/%s/G%03u/G%05u", root, rel, g[0] / 100, g[0]);
    else snprintf(d, sizeof d, "%s/aether/SAVES/%s", root, rel);
    if (!(dd = opendir(d))) return;
    while ((e = readdir(dd))) {
        static uint8_t b[70000];
        om_bup_dir h;
        FILE *f;
        long k;
        size_t L = strlen(e->d_name);
        if (L < 5 || L >= 24 || strcasecmp(e->d_name + L - 4, ".BUP")) continue;
        snprintf(q, sizeof q, "%s/%s", d, e->d_name);
        if (!(f = fopen(q, "rb"))) continue;
        k = (long)fread(b, 1, sizeof b, f);
        fclose(f);
        if (om_bup_decode_hdr(b, (uint32_t)k, &h) || om_pgs_reserved(h.name)) continue;
        for (i = 0; i < s_ncs[w]; i++)                       /* the �5.16 winner per record */
            if (!strcmp(s_cs[w][i].dir, rel) && !memcmp(s_cs[w][i].hdr + 0x10, b + 0x10, 11)) break;
        if (i < s_ncs[w] && !om_bup_prefer(b, e->d_name, s_cs[w][i].hdr, s_cs[w][i].fn)) continue;
        if (i == s_ncs[w]) { if (s_ncs[w] == 600) continue; s_ncs[w]++; }
        snprintf(s_cs[w][i].dir, sizeof s_cs[w][i].dir, "%s", rel);
        snprintf(s_cs[w][i].fn, sizeof s_cs[w][i].fn, "%s", e->d_name);
        memcpy(s_cs[w][i].hdr, b, OM_BUP_HDR);
        s_cs[w][i].crc = om_crc32(0, b + OM_BUP_HDR, h.datasize);
    }
    closedir(dd);
}
static void cs_snap(int w)
{
    char a[600];
    DIR *d;
    struct dirent *e;
    s_ncs[w] = 0;
    snprintf(a, sizeof a, "%s/aether/SAVES", root);
    if (!(d = opendir(a))) return;
    while ((e = readdir(d))) {
        char r[64];
        int k;
        if (e->d_name[0] == '.' || (e->d_name[0] == '_' && strcmp(e->d_name, "_GENERAL")) || strchr(e->d_name, '.'))
            continue;
        cs_dir(w, e->d_name);
        for (k = 1; k <= 9; k++) { snprintf(r, sizeof r, "%s/0%d", e->d_name, k); cs_dir(w, r); }
    }
    closedir(d);
}
static int cs_same_or_newer(const uint8_t *h, const char *fn, uint32_t crc, const cs_rec *o)
{
    if (!memcmp(h + 0x28, o->hdr + 0x28, 8) && crc == o->crc) return 1;          /* the same date, size and data */
    return !om_bup_prefer(o->hdr, o->fn, h, fn);      /* not older: the old copy is not ranked above it (§5.16) */
}
static void cs_check(const char *what)
{
    int i, j;
    cs_snap(1);
    s_cs_runs++;
    for (i = 0; i < s_ncs[0]; i++) {
        const cs_rec *o = &s_cs[0][i];
        int ok = 0, k;
        for (j = 0; j < s_ncs[1] && !ok; j++)
            if (!strcmp(s_cs[1][j].dir, o->dir) && !memcmp(s_cs[1][j].hdr + 0x10, o->hdr + 0x10, 11))
                ok = cs_same_or_newer(s_cs[1][j].hdr, s_cs[1][j].fn, s_cs[1][j].crc, o);
        if (!ok) {
            char nm[12];
            memcpy(nm, o->hdr + 0x10, 11);
            nm[11] = 0;
            om_bup_name_norm(nm, nm);
            if ((k = fake_bram_find(nm)) >= 0) {
                int br;
                const uint8_t *d = fake_bram_data(k, &br);
                om_bup_dir bd = *fake_bram_dir(k);
                uint8_t h[OM_BUP_HDR];
                char fn[OM_PGS_FNAME];
                if (!br && !om_bup_encode_hdr(&bd, h) && !om_pgs_file_name(fn, nm, 0, 0))
                    ok = cs_same_or_newer(h, fn, om_crc32(0, d, bd.datasize), o);
            }
        }
        if (!ok && cs_was_deleted(o)) { s_cs_deleted++; ok = 1; }   /* deleted in the game: stays deleted */
        if (!ok) {   /* C2: BRAM held the record (another version) before the run -- the console's copy wins */
            char nm[12];
            int t;
            memcpy(nm, o->hdr + 0x10, 11);
            nm[11] = 0;
            om_bup_name_norm(nm, nm);
            for (t = 0; t < s_pre_nbv && memcmp(s_pre_bv[t].nm, nm, 12); t++) ;
            if (t < s_pre_nbv) { s_cs_deleted++; ok = 1; }
        }
        if (!ok && s_cs_viol++ < 8)
            printf("CURRENT-SET VIOLATION (%s): %s/%s dropped out of the current set and BRAM\n", what, o->dir, o->fn);
    }
}

/* swap speed, part 2: every launch starts in the game's folder (lseq.c step 2) -- after a power cut too, so a resume
 * is checked from a working folder that is not /aether; the run names its files from /aether (env.rel) */
static void in_game_folder(void)
{
    static uint32_t g[16];
    strcpy((char *)g, "/Some Game (USA)");
    s_chdir((const char *)g);
}

static int run(const scen *s, om_pgs_result *r)
{
    int rc;
    cs_snap(0);
    cs_pre();
    in_game_folder();
    rc = om_pgs_run(&env, s->saves, s->slot, s->gid, r);
    if (!fake_dead) CHECK_INT(r->rel, PGS_REL);      /* a run cut by a power cut may end before its check */
    in_game_folder();                                /* lseq.c save_swap puts it back */
    cs_check(s->name);
    bv_check(s->name);                                       /* also after a cut: the card and BRAM as the cut left them */
    return rc;
}

static int survived(const orig *o)
{
    universe u;
    u.n = 1;
    memset(&u.e[0], 0, sizeof u.e[0]);
    strncpy(u.e[0].name, o->name, 11);
    {   /* any date: the original bytes anywhere */
        int k = fake_bram_find(o->name), br;
        u_find f;
        if (k >= 0) {
            const uint8_t *d = fake_bram_data(k, &br);
            if (!br && fake_bram_dir(k)->datasize == o->size && !memcmp(d, content(o->size, o->seed), o->size))
                return 1;
        }
        u.e[0].size = o->size;
        u.e[0].crc = om_crc32(0, content(o->size, o->seed), o->size);
        f.want = &u.e[0];
        f.found = 0;
        {
            char cmd[400];
            FILE *p;
            snprintf(cmd, sizeof cmd, "find %s -iname '*.BUP' 2>/dev/null", root);
            if ((p = popen(cmd, "r"))) {
                char line[700];
                while (fgets(line, sizeof line, p) && !f.found) {
                    static uint8_t buf[70000];
                    FILE *fh;
                    size_t n;
                    om_bup_dir d;
                    line[strcspn(line, "\n")] = 0;
                    if (!(fh = fopen(line, "rb"))) continue;
                    n = fread(buf, 1, sizeof buf, fh);
                    fclose(fh);
                    if (om_bup_decode_hdr(buf, (uint32_t)n, &d) == OM_OK && !strncmp(d.name, o->name, 11)
                        && d.datasize == o->size && !memcmp(buf + OM_BUP_HDR, content(o->size, o->seed), o->size))
                        f.found = 1;
                }
                pclose(p);
            }
        }
        return f.found;
    }
}

static char *bram_only(const char *st)
{   /* the "B ..." lines of a state */
    sb b = { 0, 0, 0 };
    const char *l = st;
    sb_add(&b, "");
    while (*l) {
        const char *e = strchr(l, '\n');
        size_t n = e ? (size_t)(e - l + 1) : strlen(l);
        if (l[0] == 'B' && l[1] == ' ') { char t[200]; memcpy(t, l, n); t[n] = 0; sb_add(&b, t); }
        l += n;
    }
    return b.s;
}

/* an undo loads the marker's current set, which now also carries the records BRAM lacked
 * (an import, a record the game deleted): BRAM = the old BRAM plus records of a current set (s_cs[1], cs_check's
 * snapshot after the run), byte for byte -- never a record from anywhere else */
static int undo_with_carried(const char *b, const char *oldb)
{
    const char *l = b;
    char line[200];
    while (*l) {
        const char *e = strchr(l, '\n');
        size_t n = e ? (size_t)(e - l) : strlen(l);
        char nm[16];
        unsigned size, crc;
        int j, ok = 0;
        if (n >= sizeof line) return 0;
        memcpy(line, l, n);
        line[n] = 0;
        l += n + (e ? 1 : 0);
        {   /* the same line in the old BRAM */
            const char *f = strstr(oldb, line);
            if (f && (f == oldb || f[-1] == '\n') && (f[n] == '\n' || !f[n])) continue;
        }
        if (sscanf(line, "B %15[^:]:%u:%x", nm, &size, &crc) != 3 || strchr(line, '!')) return 0;
        for (j = 0; j < s_ncs[1] && !ok; j++)
            ok = !strncmp((const char *)s_cs[1][j].hdr + 0x10, nm, 11) && om_be32(s_cs[1][j].hdr + 0x2C) == size
                 && s_cs[1][j].crc == crc;
        if (!ok) return 0;
    }
    {   /* and every old record still there */
        const char *o = oldb;
        while (*o) {
            const char *e = strchr(o, '\n');
            size_t n = e ? (size_t)(e - o) : strlen(o);
            if (n >= sizeof line) return 0;
            memcpy(line, o, n);
            line[n] = 0;
            o += n + (e ? 1 : 0);
            if (!strstr(b, line)) return 0;
        }
    }
    return 1;
}

static char *folder_lines(char *st, const char *prefix)
{   /* the lines of a state under one folder (frees st) */
    sb b = { 0, 0, 0 };
    const char *l = st;
    sb_add(&b, "");
    while (*l) {
        const char *e = strchr(l, '\n');
        size_t n = e ? (size_t)(e - l + 1) : strlen(l);
        if (!strncmp(l, prefix, strlen(prefix))) { char t[300]; memcpy(t, l, n); t[n] = 0; sb_add(&b, t); }
        l += n;
    }
    free(st);
    return b.s;
}

/* R3: the marker and ACTIVE.TXT agree on gid, slot and nonce, or neither exists */
static int active_follows_marker(void)
{
    int m = fake_bram_find(OM_PGS_MARKER), br;
    char a[80];
    long n = sd_read("ACTIVE.TXT", a, sizeof a);
    if (n < 0) n = sd_read("ACTIVE.TXT.NEW", a, sizeof a);     /* sdwrite.h's recovery rule: .NEW when it is absent */
    if (m < 0) return n < 0;
    {
        const uint8_t *d = fake_bram_data(m, &br);
        uint32_t len = fake_bram_dir(m)->datasize;
        return !br && len > 1 && n > (long)len && !memcmp(a, d, len - 1) && a[len - 1] == ' ';
    }
}

static void sweep(const scen *s)
{
    om_pgs_result r;
    universe u;
    char *clean, *old, *oldb, *newb, *st;
    int K, k, bad_cut = 0, bad_lost = 0, bad_fail = 0, bad_conv = 0, bad_res = 0, resumed = 0, undone = 0, j;
    wipe(); s->setup();
    old = state();
    oldb = bram_only(old);
    CHECK_INT(run(s, &r), 0);
    K = fake_mut_n;
    clean = state();
    newb = bram_only(clean);
    CHECK(!sd_exists("JOURNAL.TXT") && !sd_exists("JOURNAL.TXT.NEW"));
    CHECK(strstr(clean, ".NEW") == NULL);
    CHECK(reserved_ok());
    CHECK(active_follows_marker());
    printf("%s: %d mutating calls in a clean run\n", s->name, K);
    /* a power cut after call k (k = 0: before the first), the reboot, then the next launch */
    for (k = 0; k <= K; k++) {
        int i;
        wipe(); s->setup();
        fake_mut_cut = k ? k : -1;
        if (k == 0) fake_dead = 1;
        run(s, &r);
        for (i = 0; i < s_norig; i++)
            if (!survived(&s_orig[i])) { bad_lost++; printf("FAIL %s cut %d: %s lost\n", s->name, k, s_orig[i].name); }
        fake_reboot(); fake_mut_cut = -1;
        if (run(s, &r) != 0) { bad_cut++; printf("FAIL %s cut %d: next launch rc %d (%s)\n", s->name, k, r.rc, r.line); }
        resumed += r.resumed;
        if (!reserved_ok()) { bad_res++; printf("FAIL %s cut %d: a reserved record moved\n", s->name, k); }
        st = state();
        if (strcmp(st, clean)) { bad_cut++; printf("FAIL %s cut %d: end state differs\n--- got\n%s--- want\n%s", s->name, k, st, clean); }
        free(st);
    }
    /* a cut during the resume too (sampled), then a third launch */
    for (k = 1; k <= K; k += 5)
        for (j = 1; j <= 60; j += 7) {
            wipe(); s->setup();
            fake_mut_cut = k;
            run(s, &r);
            fake_reboot(); fake_mut_n = 0; fake_mut_cut = j;
            run(s, &r);
            fake_reboot(); fake_mut_cut = -1;
            if (run(s, &r) != 0) { bad_cut++; printf("FAIL %s cut %d+%d: rc %d (%s)\n", s->name, k, j, r.rc, r.line); }
            st = state();
            if (strcmp(st, clean)) { bad_cut++; printf("FAIL %s cut %d+%d: end state differs\n", s->name, k, j); }
            free(st);
        }
    /* a failed call at k: BRAM old or new, never a mix; ACTIVE follows the marker when no journal is left; the next
     * launch converges */
    for (k = 1; k <= K; k++) {
        char *b;
        wipe(); s->setup();
        snap(&u);                                      /* the universe here too */
        if (u.n <= 0) { bad_fail++; printf("FAIL %s fail %d: empty universe\n", s->name, k); }
        fake_mut_fail = k; fake_mut_fail_rc = -FR_DISK_ERR;
        run(s, &r);
        bad_lost += u_lost(&u, s->name, k);
        undone += r.undone;
        if (getenv("PGS_DEBUG")) printf("DBG %s fail %d: rc %d op %d line \"%s\" notice \"%s\" undone %d\n", s->name, k,
                                         r.rc, r.op, r.line, r.notice, r.undone);
        st = state();
        b = bram_only(st);
        if (strcmp(b, oldb) && strcmp(b, newb) && !(r.undone && undo_with_carried(b, oldb))) { bad_fail++; printf("FAIL %s fail %d: BRAM is a mix\n%s", s->name, k, b); }
        if (!sd_exists("JOURNAL.TXT") && !active_follows_marker()) {
            bad_fail++;
            printf("FAIL %s fail %d: ACTIVE.TXT does not follow the marker\n", s->name, k);
        }
        free(b); free(st);
        fake_mut_fail = -1;
        snap(&u);
        if (run(s, &r) != 0) { bad_conv++; printf("FAIL %s fail %d: next launch rc %d (%s)\n", s->name, k, r.rc, r.line); }
        bad_lost += u_lost(&u, s->name, k);
        st = state();
        {   /* compared as the launcher reads it (cur_view) */
            char *a = cur_view(st), *c = cur_view(clean);
            if (strcmp(a, c)) {
                bad_conv++;
                printf("FAIL %s fail %d: did not converge (next: notice \"%s\" foreign %d resumed %d)\n", s->name,
                       k, r.notice, r.foreign, r.resumed);
                if (bad_conv == 1) printf("--- got\n%s--- want\n%s", a, c);
            }
            free(a); free(c);
        }
        free(st);
    }
    printf("%s: cuts %d+1, %d resumed; failures %d, %d undone\n", s->name, K, resumed, K, undone);
    CHECK_INT(bad_lost, 0);
    CHECK_INT(bad_cut, 0);
    CHECK_INT(bad_fail, 0);
    CHECK_INT(bad_conv, 0);
    CHECK_INT(bad_res, 0);
    CHECK(resumed > 0 || s->nojournal);
    free(clean); free(old); free(oldb); free(newb);
}

/* --- another menu's session between a cut and the next VibeMenu launch (I4) ------------------------------------- */
enum { V_OFFICIAL, V_KAIFLUSH, V_MARKERGONE, V_STALE, V_KAIHOLDS, V_TORN, V_SAMEFP, V_N };
static const char *const k_vname[V_N] = { "official menu", "Kai flush", "marker deleted", "stale marker copy",
                                          "Kai holds BRAM", "torn newer save", "same-fingerprint byte edit" };

static int first_plain(void)
{   /* the first non-reserved, readable BRAM record, or -1 */
    int i, br;
    for (i = 0; i < fake_bram_count(); i++) {
        const om_bup_dir *d = fake_bram_dir(i);
        char nm[12];
        fake_bram_data(i, &br);
        if (!br && om_bup_name_norm(nm, d->name) && !om_pgs_reserved(nm)) return i;
    }
    return -1;
}

static void foreign_session(int v)
{
    int i, br;
    char nm[12];
    switch (v) {
    case V_OFFICIAL:                                   /* the user played: a newer copy of a record, and a new one */
        if ((i = first_plain()) >= 0) {
            const om_bup_dir *d = fake_bram_dir(i);
            uint32_t size = d->datasize, date = d->date;
            om_bup_name_norm(nm, d->name);
            fake_bram_del(nm);
            fake_bram_put(nm, content(size, 500), size, date + 1000);
        }
        rec("FOREIGN_001", 50, 78, 901);
        break;
    case V_KAIFLUSH: {                                 /* Kai moved every non-reserved record (ours too) to its folder */
        char dir[600];
        snprintf(dir, sizeof dir, "%s/SAVES/BRAM/KAIGAME", root);
        for (i = 0; i < fake_bram_count(); i++) {
            const om_bup_dir *d = fake_bram_dir(i);
            const uint8_t *p = fake_bram_data(i, &br);
            char fn[32];
            if (br || !om_bup_name_norm(nm, d->name) || (om_pgs_reserved(nm) && strcmp(nm, OM_PGS_MARKER))) continue;
            snprintf(fn, sizeof fn, "%s.BUP", nm);
            bup_at(dir, fn, nm, p, d->datasize, d->date, 0);
            fake_bram_del(nm);
            i--;
        }
        rec("PSEUDO_XTRA", 20, 94, 10);
        break;
    }
    case V_MARKERGONE:                                 /* the BIOS memory manager */
        fake_bram_del(OM_PGS_MARKER);
        break;
    case V_STALE:                                      /* Kai re-imported an old copy: old marker, an older record */
        fake_bram_del(OM_PGS_MARKER);
        marker(GH " 0 0BADBEEF\n");
        if ((i = first_plain()) >= 0) {
            const om_bup_dir *d = fake_bram_dir(i);
            uint32_t size = d->datasize, date = d->date;
            om_bup_name_norm(nm, d->name);
            fake_bram_del(nm);
            fake_bram_put(nm, content(size, 501), size, date ? date - 1 : 0);
        }
        break;
    case V_TORN:                                       /* the game's BUP_Write of a
                                                        * NEWER version of a record, cut mid-write: a torn record */
        if ((i = first_plain()) >= 0) {
            const om_bup_dir *d = fake_bram_dir(i);
            om_bup_dir w = *d;
            w.date = d->date + 1000;
            fake_mut_cut = fake_mut_n + 1;
            om_bram_write(&w, content(d->datasize, 600));
            fake_reboot(); fake_mut_cut = -1;
            om_bram_init(env.lib, env.work);
        }
        break;
    case V_SAMEFP:                                     /* same name, date and size, new bytes */
        if ((i = first_plain()) >= 0) {
            const om_bup_dir *d = fake_bram_dir(i);
            uint32_t size = d->datasize, date = d->date;
            om_bup_name_norm(nm, d->name);
            fake_bram_del(nm);
            fake_bram_put(nm, content(size, 700), size, date);
        }
        break;
    case V_KAIHOLDS:                                   /* Kai's own per-game marker and a game's record */
        rec("PSEUDO_BRAM", 16, 95, 11);
        rec("KAIGAME_001", 90, 96, 12);
        break;
    }
}

static void foreign_sweep(const scen *s)
{
    om_pgs_result r;
    universe u;
    int K, k, v, lost = 0, rcbad = 0, notflag = 0, resbad = 0, tried = 0, jn, nonempty = 0;
    wipe(); s->setup(); run(s, &r); K = fake_mut_n;
    for (v = 0; v < V_N; v++)
        for (k = 0; k <= K; k++) {
            wipe(); s->setup();
            fake_mut_cut = k ? k : -1;
            if (k == 0) fake_dead = 1;
            run(s, &r);
            fake_reboot(); fake_mut_cut = -1;
            {   /* a journal the executor will resume: the recovery rule (sdwrite.h) -- JOURNAL.TXT wins when present,
                 * and an empty one is no journal (pgsrun.c); without it, JOURNAL.TXT.NEW */
                char b[256];
                long n = sd_read("JOURNAL.TXT", b, sizeof b);
                jn = n > 0 || (n < 0 && sd_read("JOURNAL.TXT.NEW", b, sizeof b) > 0);
            }
            om_bram_init(env.lib, env.work);
            foreign_session(v);
            snap(&u);
            if (u.n <= 0) { nonempty++; printf("FAIL %s %s cut %d: empty universe\n", s->name, k_vname[v], k); }
            if (run(s, &r) != 0) { rcbad++; printf("FAIL %s %s cut %d: rc %d (%s)\n", s->name, k_vname[v], k, r.rc, r.line); }
            lost += u_lost(&u, s->name, k);
            /* R2: a journal whose BRAM changed is never resumed blindly */
            if (jn && v == V_OFFICIAL) {
                tried++;
                if (!r.foreign) { notflag++; printf("FAIL %s %s cut %d: BRAM changed, not flagged\n", s->name, k_vname[v], k); }
            }
            if (v != V_KAIFLUSH && !reserved_ok()) { resbad++; printf("FAIL %s %s cut %d: reserved moved\n", s->name, k_vname[v], k); }
            snap(&u);                                  /* and once more: a settled launch loses nothing either */
            if (run(s, &r) != 0) { rcbad++; printf("FAIL %s %s cut %d: 2nd rc %d (%s)\n", s->name, k_vname[v], k, r.rc, r.line); }
            lost += u_lost(&u, s->name, k);
        }
    printf("%s foreign: %d kinds x %d cuts, %d journaled official-menu cuts\n", s->name, V_N, K + 1, tried);
    CHECK_INT(lost, 0);
    CHECK_INT(rcbad, 0);
    CHECK_INT(notflag, 0);
    CHECK_INT(resbad, 0);
    CHECK_INT(nonempty, 0);
}

/* --- fix round 3: a game's torn save keeps the card copy in the game's folder ---------------------- */
static void tear(int i)
{   /* the game's BUP_Write of a NEWER version of BRAM record i, cut mid-write: a torn (BUP_BROKEN) record */
    om_bup_dir w = *fake_bram_dir(i);
    w.date += 1000;
    fake_mut_cut = fake_mut_n + 1;
    om_bram_write(&w, content(w.datasize, 600));
    fake_reboot(); fake_mut_cut = -1;
    om_bram_init(env.lib, env.work);
}

/* the newest copy of record nm in a game folder (any folder under SAVES/ but _QUARANTINE, *.NEW and *.OLD); 0 = none */
static int folder_copy(const char *nm, uint32_t *date, uint32_t *crc)
{
    char cmd[600];
    FILE *p;
    int found = 0;
    snprintf(cmd, sizeof cmd, "find %s/aether/SAVES -iname '*.BUP' -not -path '*/_QUARANTINE/*' -not -path '*.NEW/*' "
             "-not -path '*.OLD/*' 2>/dev/null", root);
    if (!(p = popen(cmd, "r"))) return 0;
    {
        char line[700];
        while (fgets(line, sizeof line, p)) {
            static uint8_t buf[70000];
            FILE *f;
            size_t n;
            om_bup_dir d;
            char rn[12];
            line[strcspn(line, "\n")] = 0;
            if (!(f = fopen(line, "rb"))) continue;
            n = fread(buf, 1, sizeof buf, f);
            fclose(f);
            if (om_bup_decode_hdr(buf, (uint32_t)n, &d) != OM_OK || !om_bup_name_norm(rn, d.name) || strcmp(rn, nm))
                continue;
            if (!found || d.date > *date) {
                *date = d.date;
                *crc = om_crc32(0, buf + OM_BUP_HDR, d.datasize);
                found = 1;
            }
        }
    }
    pclose(p);
    return found;
}

/* the reviewer's single-fault trace (scratchpad/rr1/single.c), ported: Golden Axe's save torn by a power cut during the
 * game's own save; one swap away must keep GS-9125[U]/GAXEDUEL_01.BUP in Golden Axe's folder and say so, and the next
 * Golden Axe launch must get it back. RED on round-2 code: the file moved to _QUARANTINE, no notice, BRAM empty. */
static void single_torn(void)
{
    om_pgs_result r;
    scen back = { "GH", 0, 1, 0, GH };
    uint32_t d0 = 0, c0 = 0, d1 = 0, c1 = 0;
    int i;
    wipe(); setup_a();
    run(&k_scen[0], &r);
    run(&back, &r);
    run(&k_scen[0], &r);                               /* Golden Axe in BRAM, its save also in GS-9125[U] */
    CHECK(cur_exists("GS-9125[U]/GAXEDUEL_01.BUP"));
    CHECK(folder_copy("GAXEDUEL_01", &d0, &c0));
    i = fake_bram_find("GAXEDUEL_01");
    CHECK(i >= 0);
    if (i < 0) return;
    tear(i);
    CHECK_INT(run(&back, &r), 0);
    CHECK(cur_exists("GS-9125[U]/GAXEDUEL_01.BUP"));   /* still in the game's folder */
    CHECK(folder_copy("GAXEDUEL_01", &d1, &c1) && d1 == d0 && c1 == c0);
    CHECK_INT(r.torn, 1);
    CHECK_INT(r.torn_kept, 1);
    CHECK_STR(r.notice, GA ": DAMAGED SAVE IN CONSOLE MEMORY, KEPT THE CARD COPY");
    CHECK_INT(run(&k_scen[0], &r), 0);                 /* Golden Axe again: its save is back in BRAM */
    i = fake_bram_find("GAXEDUEL_01");
    CHECK(i >= 0 && fake_bram_dir(i)->date == d0);
    if (i >= 0) {
        int br;
        const uint8_t *d = fake_bram_data(i, &br);
        CHECK(!br && om_crc32(0, d, fake_bram_dir(i)->datasize) == c0);
    }
}

/* the same single fault after every cut of every scenario: wherever the record's newest intact copy sat in a game
 * folder at tear time, it is still in a game folder (or intact in BRAM) after the next two launches */
static void torn_sweep(void)
{
    om_pgs_result r;
    int s, k, K, runs = 0, skipped = 0, bad = 0, i;
    for (s = 0; s < NSCEN; s++) {
        wipe(); k_scen[s].setup(); run(&k_scen[s], &r); K = fake_mut_n;
        for (k = 0; k <= K; k++) {
            char nm[12];
            uint32_t d0, c0, d1, c1;
            int j, br, ok = 0;
            wipe(); k_scen[s].setup();
            fake_mut_cut = k ? k : -1;
            if (k == 0) fake_dead = 1;
            run(&k_scen[s], &r);
            fake_reboot(); fake_mut_cut = -1;
            om_bram_init(env.lib, env.work);
            if ((i = first_plain()) < 0) { skipped++; continue; }
            om_bup_name_norm(nm, fake_bram_dir(i)->name);
            if (!folder_copy(nm, &d0, &c0)) { skipped++; continue; }
            tear(i);
            runs++;
            run(&k_scen[s], &r);
            run(&k_scen[s], &r);
            j = fake_bram_find(nm);
            if (j >= 0) {
                const uint8_t *d = fake_bram_data(j, &br);
                ok = !br && fake_bram_dir(j)->date >= d0
                     && (fake_bram_dir(j)->date > d0 || om_crc32(0, d, fake_bram_dir(j)->datasize) == c0);
            }
            if (!ok && folder_copy(nm, &d1, &c1)) ok = d1 > d0 || (d1 == d0 && c1 == c0);
            if (!ok) { bad++; printf("FAIL torn %s cut %d: %s's intact copy left the game folders\n", k_scen[s].name, k, nm); }
        }
    }
    printf("torn sweep: %d runs (%d cuts with no folder copy of the record skipped); %d moved out\n", runs, skipped, bad);
    CHECK(runs > 0);
    CHECK_INT(bad, 0);
}

/* every folder creation fails (a full or write-protected card) after each cut of every
 * scenario -- the launch may stop, but nothing is lost; then the fault clears and two launches converge without loss */
static void mkdir_fails(void)
{
    om_pgs_result r;
    universe u;
    int s, k, K, lost = 0, runs = 0, stopped = 0, bad2 = 0, failed_any = 0;
    for (s = 0; s < NSCEN; s++) {
        wipe(); k_scen[s].setup(); run(&k_scen[s], &r); K = fake_mut_n;
        for (k = 0; k <= K; k++) {
            wipe(); k_scen[s].setup();
            fake_mut_cut = k ? k : -1;
            if (k == 0) fake_dead = 1;
            run(&k_scen[s], &r);
            fake_reboot(); fake_mut_cut = -1;
            snap(&u);
            if (u.n <= 0) lost++;
            fake_fail_op = 'M'; fake_fail_skip = 0; fake_fail_times = 1000000; fake_fail_rc = -FR_DENIED;
            runs++;
            if (run(&k_scen[s], &r) != 0) stopped++;
            failed_any += fake_fail_times < 1000000;
            fake_fail_op = 0; fake_fail_times = 0;
            lost += u_lost(&u, "mkdir fails", k);
            snap(&u);
            if (run(&k_scen[s], &r) != 0) { bad2++; printf("FAIL mkdir %s cut %d: rc %d (%s)\n", k_scen[s].name, k, r.rc, r.line); }
            lost += u_lost(&u, "mkdir fails, then", k);
            run(&k_scen[s], &r);
            lost += u_lost(&u, "mkdir fails, then 2", k);
        }
    }
    printf("mkdir fails: %d runs, %d stopped with an rc, %d hit a refused mkdir; %d lost\n", runs, stopped, failed_any, lost);
    CHECK(failed_any > 0);                             /* the mode is not vacuous */
    CHECK_INT(lost, 0);
    CHECK_INT(bad2, 0);
}

/* Review C1 (task-23-review.md; the reviewer's probe, scratchpad/foreign/foreign.c, ported): a power cut after each
 * call of scenario A, a reboot, then ANOTHER MENU's session writes BRAM -- a newer GAXEDUEL_01 and a new FOREIGN_001 --
 * then VibeMenu's next launch. Both records must still exist somewhere (BRAM or any .BUP on the card). RED before the
 * fix (Task 23 round 0 code, measured): losses in 34 + 41 of 72 journaled cuts. */
static void probe_c1(void)
{
    om_pgs_result r;
    int k, K, lostnew = 0, lostupd = 0, tried = 0;
    orig fn = { "FOREIGN_001", 50, 78 }, up = { "GAXEDUEL_01", 700, 77 };
    wipe(); setup_a(); run(&k_scen[0], &r); K = fake_mut_n;
    for (k = 1; k <= K; k++) {
        wipe(); setup_a();
        fake_mut_cut = k; run(&k_scen[0], &r);
        fake_reboot(); fake_mut_cut = -1;
        if (!sd_exists("JOURNAL.TXT") && !sd_exists("JOURNAL.TXT.NEW")) continue;
        tried++;
        om_bram_init(env.lib, env.work);                 /* the other menu's session (no fault counting) */
        fake_bram_del("GAXEDUEL_01");
        rec("GAXEDUEL_01", 700, 77, 900);
        rec("FOREIGN_001", 50, 78, 901);
        run(&k_scen[0], &r);
        if (!survived(&fn)) { lostnew++; printf("FAIL probe C1 cut %d: FOREIGN_001 lost\n", k); }
        if (!survived(&up)) { lostupd++; printf("FAIL probe C1 cut %d: newer GAXEDUEL_01 lost\n", k); }
    }
    printf("probe C1: %d journaled cuts of %d; new record lost %d, newer record lost %d\n", tried, K, lostnew, lostupd);
    CHECK(tried > 0);
    CHECK_INT(lostnew, 0);
    CHECK_INT(lostupd, 0);
}

/* the swap's live progress (hardware 2026-11-24: "SAVE SWAP - KEEP THE POWER ON" for minutes with nothing moving):
 * step n of N from the first step, the last step reached, each file named and finished, the beat never going back */
static om_pgs_prog s_sp_last;
static int s_sp_n, s_sp_back, s_sp_files, s_sp_first_step;
static void sp_rec(const om_pgs_prog *p)
{
    if (!s_sp_n++) s_sp_first_step = (int)p->step;
    if (p->beat < s_sp_last.beat) s_sp_back++;
    if (p->file_total && p->file_done == p->file_total) s_sp_files++;
    s_sp_last = *p;
}

/* R3: a stale marker copy (same game and slot as ACTIVE.TXT, an older nonce, an older record) is foreign: BRAM goes to
 * quarantine and the folder the marker names is not touched */
static void stale_marker(void)
{
    om_pgs_result r;
    universe u;
    char *before, *after;
    scen other = { "stale", 0, 1, 0, "T-1234567@J" };
    wipe();
    rec("GHEROES_001", 10, 1, 10);                       /* the old copy Kai brought back */
    reserved();
    marker(GH " 0 0BADBEEF\n");
    sd_text("ACTIVE.TXT", GH " 0 0000C0DE 00000000\n");
    sd_bup("GS-9031[U]", "GHEROES_001.BUP", "GHEROES_001", 10, 77, 20, 1);   /* the newer copy on the card */
    snap(&u);
    before = folder_lines(state(), "GS-9031[U]/");
    CHECK(strstr(before, "GS-9031[U]/GHEROES_001.BUP:74:") != NULL);   /* the probe sees the folder (not a glob) */
    env.prog = sp_rec;
    s_sp_n = s_sp_back = s_sp_files = 0;
    memset(&s_sp_last, 0, sizeof s_sp_last);
    CHECK_INT(run(&other, &r), 0);
    env.prog = 0;
    CHECK(s_sp_n > 0);
    CHECK_INT(s_sp_first_step, 1);
    CHECK(s_sp_last.steps > 0 && s_sp_last.step == s_sp_last.steps);
    CHECK(s_sp_last.stage[0] != 0);
    CHECK(s_sp_files > 0);                               /* the quarantined record, written and named */
    CHECK_INT(s_sp_back, 0);
    CHECK_INT(r.foreign, 1);
    CHECK(!strncmp(r.notice, "CONSOLE SAVES CHANGED SINCE LAST TIME.  COPY KEPT ON THE CARD: _QUARANTINE/", 75));
    CHECK(strlen(r.notice) <= 80);                       /* two rows */
    CHECK_INT(u_lost(&u, "stale marker", 0), 0);
    after = folder_lines(state(), "GS-9031[U]/");
    CHECK_STR(after, before);                            /* the folder the stale marker names: untouched */
    CHECK(reserved_ok());
    CHECK(active_follows_marker());
    free(before); free(after);
}

/* R1: the card holds a NEWER copy than BRAM (copied back on a PC): the archive never deletes it */
static void newer_on_card(void)
{
    om_pgs_result r;
    universe u;
    wipe();
    rec("GHEROES_001", 10, 1, 10);
    reserved();
    session(GH " 0", "0000AAAA");
    sd_bup("GS-9031[U]", "GHEROES_001.BUP", "GHEROES_001", 10, 77, 20, 1);
    snap(&u);
    CHECK_INT(run(&k_scen[0], &r), 0);
    CHECK_INT(u_lost(&u, "newer on card", 0), 0);
    CHECK(prev_exists("GS-9031[U]/GHEROES_001.BUP") && !r.notice[0]);   /* still where it was */
    {   /* fix 3: no ACTIVE.TXT list -- BRAM's copy wins over a card copy dated later (a console clock
         * reset dates the newer save earlier); the card copy stays on the card */
        scen back = { "GH", 0, 1, 0, GH, 0 };
        int i;
        CHECK_INT(run(&back, &r), 0);
        CHECK((i = fake_bram_find("GHEROES_001")) >= 0 && fake_bram_dir(i)->date == 10);
        CHECK(prev_exists("GS-9031[U]/GHEROES_001.BUP"));
    }
}

/* I3: the marker lost twice in a row while a session is active -- the general set must never be overwritten */
static void marker_lost_twice(void)
{
    om_pgs_result r;
    universe u;
    int t;
    wipe(); setup_b();
    CHECK_INT(run(&k_scen[1], &r), 0);                  /* the general set to _GENERAL, Guardian Heroes slot 2 in */
    for (t = 0; t < 2; t++) {
        char nm[16];
        snprintf(nm, sizeof nm, "GHEROES_%03d", t & 7);
        rec(nm, 400, 60 + (uint32_t)t, 700 + (uint32_t)t);   /* the user played */
        fake_bram_del(OM_PGS_MARKER);                        /* and deleted the marker in the memory manager */
        snap(&u);
        CHECK_INT(run(&k_scen[1], &r), 0);
        CHECK_INT(r.foreign, 1);
        CHECK_INT(u_lost(&u, "marker lost", t), 0);
        CHECK(cur_exists("_GENERAL/SAKURA_0001.BUP") && cur_exists("_GENERAL/DAYTONA__00.BUP"));
        CHECK(active_follows_marker());
    }
}

/* I3/R5: repeated launches with no marker and no ACTIVE.TXT, BRAM different each time: rotations keep every
 * generation (the dropped one is quarantined) */
static void no_marker_rotations(void)
{
    om_pgs_result r;
    universe u;
    int t;
    scen off = { "off", 0, 0, 0, GH };
    wipe(); setup_b();
    for (t = 0; t < 4; t++) {
        char nm[16];
        CHECK_INT(run(&k_scen[1], &r), 0);              /* case 3 */
        snprintf(nm, sizeof nm, "OTHER_%05d", t & 7);
        fake_bram_del(OM_PGS_MARKER);                   /* another card or a reinstall: no marker, no ACTIVE.TXT */
        {
            char p[600];
            snprintf(p, sizeof p, "%s/aether/SAVES/ACTIVE.TXT", root);
            remove(p);
        }
        rec(nm, 300, 80 + (uint32_t)t, 800 + (uint32_t)t);
        snap(&u);
        CHECK_INT(run(&k_scen[1], &r), 0);              /* rotates _GENERAL */
        CHECK_INT(u_lost(&u, "rotations", t), 0);
        CHECK_INT(run(&off, &r), 0);                    /* and back to the general set */
    }
}

/* a journal from a cut, then Kai's session leaves PSEUDO_BRAM: no resume either -- nothing written,
 * the journal stays; once Kai has taken its marker away the next launch resumes under R2 and loses nothing */
static void kai_holds_with_journal(void)
{
    om_pgs_result r;
    universe u;
    char *a, *b;
    wipe(); setup_a();
    fake_mut_cut = 40;
    run(&k_scen[0], &r);
    fake_reboot(); fake_mut_cut = -1;
    om_bram_init(env.lib, env.work);
    rec("PSEUDO_BRAM", 16, 95, 11);
    rec("KAIGAME_001", 90, 96, 12);
    snap(&u);
    a = state();
    fake_mut_n = 0;
    CHECK_INT(run(&k_scen[0], &r), 0);
    CHECK_INT(r.paused, 1);
    CHECK_INT(r.resumed, 0);
    CHECK_INT(fake_mut_n, 0);
    b = state();
    CHECK(!strcmp(a, b));
    CHECK(sd_exists("JOURNAL.TXT") || sd_exists("JOURNAL.TXT.NEW"));
    fake_bram_del("PSEUDO_BRAM");                       /* Kai booted and dropped its own marker */
    snap(&u);
    CHECK(u.n > 0);
    CHECK_INT(run(&k_scen[0], &r), 0);
    CHECK_INT(r.resumed, 1);
    CHECK_INT(u_lost(&u, "kai then resume", 0), 0);
    free(a); free(b);
}

/* R4/I2: Kai's PSEUDO_BRAM in BRAM -- Kai holds a game's set: no swap, nothing written */
static void kai_holds(void)
{
    om_pgs_result r;
    char *a, *b;
    wipe(); setup_a();
    rec("PSEUDO_BRAM", 16, 95, 11);
    a = state();
    fake_mut_n = 0;
    CHECK_INT(run(&k_scen[0], &r), 0);
    CHECK_INT(r.paused, 1);
    CHECK_INT(fake_mut_n, 0);
    b = state();
    CHECK(!strcmp(a, b));
    CHECK(!strncmp(r.notice, "SAVES: KAI'S PSEUDO_BRAM IN BRAM", 32));
    free(a); free(b);
}

/* a folder imported from Kai or Rings without areas ("SAVES/<product>") is the game's folder while
 * "SAVES/<product>[<areas>]" is absent (Kai's product-only rule, SPEC §5.14) -- loaded at the launch, archived back to
 * it at the next swap, and never re-routed to the region folder; with both present the region folder wins. */
static void product_only(void)
{
    om_pgs_result r;
    int i;
    wipe(); setup_b();
    sd_bup("GS-9031", "GHEROES_001.BUP", "GHEROES_001", 77, 41, 500, 0);          /* Rings' folder, imported */
    CHECK_INT(om_pgs_run(&env, 1, 0, GH, &r), 0);    i = fake_bram_find("GHEROES_001");
    CHECK(i >= 0 && fake_bram_dir(i)->datasize == 77);
    CHECK(!sd_exists("GS-9031[U]"));
    fake_bram_put("GHEROES_002", content(20, 42), 20, 501);                       /* the game saves */
    CHECK_INT(om_pgs_run(&env, 1, 0, GA, &r), 0);                                 /* another game: archive */
    CHECK(cur_exists("GS-9031/GHEROES_002.BUP"));
    CHECK(!sd_exists("GS-9031[U]"));
    CHECK_INT(om_pgs_run(&env, 1, 0, GH, &r), 0);                                 /* back: loaded from it again */
    CHECK(fake_bram_find("GHEROES_002") >= 0);
    /* both folders: the region folder wins (a game already played under VibeMenu keeps its folder) */
    wipe(); setup_b();
    sd_bup("GS-9031", "GHEROES_001.BUP", "GHEROES_001", 77, 41, 500, 0);
    sd_bup("GS-9031[U]", "GHEROES_001.BUP", "GHEROES_001", 55, 43, 502, 0);
    CHECK_INT(om_pgs_run(&env, 1, 0, GH, &r), 0);
    i = fake_bram_find("GHEROES_001");
    CHECK(i >= 0 && fake_bram_dir(i)->datasize == 55);
    /* a journal resumed after a cut re-derives the same folder */
    wipe(); setup_b();
    sd_bup("GS-9031", "GHEROES_001.BUP", "GHEROES_001", 77, 41, 500, 0);
    for (i = 1; i < 60; i++) {
        wipe(); setup_b();
        sd_bup("GS-9031", "GHEROES_001.BUP", "GHEROES_001", 77, 41, 500, 0);
        fake_mut_cut = i;
        om_pgs_run(&env, 1, 0, GH, &r);
        fake_reboot(); fake_mut_cut = -1;
        CHECK_INT(om_pgs_run(&env, 1, 0, GH, &r), 0);
        {
            int k = fake_bram_find("GHEROES_001");
            if (!(k >= 0 && fake_bram_dir(k)->datasize == 77)) { th_fail++; printf("FAIL product_only cut %d\n", i); }
            else th_pass++;
        }
        if (sd_exists("GS-9031[U]")) { th_fail++; printf("FAIL product_only cut %d: region folder made\n", i); }
    }
}

/* a slot's FIRST load compares record by record. The reviewer's
 * scenario: the console's general set holds a newer GAMESAV_001 than the slot folder the import brought (Kai's, older).
 * The newer intact copy is loaded; the other is never dropped (the slot keeps its file until a newer copy supersedes it
 * by R1, _GENERAL keeps the newer). Only the first load: afterwards the slot's own saves load as they are. */
static int bram_date(const char *nm)
{
    int i = fake_bram_find(nm);
    return i < 0 ? -1 : (int)fake_bram_dir(i)->date;
}

static void first_load(void)
{
    om_pgs_result r;
    int i;
    /* case 3: no marker, BRAM holds the general set with the newer save */
    wipe();
    rec("GAMESAV_001", 300, 61, 900); rec("OTHERSAV_01", 40, 62, 50);
    reserved();
    sd_bup("T-1111[U]", "GAMESAV_001.BUP", "GAMESAV_001", 200, 63, 100, 0);   /* imported from Kai: older */
    sd_bup("T-1111[U]", "GAMESAV_002.BUP", "GAMESAV_002", 20, 64, 100, 0);    /* only in the slot */
    CHECK_INT(om_pgs_run(&env, 2, 0, "T-1111@U", &r), 0);
    CHECK_INT(bram_date("GAMESAV_001"), 900);                                  /* the newer one */
    CHECK_INT(bram_date("GAMESAV_002"), 100);
    CHECK(cur_exists("T-1111[U]/GAMESAV_001.BUP"));                             /* the other copy: still on the card */
    CHECK(cur_exists("_GENERAL/GAMESAV_001.BUP"));
    CHECK(sd_exists("T-1111[U]/VIBEMENU.TXT"));                                /* loaded once */
    /* no date decides over the console: the slot's copy is dated later, but the
     * console's own copy (just archived into _GENERAL) is what loads; the slot's stays on the card */
    wipe();
    rec("GAMESAV_001", 300, 61, 100);
    sd_bup("T-1111[U]", "GAMESAV_001.BUP", "GAMESAV_001", 200, 63, 900, 0);
    CHECK_INT(om_pgs_run(&env, 2, 0, "T-1111@U", &r), 0);
    CHECK_INT(bram_date("GAMESAV_001"), 100);
    CHECK(cur_exists("_GENERAL/GAMESAV_001.BUP") && cur_exists("T-1111[U]/GAMESAV_001.BUP"));
    CHECK(strstr(r.notice, "IMPORTED SAVE DIFFERS - KEPT BOTH") != NULL);    /* 24c: said at this launch */
    CHECK(!strncmp(r.notice, "T-1111@U:", 9));
    CHECK_INT(om_pgs_run(&env, 2, 0, "T-2222@U", &r), 0);                  /* the archive: not said again */
    CHECK(strstr(r.notice, "IMPORTED SAVE DIFFERS") == NULL);
    /* case 2: another game in BRAM; _GENERAL holds a copy dated later -- no console version involved (fix 4): the
     * slot's own copy loads, the general one stays */
    wipe();
    rec("GHEROES_001", 10, 1, 100);
    session(GH " 0", "0000A001");
    sd_bup("_GENERAL", "GAMESAV_001.BUP", "GAMESAV_001", 300, 61, 900, 0);
    sd_bup("T-1111[U]", "GAMESAV_001.BUP", "GAMESAV_001", 200, 63, 100, 0);
    CHECK_INT(om_pgs_run(&env, 2, 0, "T-1111@U", &r), 0);
    CHECK_INT(bram_date("GAMESAV_001"), 100);
    CHECK(cur_exists("T-1111[U]/GAMESAV_001.BUP") && cur_exists("_GENERAL/GAMESAV_001.BUP"));
    /* not the first load: the user played with saves off later and _GENERAL got newer -- the slot's own save loads */
    fake_bram_del("GAMESAV_001");
    fake_bram_put("GAMESAV_001", content(300, 70), 300, 950);                  /* played in the slot */
    CHECK_INT(om_pgs_run(&env, 2, 0, GH, &r), 0);                              /* archived into the slot */
    sd_bup("_GENERAL", "GAMESAV_001.BUP", "GAMESAV_001", 300, 71, 990, 0);     /* a later general-set save */
    CHECK_INT(om_pgs_run(&env, 2, 0, "T-1111@U", &r), 0);
    CHECK_INT(bram_date("GAMESAV_001"), 950);
    /* a power cut after every call of the first-load swap: the next launch still ends with the newer copy */
    for (i = 1; i < 80; i++) {
        wipe();
        rec("GAMESAV_001", 300, 61, 900); rec("OTHERSAV_01", 40, 62, 50);
        sd_bup("T-1111[U]", "GAMESAV_001.BUP", "GAMESAV_001", 200, 63, 100, 0);
        fake_mut_cut = i;
        om_pgs_run(&env, 2, 0, "T-1111@U", &r);
        fake_reboot(); fake_mut_cut = -1;
        CHECK_INT(om_pgs_run(&env, 2, 0, "T-1111@U", &r), 0);
        if (bram_date("GAMESAV_001") != 900) { th_fail++; printf("FAIL first_load cut %d: date %d\n", i, bram_date("GAMESAV_001")); }
        else th_pass++;
    }
}

/* slots 3 and 0. Slot 3 is its own save line: it never
 * takes records from _GENERAL, so its date-100 save loads and survives. At slot 0 the first-load prefer
 * stands; the older file then goes only when a newer copy is on the card (C1b; the R1 property checks every delete). */
static void sim_c1(void)
{
    om_pgs_result r;
    char rel[64];
    int slot;
    for (slot = 3; slot >= 0; slot -= 3) {
        wipe();
        rec("GAMESAV_001", 300, 61, 900);                                     /* the slot-0 line, newer */
        snprintf(rel, sizeof rel, slot ? "T-1111[U]/0%d" : "T-1111[U]", slot);
        sd_bup(rel, "GAMESAV_001.BUP", "GAMESAV_001", 200, 63, 100, 0);       /* a separate line, no stamp */
        CHECK_INT(om_pgs_run(&env, 2, (uint8_t)slot, "T-1111@U", &r), 0);
        CHECK_INT(bram_date("GAMESAV_001"), slot ? 100 : 900);
        CHECK_INT(om_pgs_run(&env, 2, 0, GH, &r), 0);                         /* the next launch archives the slot */
        {
            uint32_t date = 0, crc = 0;
            char a[128];
            snprintf(a, sizeof a, "%s/GAMESAV_001.BUP", rel);
            CHECK(sd_exists(a));
            if (slot) {                                                       /* its own save, still there */
                char b[300];
                long n = sd_read(a, b, sizeof b);
                CHECK(n > 64 && b[0x28] == 0 && b[0x29] == 0 && b[0x2A] == 0 && (uint8_t)b[0x2B] == 100);
            }
            (void)date; (void)crc;
        }
    }
}

int main(void)
{
    om_pgs_result r;
    char a[128], *st, *st2;
    int i;
    if (!mkdtemp(root)) { printf("FAIL mkdtemp\n"); return 1; }
    setenv("FAKE_SD_ROOT", root, 1);
    om_pgs_arena_split(&env, (uint8_t *)arena32);
    env.say = say;
    env.nonce = nonce;
    env.rel = PGS_REL;                                   /* the launcher's mode (lseq.c save_swap) */
    env.deep = 1;                                        /* option A forced on for every op that has it (relative
                                                          * names only: no effect in test_pgsrun_abs) */
    fake_r1_on = 1; fake_addonly = 1;
    fake_nopath = 1;                                     /* FatFs's FR_NO_PATH for a missing folder */                     /* the R1 property over every test and sweep below */

    /* the gate: saves off, no ACTIVE.TXT, no journal -> not one backup-library call */
    wipe(); setup_b();
    CHECK_INT(om_pgs_run(&env, 0, 0, GH, &r), 0);
    CHECK_INT(fake_bram_inits, 0);
    CHECK_INT(r.ran, 0);
    CHECK_INT(fake_mut_n, 0);

    /* A, clean: the end state field by field */
    wipe(); setup_a();
    CHECK_INT(run(&k_scen[0], &r), 0);
    CHECK_INT(r.ran, 1);
    CHECK_STR(said, "SAVE SWAP - KEEP THE POWER ON");
    CHECK(fake_bram_find("GAXEDUEL_01") >= 0 && fake_bram_find("GAXEDUEL_02") >= 0);
    CHECK(fake_bram_find("GHEROES_001") < 0 && fake_bram_find("GHEROES_CFG") < 0);
    CHECK_INT(fake_bram_count(), 3 + 3);                                   /* + the three reserved records */
    CHECK(sd_read("ACTIVE.TXT", a, sizeof a) > 0 && !strncmp(a, GA " 0 ", 12));
    CHECK(active_follows_marker());
    CHECK(cur_exists("GS-9031[U]/GHEROES_001.BUP") && cur_exists("GS-9031[U]/GHEROES_002.BUP")
          && cur_exists("GS-9031[U]/GHEROES_CFG.BUP"));
    CHECK(cur_exists("GS-9031[U]/GHEROES_OLD.BUP"));    /* not in BRAM: carried, stays current (24b fix) */
    {
        orig o = { "GHEROES_OLD", 64, 98 };
        CHECK(survived(&o));
        CHECK(prev_exists("GS-9031[U]/GHEROES_OLD.BUP") && !r.notice[0]);   /* and still where it was, silent */
    }
    CHECK(cur_exists("GS-9031[U]/01/GHEROES_001.BUP"));                     /* slot 1 kept (the nesting) */
    CHECK(!sd_exists("GS-9031[U].NEW") && !sd_exists("JOURNAL.TXT"));
    CHECK(reserved_ok());
    for (i = 0; i < 3; i++) CHECK(survived(&s_orig[i]));
    /* the same launch again: case 1, nothing written */
    i = fake_mut_n;
    CHECK_INT(run(&k_scen[0], &r), 0);
    CHECK_INT(fake_mut_n, i);
    /* then Guardian Heroes again: its saves come back from the card */
    {
        scen back = { "A back", 0, 1, 0, GH };
        CHECK_INT(run(&back, &r), 0);
        CHECK(fake_bram_find("GHEROES_CFG") >= 0 && fake_bram_find("GAXEDUEL_01") < 0);
        for (i = 0; i < 3; i++) CHECK(survived(&s_orig[i]));
        CHECK(cur_exists("GS-9125[U]/GAXEDUEL_01.BUP"));
        CHECK(active_follows_marker());
    }

    /* I, clean (ruling "in-game deletes stick"): GHEROES_OLD, loaded with Guardian Heroes and deleted by the game
     * since, is not carried into the new generation; its file stays in the folder's history */
    wipe(); setup_del();
    CHECK_INT(run(&k_scen[8], &r), 0);
    CHECK(!strcmp(k_scen[8].name, "I case 2 deleted"));
    CHECK(!cur_exists("GS-9031[U]/GHEROES_OLD.BUP") && prev_exists("GS-9031[U]/GHEROES_OLD.BUP"));
    CHECK(cur_exists("GS-9031[U]/GHEROES_001.BUP"));
    {
        scen back = { "GH", 0, 1, 0, GH, 0 };
        CHECK_INT(run(&back, &r), 0);
        CHECK(fake_bram_find("GHEROES_OLD") < 0 && fake_bram_find("GHEROES_CFG") >= 0);
    }

    /* O, clean (24c fix I9): the general import kept as an alternate, BRAM's copy current, the notice */
    wipe(); setup_genimp();
    CHECK_INT(run(&k_scen[14], &r), 0);
    CHECK(!strcmp(k_scen[14].name, "O case 3 general import"));
    CHECK(!strncmp(r.notice, "GENERAL SAVES:", 14) && strstr(r.notice, "IMPORTED SAVE DIFFERS - KEPT BOTH"));
    {
        unsigned g[16];
        char q[300];
        CHECK(gens_of("_GENERAL", g, 16) >= 1);
        snprintf(q, sizeof q, "_GENERAL/G%03u/G%05u/ALT/SAKURA_0001~1.BUP", g[0] / 100, g[0]);
        CHECK(sd_exists(q) || (q[strlen(q) - 6] = 0, strcat(q, ".BUP"), sd_exists(q)));
    }

    /* B and C, clean */
    wipe(); setup_b();
    CHECK_INT(run(&k_scen[1], &r), 0);
    CHECK_INT(fake_bram_count(), 1 + 3);                                   /* Move: the game starts empty */
    CHECK(cur_exists("_GENERAL/SAKURA_0001.BUP") && cur_exists("_GENERAL/DAYTONA__00.BUP"));
    CHECK(prev_exists("_GENERAL/SAKURA_0001.BUP") && !prev_exists("_GENERAL/NIGHTS_0001.BUP"));
    {
        orig o = { "NIGHTS_0001", 90, 14 };                               /* R5: the dropped generation is kept */
        CHECK(survived(&o));
    }
    CHECK(sd_read("ACTIVE.TXT", a, sizeof a) > 0 && !strncmp(a, GH " 2 ", 12));
    wipe(); setup_b();
    CHECK_INT(run(&k_scen[2], &r), 0);
    CHECK_INT(fake_bram_count(), 3 + 3);                                   /* Share: a copy of the general set */
    /* D, clean */
    wipe(); setup_d();
    CHECK_INT(run(&k_scen[3], &r), 0);
    CHECK(fake_bram_find(OM_PGS_MARKER) < 0 && fake_bram_find("SAKURA_0001") >= 0);
    CHECK(!sd_exists("ACTIVE.TXT"));
    CHECK(cur_exists("GS-9125[U]/02/GAXEDUEL_01.BUP"));
    CHECK(reserved_ok());

    /* CHECK_FITS: Golden Axe's folder too big -> stops before BRAM changes, the brief's message */
    wipe(); setup_a();
    sd_bup("GS-9125[U]", "GAXEDUEL_03.BUP", "GAXEDUEL_03", 30000, 6, 202, 0);
    st = state();
    CHECK(run(&k_scen[0], &r) != 0);
    CHECK_INT(r.op, OM_OP_CHECK_FITS);
    CHECK(!strncmp(r.line, "SAVES: ", 7) && strstr(r.line, " KB DO NOT FIT IN 32 KB BRAM"));
    st2 = state();
    {   /* the old BRAM back, plus Guardian Heroes' carried GHEROES_OLD */
        char *x = bram_only(st), *y = bram_only(st2);
        CHECK(!strcmp(x, y) || undo_with_carried(y, x));
        free(x); free(y);
    }
    CHECK(!sd_exists("JOURNAL.TXT"));
    free(st); free(st2);

    /* a corrupt file in the folder to load: named, before BRAM changes */
    wipe(); setup_a();
    sd_text("GS-9125[U]/BROKEN.BUP", "Vmem but far too short");
    st = state();
    CHECK(run(&k_scen[0], &r) != 0);
    CHECK_STR(r.line, "SAVES: BAD FILE BROKEN.BUP");
    st2 = state();
    {   /* the old BRAM back, plus Guardian Heroes' carried GHEROES_OLD */
        char *x = bram_only(st), *y = bram_only(st2);
        CHECK(!strcmp(x, y) || undo_with_carried(y, x));
        free(x); free(y);
    }
    free(st); free(st2);

    /* the device needs more blocks than CHECK_FITS estimated: BUP_Write runs out mid-LOAD -> undone, BRAM as before */
    wipe(); setup_a();
    fake_bram_extra = 0;
    sd_bup("GS-9125[U]", "GAXEDUEL_03.BUP", "GAXEDUEL_03", 22000, 6, 202, 0);
    st = state();
    fake_bram_extra = 60;                                                  /* 60 more blocks per record than estimated */
    CHECK(run(&k_scen[0], &r) != 0);
    CHECK_INT(r.undone, 1);
    CHECK_INT(r.op, OM_OP_LOAD_DIR_TO_BRAM);
    CHECK_STR(r.line, "BRAM: FULL");
    fake_bram_extra = 0;
    st2 = state();
    {   /* the old BRAM back, plus Guardian Heroes' carried GHEROES_OLD */
        char *x = bram_only(st), *y = bram_only(st2);
        CHECK(!strcmp(x, y) || undo_with_carried(y, x));
        free(x); free(y);
    }
    CHECK(!sd_exists("JOURNAL.TXT"));
    CHECK(active_follows_marker());
    CHECK(sd_read("ACTIVE.TXT", a, sizeof a) > 0 && !strncmp(a, GH " 0 ", 12));
    free(st); free(st2);

    /* duplicates in the folder to load: the later record date wins (SPEC §5.16) */
    wipe(); setup_a();
    sd_bup("GS-9125[U]", "GAXE_COPY.BUP", "GAXEDUEL_01", 33, 7, 999, 0);
    CHECK_INT(run(&k_scen[0], &r), 0);
    i = fake_bram_find("GAXEDUEL_01");
    CHECK(i >= 0 && fake_bram_dir(i)->datasize == 33 && fake_bram_dir(i)->date == 999);

    /* an unformatted BRAM: the launch goes on with a notice, nothing written or formatted */
    wipe(); setup_a();
    fake_bram_unformat = 1;
    CHECK_INT(run(&k_scen[0], &r), 0);
    CHECK_STR(r.notice, "CONSOLE SAVE MEMORY NOT FORMATTED - SAVES SKIPPED");
    CHECK_INT(fake_mut_n, 0);

    /* a resume names itself on the step screen, then the launch asked for next (another game) runs after it */
    wipe(); setup_a();
    fake_mut_cut = 40;
    run(&k_scen[0], &r);
    fake_reboot(); fake_mut_cut = -1;
    {
        scen other = { "A other", 0, 1, 0, "T-1234567@J" };
        CHECK_INT(run(&other, &r), 0);
        CHECK_INT(r.resumed, 1);
        CHECK(cur_exists("GS-9125[U]/GAXEDUEL_01.BUP"));                     /* Golden Axe archived after the resume */
        CHECK(sd_read("ACTIVE.TXT", a, sizeof a) > 0 && !strncmp(a, "T-1234567@J 0 ", 14));
        CHECK_INT(fake_bram_count(), 1 + 3);
        for (i = 0; i < s_norig; i++) CHECK(survived(&s_orig[i]));
    }

    /* a hand-damaged journal stops the launch before BRAM is touched */
    wipe(); setup_a();
    sd_text("JOURNAL.TXT", "op=banana\n");
    CHECK(run(&k_scen[0], &r) != 0);
    CHECK_INT(fake_bram_inits, 0);
    CHECK(!strncmp(r.line, "SAVES: JOURNAL.TXT BAD", 22));

    product_only();
    sim_c1();
    first_load();
    probe_c1();
    single_torn();
    torn_sweep();
    mkdir_fails();
    stale_marker();
    newer_on_card();
    marker_lost_twice();
    no_marker_rotations();
    kai_holds();
    kai_holds_with_journal();
    for (i = 0; i < NSCEN; i++)
        sweep(&k_scen[i]);
    for (i = 0; i < NSCEN; i++)
        foreign_sweep(&k_scen[i]);
    {
        char cmd[300];
        snprintf(cmd, sizeof cmd, "rm -rf %s", root);
        if (system(cmd)) {}
    }
    printf("R1 property: %d violations over every delete and overwrite of a record file\n", fake_r1_violations);
    CHECK_INT(fake_r1_violations, 0);
    printf("current-set property: %d violations over %d launches (every current-set record stays current or in BRAM, "
           "but a deleted one: %d drops)\n", s_cs_viol, s_cs_runs, s_cs_deleted);
    CHECK(s_cs_deleted > 0);
    printf("console-side property: %d violations over %d launches (every BRAM version stays in BRAM, a current set, "
           "or the quarantine; never replaced)\n", s_bv_viol, s_bv_runs);
    CHECK_INT(s_bv_viol, 0);
    CHECK_INT(s_cs_viol, 0);
    CHECK(s_cs_runs > 1000);
    printf("add-only property: %d violations over every delete, overwrite, move and rename-over of a record file\n", fake_addonly_violations);
    CHECK_INT(fake_addonly_violations, 0);
    {   /* option A ran: the sweeps changed into save folders (relative names only) */
        extern long fake_chdir_deep;
        if (PGS_REL) CHECK(fake_chdir_deep > 1000);
        else CHECK_INT(fake_chdir_deep, 0);
        printf("option A: %ld chdirs into a save folder\n", fake_chdir_deep);
    }
    return th_done(PGS_REL ? "test_pgsrun" : "test_pgsrun (absolute names)");
}
