/* menu/host-tests/test_pgsbak.c -- the one-time Kai/Rings import and the always-on BRAM backup
 * (menu/launcher/pgsbak.c, SPEC §5.17) over fake_satiator.c and fake_bram.c.
 * Import: the 326 real .BUP headers of Kai's release (kai_bup_headers.json via gen_cases.py; file names that are not
 * the record's, duplicates, SGC's '.' at 0x1B) laid out as Kai's and Rings' per-game folders; the Python codec's
 * duplicate winners are the oracle. Then: idempotence (IMPORT.TXT present: nothing read, nothing written), a re-run
 * with IMPORT.TXT removed (nothing copied twice), never over an existing VibeMenu save, a power cut after every
 * mutating call followed by a re-run (the end state equals the clean run's; the sources never change).
 * Backup: the generation's layout against BRAM, unchanged BRAM writes nothing, the clock in the name, a torn record
 * left out, add-only (no generation ever deleted, shard folders of 100), and a power cut after every
 * mutating call (no partial .BKP, a .TMP never deleted).
 * The long run: 1200 launches, nothing the user had is lost. Every test runs under the fake card's add-only property
 * (Tidy saves and its tests went in 0.1). BSD-3-Clause. */
/* host-only paths: a d_name (<= 255) into a bounded buffer is truncated, never overflowed */
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
#include "savegen.h"
#include "impscan.h"
#include "sd.h"
#include "smpcpad.h"
#include "cases.inc"

extern int fake_r1_on, fake_r1_violations, fake_addonly, fake_addonly_violations, fake_tidy_ok, fake_nopath;
extern int fake_dead, fake_mut_n, fake_mut_cut, fake_mut_fail, fake_mut_fail_rc;
extern int fake_bram_writes;
extern int fake_mkdir_calls;
void fake_reboot(void);
void fake_bram_reset(void);
void fake_bram_put(const char *name, const uint8_t *data, uint32_t size, uint32_t date);
void fake_bram_del(const char *name);
int fake_bram_count(void);
const om_bup_dir *fake_bram_dir(int i);
const uint8_t *fake_bram_data(int i, int *broken);
int fake_bram_find(const char *name);
int om_bram_init(uint8_t *lib, uint8_t *work);

static char root[] = "/tmp/omsat_bak_XXXXXX";
static uint32_t arena32[OM_PGS_ARENA / 4 + 4];
static om_pgs_env env;
static char said[64];
static int nsaid, ncopy;
static void say(const char *t) { strncpy(said, t, sizeof said - 1); nsaid++; if (!strncmp(t, "SAVES: COPYING", 14)) ncopy++; }

/* --- the card as text ------------------------------------------------------------------------------------------ */
typedef struct { char *s; size_t n, cap; } sb;
static void sb_add(sb *b, const char *t)
{
    size_t k = strlen(t);
    if (b->n + k + 1 > b->cap) { b->cap = (b->n + k + 1) * 2 + 64; b->s = (char *)realloc(b->s, b->cap); }
    memcpy(b->s + b->n, t, k + 1);
    b->n += k;
}
static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static void walk(const char *abs, const char *rel, char **lines, int *n, int skip_imp)
{
    DIR *d = opendir(abs);
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d))) {
        char a[900], r[900];
        struct stat st;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(a, sizeof a, "%s/%s", abs, e->d_name);
        snprintf(r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", e->d_name);
        stat(a, &st);
        if (S_ISDIR(st.st_mode)) {
            char *l = (char *)malloc(strlen(r) + 3);
            sprintf(l, "%s/", r);
            lines[(*n)++] = l;
            walk(a, r, lines, n, skip_imp);
        } else {
            static uint8_t buf[200000];
            FILE *f = fopen(a, "rb");
            size_t k = fread(buf, 1, sizeof buf, f);
            char *l = (char *)malloc(strlen(r) + 32);
            fclose(f);
            if (skip_imp && strstr(r, "IMPORT.TXT")) { free(l); continue; }
            sprintf(l, "%s:%zu:%08x", r, k, om_crc32(0, buf, (uint32_t)k));
            lines[(*n)++] = l;
        }
    }
    closedir(d);
}
static char *tree(const char *rel, int skip_imp)
{
    static char *lines[8192];
    char abs[600];
    int n = 0, i;
    sb b = { 0, 0, 0 };
    sb_add(&b, "");
    snprintf(abs, sizeof abs, "%s/%s", root, rel);
    walk(abs, "", lines, &n, skip_imp);
    qsort(lines, (size_t)n, sizeof lines[0], cmp_str);
    for (i = 0; i < n; i++) { sb_add(&b, lines[i]); sb_add(&b, "\n"); free(lines[i]); }
    return b.s;
}

static void mkdirs_abs(char *p)
{
    char *s;
    for (s = p + strlen(root) + 1; *s; s++)
        if (*s == '/') { *s = 0; mkdir(p, 0755); *s = '/'; }
    mkdir(p, 0755);
}
static void put_file(const char *rel, const uint8_t *data, size_t n)
{
    char p[900], *sl;
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    sl = strrchr(p, '/');
    *sl = 0; mkdirs_abs(p); *sl = '/';
    f = fopen(p, "wb");
    fwrite(data, 1, n, f);
    fclose(f);
}
static long get_file(const char *rel, uint8_t *out, long cap)
{
    char p[900];
    FILE *f;
    long n;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    if (!(f = fopen(p, "rb"))) return -1;
    n = (long)fread(out, 1, (size_t)cap, f);
    fclose(f);
    return n;
}
static int exists(const char *rel)
{
    char p[900];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    return !stat(p, &st);
}
static void wipe_bram_only(void) { fake_reboot(); fake_bram_reset(); }
static void wipe(void)
{
    char cmd[400];
    snprintf(cmd, sizeof cmd, "rm -rf %s/aether %s/SAVES %s/satiator-saves", root, root, root);
    if (system(cmd)) {}
    snprintf(cmd, sizeof cmd, "%s/aether", root);
    mkdir(cmd, 0755);
    fake_reboot();
    fake_bram_reset();
    fake_mut_n = 0; fake_mut_cut = -1; fake_mut_fail = -1; fake_mut_fail_rc = 0;
}

/* a .BUP file image: header + data */
static size_t bup_img(uint8_t *o, const char *name, const char *comment, uint32_t size, uint32_t seed, uint32_t date)
{
    om_bup_dir d;
    uint32_t i;
    memset(&d, 0, sizeof d);
    strncpy(d.name, name, 11);
    strncpy(d.comment, comment, 10);
    d.language = 1;
    d.date = date;
    d.datasize = size;
    om_bup_encode_hdr(&d, o);
    for (i = 0; i < size; i++) o[OM_BUP_HDR + i] = (uint8_t)(i * 13u + seed * 7u);
    return OM_BUP_HDR + size;
}
static void put_bup(const char *rel, const char *name, uint32_t size, uint32_t seed, uint32_t date)
{
    static uint8_t b[40000];
    put_file(rel, b, bup_img(b, name, "TEST", size, seed, date));
}

/* --- the corpus as Kai's and Rings' trees -------------------------------------------------------------------------
 * corpus folder f (sorted, 0..NF-1) -> its source and destination:
 *   f % 4 == 0: Kai   SAVES/BRAM/T-<f>[JU]/            -> aether/SAVES/T-<f>[JU]/
 *   f % 4 == 1: Kai   SAVES/BRAM/T-<f>/0<f%9+1>/       -> aether/SAVES/T-<f>/0<n>/   (a product-only folder, a slot)
 *   f % 4 == 2: Rings satiator-saves/t-<f>/            -> aether/SAVES/T-<f>/        (lower case: sanitized)
 *   f % 4 == 3: Rings satiator-saves/[SLOT0<n>]/T-<f>/ -> aether/SAVES/T-<f>/0<n>/ */
#define NFMAX 64
static char s_fold[NFMAX][64];
static int s_nf;
static int fold_of(const char *path)
{
    char f[300];
    int i;
    const char *sl = strrchr(path, '/');
    memcpy(f, path, (size_t)(sl - path));
    f[sl - path] = 0;
    for (i = 0; i < s_nf; i++) if (!strcmp(s_fold[i], f)) return i;
    strcpy(s_fold[s_nf], f);
    return s_nf++;
}
static void src_of(int f, char *src, char *dst)
{
    int n = f % 9 + 1;
    switch (f % 4) {
    case 0: sprintf(src, "SAVES/BRAM/T-%03d[JU]", f); sprintf(dst, "aether/SAVES/T-%03d[JU]", f); break;
    case 1: sprintf(src, "SAVES/BRAM/T-%03d/0%d", f, n); sprintf(dst, "aether/SAVES/T-%03d/0%d", f, n); break;
    case 2: sprintf(src, "satiator-saves/t-%03d", f); sprintf(dst, "aether/SAVES/T-%03d", f); break;
    default: sprintf(src, "satiator-saves/[SLOT0%d]/T-%03d", n, f); sprintf(dst, "aether/SAVES/T-%03d/0%d", f, n);
    }
}
static int loser(uint32_t k)
{
    uint32_t g, j;
    for (g = 0; g < KB_NDUP; g++)
        for (j = 0; j < KB_DUP[g].n; j++)
            if (KB_DUP[g].idx[j] == (int)k && KB_DUP[g].winner != (int)k) return 1;
    return 0;
}
static void lay_corpus(void)
{
    uint32_t k;
    static uint8_t b[200000];
    s_nf = 0;
    for (k = 0; k < KB_N; k++) {
        char src[200], dst[200], rel[400];
        int f = fold_of(KB[k].path);
        src_of(f, src, dst);
        memset(b, 0, KB[k].len);
        memcpy(b, KB[k].hdr, KB[k].len < 64 ? KB[k].len : 64);
        snprintf(rel, sizeof rel, "%s/%s", src, KB[k].file);
        put_file(rel, b, KB[k].len);
    }
    put_bup("SAVES/BRAM/_BACKUP/GENERAL_01.BUP", "GENERAL_01", 40, 1, 5);           /* Kai's general set: skipped */
    put_bup("satiator-saves/_BACKUP/GENERAL_02.BUP", "GENERAL_02", 40, 2, 5);       /* Rings': skipped */
    put_bup("satiator-saves/[SLOT12]/T-900/SLOT12_SAVE.BUP", "SLOT12_SAVE", 40, 3, 5);  /* Rings slot 12: skipped */
    put_bup("satiator-saves/T-901/SAVRING.BUP", "SAVRING", 0, 4, 1);                /* Rings' marker: reserved */
    put_file("SAVES/BRAM/T-000[JU]/SLOT.TXT", (const uint8_t *)"id=0\r\n", 6);    /* not a save */
}

/* every expected record is in its destination, byte for byte, and nothing else is */
static int s_ncart;
static int check_corpus(void)
{   /* the records for the internal memory, each once in its folder; the ones over 32 KB in its CART/ */
    uint32_t k;
    int bad = 0, want = 0, f;
    static uint8_t a[200000], c[200000];
    s_ncart = 0;
    for (k = 0; k < KB_N; k++) {
        char src[200], dst[200], rel[800], frel[800];
        char nm[12], fn[20];
        DIR *d;
        struct dirent *e;
        int found = 0;
        f = fold_of(KB[k].path);
        src_of(f, src, dst);
        if (!KB[k].ok || loser(k)) continue;
        if (KB[k].len + 4u > OM_PGS_REC) { strcat(dst, "/CART"); s_ncart++; }   /* a cartridge or FDD save */
        else want++;
        memset(nm, 0, 12);
        memcpy(nm, KB[k].name, strlen(KB[k].name));
        snprintf(rel, sizeof rel, "%s/%s", src, KB[k].file);
        long la = get_file(rel, a, sizeof a);
        snprintf(frel, sizeof frel, "%s/%s", root, dst);
        if (!(d = opendir(frel))) { bad++; continue; }
        while ((e = readdir(d))) {
            om_bup_dir dd;
            long lc;
            if (!strstr(e->d_name, ".BUP")) continue;
            snprintf(rel, sizeof rel, "%s/%s", dst, e->d_name);
            lc = get_file(rel, c, sizeof c);
            if (lc >= 64 && !om_bup_decode_hdr(c, (uint32_t)lc, &dd) && !memcmp(dd.name, nm, 12)) {
                found++;
                if (lc != la || memcmp(a, c, (size_t)la)) { bad++; printf("corpus: %s differs\n", rel); }
            }
        }
        closedir(d);
        (void)fn;
        if (found != 1) { bad++; printf("corpus: %s found %d times in %s\n", KB[k].path, found, dst); }
    }
    return bad ? -bad : want;
}

static int ndatesz(const char *rel, uint32_t *date)
{   /* a .BUP file's datasize (and date), -1 when absent or undecodable */
    static uint8_t b[40000];
    om_bup_dir d;
    long n = get_file(rel, b, sizeof b);
    if (n < 64 || om_bup_decode_hdr(b, (uint32_t)n, &d)) return -1;
    if (date) *date = d.date;
    return (int)d.datasize;
}

static int count_files(const char *rel)
{
    char p[600];
    DIR *d;
    struct dirent *e;
    int n = 0;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    if (!(d = opendir(p))) return 0;
    while ((e = readdir(d))) if (strstr(e->d_name, ".BUP")) n++;
    closedir(d);
    return n;
}

static void import_tests(void)
{
    om_pgs_imp r;
    char note[OM_PGS_NOTICE], *src0, *src1, *dst0, *dst1;
    int want, i, K, losers = 0;
    uint32_t k, dt;

    for (k = 0; k < KB_N; k++)                                         /* the corpus' duplicate losers (§5.16) */
        if (KB[k].ok && loser(k)) losers++;
    /* clean: every record once in its folder, every losing duplicate in _QUARANTINE/IMPORT, the sources unchanged */
    wipe();
    lay_corpus();
    src0 = tree("SAVES", 0);
    ncopy = 0;
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.rc, 0);
    CHECK_INT(r.ran, 1);
    want = check_corpus();
    CHECK(want > 300);
    CHECK_INT(r.kai + r.rings, want + 1);                              /* + the [SLOT12] fixture (24c) */
    CHECK(r.kai > 0 && r.rings > 0);
    CHECK_INT(r.older, losers);                                        /* losers are kept, not dropped */
    /* the corpus' saves over 32 KB are copied cart-only, counted, none skipped */
    CHECK_INT(r.cart, s_ncart);
    CHECK_INT(s_ncart, 19);
    CHECK_INT(r.skipped, 3);           /* 24c, by file: the two _BACKUP general-set files and SAVRING; [SLOT12] imported */
    {
        uint32_t kk, big = 0, maxlen = 0;
        for (kk = 0; kk < KB_N; kk++)
            if (KB[kk].ok && KB[kk].len + 4u > OM_PGS_REC) { big++; if (KB[kk].len > maxlen) maxlen = KB[kk].len; }
        CHECK_INT(big, 19);                                            /* the corpus sizes: 19 files over 32 KB */
        CHECK(maxlen > 2u * OM_PGS_REC);                               /* some exceed a whole buffer: in pieces */
        printf("corpus: %u saves over 32 KB, the largest %u B\n", big, maxlen);
    }
    CHECK(losers > 0);
    CHECK_INT(r.conflicts, 0);
    CHECK_INT(ncopy, (want + 1) / 8);                                  /* a moving count on the step screen */
    CHECK(exists("aether/SAVES/IMPORT.TXT"));
    CHECK(!exists("aether/SAVES/_BACKUP"));                          /* Kai's general set is not a game's */
    CHECK(exists("aether/SAVES/T-900/SLOT12_SAVE.BUP"));             /* Rings slot 12, the game's only saves: slot 0 (hw 2026-11-25) */
    CHECK(!exists("aether/SAVES/T-901/SAVRING.BUP"));                /* the reserved marker */
    CHECK(!exists("aether/SAVES/T-000[JU]/SLOT.TXT"));
    CHECK_STR(note, "");
    src1 = tree("SAVES", 0);
    CHECK(!strcmp(src0, src1));                                        /* the originals: not one byte changed */
    free(src1);
    {
        dst0 = tree("aether/SAVES", 1);
        K = fake_mut_n;
        printf("import: %d records copied, %d older copies kept, %u skipped, %d mutating calls\n", want, losers,
               r.skipped, K);

        /* run again (the user chose it twice): nothing copied twice, nothing quarantined twice */
        fake_mut_n = 0;
        CHECK_INT(om_pgs_import(&env, &r, note), 0);
        CHECK_INT(r.rc, 0);
        CHECK_INT(r.kai + r.rings, 0);
        CHECK_INT(r.present, want + s_ncart + 1);
        CHECK_INT(r.older, 0);
        CHECK_INT(r.cart, 0);
        dst1 = tree("aether/SAVES", 1);
        CHECK(!strcmp(dst0, dst1));
        free(dst1);
        /* a save added in Kai since: the next run copies it */
        put_bup("SAVES/BRAM/T-000[JU]/NEW_SAVE01.BUP", "NEW_SAVE01", 10, 9, 9);
        CHECK_INT(om_pgs_import(&env, &r, note), 0);
        CHECK_INT(r.kai, 1);
        CHECK(ndatesz("aether/SAVES/T-000[JU]/NEW_SAVE01.BUP", 0) == 10);
        {
            char t2[700];
            snprintf(t2, sizeof t2, "%s/SAVES/BRAM/T-000[JU]/NEW_SAVE01.BUP", root);
            remove(t2);
        }

        /* a power cut after every 7th mutating call (and the last 40), then the import run again */
        for (i = 1; i <= K; i++) {
            char *d2, *s2;
            if (i % 7 && i < K - 40) continue;
            wipe();
            lay_corpus();
            fake_mut_cut = i;
            om_pgs_import(&env, &r, note);
            fake_reboot(); fake_mut_cut = -1;
            CHECK_INT(om_pgs_import(&env, &r, note), 0);
            if (r.rc) printf("cut %d: rc %d\n", i, r.rc);
            d2 = tree("aether/SAVES", 1);
            if (!exists("aether/SAVES/IMPORT.TXT") && !exists("aether/SAVES/IMPORT.TXT.NEW")) { th_fail++; printf("FAIL import cut %d: no IMPORT.TXT\n", i); }
            s2 = tree("SAVES", 0);
            if (strcmp(d2, dst0)) { th_fail++; printf("FAIL import cut %d: end state differs\n", i); }
            else th_pass++;
            if (strcmp(s2, src0)) { th_fail++; printf("FAIL import cut %d: a source changed\n", i); }
            else th_pass++;
            free(d2); free(s2);
        }
        free(dst0);
    }
    free(src0);

    /* Kai's region folder and Rings' id folder for one game merge into the folder VibeMenu
     * reads (the region one); per record the newest intact copy wins, the other goes to _QUARANTINE/IMPORT */
    wipe();                                                            /* order 1: Kai's copy is the newer */
    put_bup("SAVES/BRAM/MK-81214[UT]/SCORCHER_01.BUP", "SCORCHER_01", 50, 1, 300);
    put_bup("satiator-saves/MK-81214/SCORCHER_01.BUP", "SCORCHER_01", 60, 2, 200);
    put_bup("satiator-saves/MK-81214/SCORCHER_02.BUP", "SCORCHER_02", 70, 3, 200);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(ndatesz("aether/SAVES/MK-81214[UT]/SCORCHER_01.BUP", &dt), 50); CHECK_INT(dt, 300);
    CHECK_INT(ndatesz("aether/SAVES/MK-81214[UT]/SCORCHER_02.BUP", 0), 70);  /* Rings' alone: merged in */
    CHECK_INT(ndatesz("aether/SAVES/_QUARANTINE/IMPORT/MK-81214[UT]/SCORCHER_01.BUP", &dt), 60); CHECK_INT(dt, 200);
    CHECK(!exists("aether/SAVES/MK-81214"));                         /* no folder VibeMenu would not read */
    CHECK_INT(r.kai, 1); CHECK_INT(r.rings, 1); CHECK_INT(r.older, 1);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);                       /* again: nothing new anywhere */
    CHECK_INT(r.kai + r.rings, 0); CHECK_INT(r.older, 0); CHECK_INT(r.present, 2);
    CHECK_INT(count_files("aether/SAVES/_QUARANTINE/IMPORT/MK-81214[UT]"), 1);
    wipe();                                                            /* order 2: Rings' copy is the newer */
    put_bup("SAVES/BRAM/MK-81214[UT]/SCORCHER_01.BUP", "SCORCHER_01", 50, 1, 300);
    put_bup("satiator-saves/MK-81214/SCORCHER_01.BUP", "SCORCHER_01", 60, 2, 400);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(ndatesz("aether/SAVES/MK-81214[UT]/SCORCHER_01.BUP", &dt), 60); CHECK_INT(dt, 400);
    CHECK_INT(ndatesz("aether/SAVES/_QUARANTINE/IMPORT/MK-81214[UT]/SCORCHER_01.BUP", &dt), 50); CHECK_INT(dt, 300);
    CHECK_INT(r.kai, 0); CHECK_INT(r.rings, 1); CHECK_INT(r.older, 1);
    wipe();                                                            /* the same version in both: one copy, no loser */
    put_bup("SAVES/BRAM/MK-81214[UT]/SCORCHER_01.BUP", "SCORCHER_01", 50, 1, 300);
    put_bup("satiator-saves/MK-81214/SCORCHER_01.BUP", "SCORCHER_01", 50, 1, 300);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.kai + r.rings, 1); CHECK_INT(r.older, 0);
    CHECK(!exists("aether/SAVES/_QUARANTINE"));
    wipe();                                                            /* Kai's own tree with both (its live saves in the
                                                                        * product folder, pseudo_bram.c:1255-1259) */
    put_bup("SAVES/BRAM/MK-81214/SCORCHER_01.BUP", "SCORCHER_01", 50, 1, 500);
    put_bup("SAVES/BRAM/MK-81214[UT]/SCORCHER_01.BUP", "SCORCHER_01", 40, 2, 100);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(ndatesz("aether/SAVES/MK-81214[UT]/SCORCHER_01.BUP", &dt), 50); CHECK_INT(dt, 500);
    CHECK(!exists("aether/SAVES/MK-81214"));
    wipe();                                                            /* VibeMenu already has the region folder: Rings'
                                                                        * product folder merges into it */
    put_bup("aether/SAVES/MK-81214[UT]/SCORCHER_02.BUP", "SCORCHER_02", 30, 1, 100);
    put_bup("satiator-saves/MK-81214/SCORCHER_01.BUP", "SCORCHER_01", 60, 2, 200);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(ndatesz("aether/SAVES/MK-81214[UT]/SCORCHER_01.BUP", 0), 60);
    CHECK(!exists("aether/SAVES/MK-81214"));
    wipe();                                                            /* Rings alone: its product folder (the launcher
                                                                        * reads it while no region folder exists) */
    put_bup("satiator-saves/mk-81214/SCORCHER_01.BUP", "SCORCHER_01", 60, 2, 200);
    put_bup("satiator-saves/[SLOT03]/MK-81214/SCORCHER_09.BUP", "SCORCHER_09", 20, 3, 200);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(ndatesz("aether/SAVES/MK-81214/SCORCHER_01.BUP", 0), 60);
    CHECK_INT(ndatesz("aether/SAVES/MK-81214/03/SCORCHER_09.BUP", 0), 20);

    /* never over a VibeMenu save: a different version already there is a conflict, left as it is */
    wipe();
    put_bup("aether/SAVES/MK-81214[UT]/SCORCHER_01.BUP", "SCORCHER_01", 30, 1, 100);
    put_bup("SAVES/BRAM/MK-81214[UT]/SCORCHER_01.BUP", "SCORCHER_01", 50, 2, 200);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(ndatesz("aether/SAVES/MK-81214[UT]/SCORCHER_01.BUP", &dt), 30); CHECK_INT(dt, 100);
    CHECK_INT(r.conflicts, 1); CHECK_INT(r.kai, 0);

    /* no Kai or Rings folder at all: IMPORT.TXT written, no notice */
    wipe();
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.rc, 0);
    CHECK(exists("aether/SAVES/IMPORT.TXT"));
    CHECK_STR(note, "");

    /* a failing SD: a notice, no IMPORT.TXT; the next run finishes */
    wipe();
    put_bup("SAVES/BRAM/MK-81214/SCORCHER_02.BUP", "SCORCHER_02", 50, 3, 200);
    fake_mut_fail = 1; fake_mut_fail_rc = -FR_DISK_ERR;
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    fake_mut_fail = -1;
    CHECK(r.rc != 0);
    CHECK(!strncmp(note, "IMPORT STOPPED rc=", 18));
    CHECK(!exists("aether/SAVES/IMPORT.TXT"));
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.rc, 0);
    CHECK(exists("aether/SAVES/MK-81214/SCORCHER_02.BUP"));
}

/* --- backup ----------------------------------------------------------------------------------------------------- */
static uint32_t g32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static uint32_t seq_of(const char *rel)
{
    const char *b = strrchr(rel, '/');
    return (uint32_t)atoi(b ? b + 1 : rel);
}
static int cmp_seq(const void *a, const void *b)
{
    uint32_t x = seq_of((const char *)a), y = seq_of((const char *)b);
    return x < y ? -1 : x > y;
}
static int gens_in(const char *p, const char *pre, char names[][64], int cap, int n, int *tmp)
{
    DIR *d;
    struct dirent *e;
    if (!(d = opendir(p))) return n;
    while ((e = readdir(d))) {
        if (strstr(e->d_name, ".TMP")) (*tmp)++;
        if (!strstr(e->d_name, ".BKP") || n >= cap) continue;
        snprintf(names[n++], 64, "%s%s", pre, e->d_name);
    }
    closedir(d);
    return n;
}
static int gens(char names[][64], int cap, int *tmp)
{   /* the complete generations, by seq: "sss/<file>" in a shard (M6 Task 24b), "<file>" in the flat layout */
    char p[600], q[700], pre[8];
    DIR *d;
    struct dirent *e;
    int n = 0;
    *tmp = 0;
    snprintf(p, sizeof p, "%s/aether/SAVES/_BACKUP", root);
    n = gens_in(p, "", names, cap, n, tmp);
    if (!(d = opendir(p))) return n;
    while ((e = readdir(d)))
        if (strlen(e->d_name) == 3 && e->d_name[0] >= '0' && e->d_name[0] <= '9') {
            snprintf(q, sizeof q, "%s/%s", p, e->d_name);
            snprintf(pre, sizeof pre, "%s/", e->d_name);
            n = gens_in(q, pre, names, cap, n, tmp);
        }
    closedir(d);
    qsort(names, (size_t)n, 64, cmp_seq);
    return n;
}

/* a generation file checked whole: magic, count, body CRC, every image decodes; *out = "name:size:crc;..." */
static int gen_ok(const char *file, char *out)
{
    static uint8_t b[200000];
    char rel[300];
    long n;
    uint32_t cnt, body, k, off;
    snprintf(rel, sizeof rel, "aether/SAVES/_BACKUP/%s", file);
    n = get_file(rel, b, sizeof b);
    if (n < 64 || memcmp(b, "OMSATBKP", 8)) return 0;
    cnt = ((uint32_t)b[10] << 8) | b[11];
    body = g32(b + 12);
    off = 64 + cnt * 32;
    if ((uint32_t)n != off + body || om_crc32(0, b + off, body) != g32(b + 16)) return 0;
    out[0] = 0;
    for (k = 0; k < cnt; k++) {
        om_bup_dir d;
        char t[80];
        if (om_bup_decode_hdr(b + off, (uint32_t)n - off, &d)) return 0;
        if (memcmp(d.name, b + 64 + k * 32, 12) || d.datasize != g32(b + 64 + k * 32 + 28)) return 0;
        sprintf(t, "%s:%u:%08x;", d.name, d.datasize, om_crc32(0, b + off + 64, d.datasize));
        strcat(out, t);
        off += 64 + d.datasize + ((4 - (d.datasize & 3)) & 3);
    }
    return 1;
}

static void bram_text(char *out)
{   /* BRAM as gen_ok lists it: every record but the marker, BRAM order */
    int i;
    out[0] = 0;
    for (i = 0; i < fake_bram_count(); i++) {
        int br;
        const om_bup_dir *d = fake_bram_dir(i);
        const uint8_t *p = fake_bram_data(i, &br);
        char t[80];
        if (!strcmp(d->name, OM_PGS_MARKER) || br) continue;
        sprintf(t, "%s:%u:%08x;", d->name, d->datasize, om_crc32(0, p, d->datasize));
        strcat(out, t);
    }
}

static uint8_t s_rb[40000];
static void brec(const char *name, uint32_t size, uint32_t seed, uint32_t date)
{
    uint32_t i;
    for (i = 0; i < size; i++) s_rb[i] = (uint8_t)(i * 31u + seed);
    fake_bram_del(name);
    fake_bram_put(name, s_rb, size, date);
}

static void backup_tests(void)
{
    om_pgs_bak r;
    char note[OM_PGS_MSG], g[64][64], a[8000], b[8000], hdr[64];
    int n, tmp, i, k, K;
    static const char marker[] = "GS-9031@U 0 0000A001\n";

    /* pure: names, seqs, folders, Rings slots, the clock */
    CHECK_INT(om_pgs_bak_name(a, sizeof a, 12, "20261122-053000", ".BKP"), 0);
    CHECK_STR(a, "00012-20261122-053000.BKP");
    CHECK_INT(om_pgs_bak_name(a, sizeof a, 7, "", ".TMP"), 0);
    CHECK_STR(a, "00007.TMP");
    CHECK_INT(om_pgs_bak_name(a, sizeof a, 0, "", ".BKP"), OM_E_FIELD);
    CHECK_INT(om_pgs_bak_name(a, sizeof a, 100000, "", ".BKP"), OM_E_FIELD);
    CHECK_INT(om_pgs_bak_seq("00012-20261122-053000.BKP", &k), 12); CHECK_INT(k, 1);
    CHECK_INT(om_pgs_bak_seq("00007.TMP", &k), 7); CHECK_INT(k, 0);
    CHECK_INT(om_pgs_bak_seq("00007.BUP", &k), 0);
    CHECK_INT(om_pgs_bak_seq("0007.BKP", &k), 0);
    CHECK_INT(om_pgs_bak_seq("00000.BKP", &k), 0);
    CHECK_INT(om_pgs_imp_folder("MK-81214[UT]", a), 0); CHECK_STR(a, "MK-81214[UT]");
    CHECK_INT(om_pgs_imp_folder("mk-81214", a), 0); CHECK_STR(a, "MK-81214");
    CHECK_INT(om_pgs_imp_folder("T.1234", a), 0); CHECK_STR(a, "T_1234");
    CHECK_INT(om_pgs_imp_folder("_BACKUP", a), OM_E_FIELD);
    CHECK_INT(om_pgs_imp_folder("[SLOT01]", a), OM_E_FIELD);
    CHECK_INT(om_pgs_imp_folder("ABCDEFGHIJK", a), OM_E_FIELD);                  /* 11: over Kai's 10 */
    CHECK_INT(om_pgs_imp_folder("MK-1[ut]", a), OM_E_FIELD);
    CHECK_INT(om_pgs_imp_folder("MK-1[ABCDEFGHI]", a), OM_E_FIELD);              /* 9 areas: over 8 */
    CHECK_INT(om_pgs_rings_slot("[SLOT03]"), 3);
    CHECK_INT(om_pgs_rings_slot("[SLOT12]"), 12);
    CHECK_INT(om_pgs_rings_slot("[SLOT3]"), 0);
    {
        uint8_t o[8] = { 0x80, 0x20, 0x26, 0x0B, 0x22, 0x05, 0x30, 0x59 };
        CHECK_INT(om_rtc_fmt(o, a), 0);
        CHECK_STR(a, "20261122-053059");
        o[0] = 0x00;                                                             /* STE 0: never set */
        CHECK_INT(om_rtc_fmt(o, a), OM_SMPC_RTC_UNSET); CHECK_STR(a, "");
        o[0] = 0x80; o[3] = 0x0D;                                                /* month 13 */
        CHECK_INT(om_rtc_fmt(o, a), OM_SMPC_RTC_UNSET);
        o[3] = 0x0B; o[7] = 0x5A;                                                /* not BCD */
        CHECK_INT(om_rtc_fmt(o, a), OM_SMPC_RTC_UNSET);
    }

    /* one generation holds BRAM (every record but the marker), the clock in its name, the marker in its header */
    wipe();
    om_bram_init(env.lib, env.work);
    brec("GHEROES_001", 10, 1, 100); brec("GHEROES_002", 3001, 2, 101); brec("PSEUDO__SAT", 300, 3, 7);
    fake_bram_put(OM_PGS_MARKER, (const uint8_t *)marker, (uint32_t)strlen(marker), 0);
    CHECK_INT(om_pgs_backup(&env, "20261122-053000", &r, note), 0);
    CHECK_INT(r.rc, 0); CHECK_INT(r.written, 1); CHECK_INT(r.records, 3); CHECK_INT(r.seq, 1);
    CHECK_STR(note, "");
    n = gens(g, 64, &tmp);
    CHECK_INT(n, 1); CHECK_INT(tmp, 0);
    CHECK_STR(g[0], "000/00001-20261122-053000.BKP");                          /* shard 000 */
    CHECK(gen_ok(g[0], a));
    bram_text(b);
    CHECK_STR(a, b);
    get_file("aether/SAVES/_BACKUP/000/00001-20261122-053000.BKP", (uint8_t *)hdr, 64);
    CHECK(!memcmp(hdr + 40, "GS-9031@U 0 0000A001", 20));
    CHECK(!memcmp(hdr + 24, "20261122-053000", 15));
    CHECK_INT(fake_bram_writes, 0);                                              /* copy-only */
    {   /* for tools/tests/test_bramback.py: the Python builder must give these bytes from the same records */
        static uint8_t f[20000];
        long fl = get_file("aether/SAVES/_BACKUP/000/00001-20261122-053000.BKP", f, sizeof f);
        FILE *o = fopen("../../build/host-tests/c_gen.bkp", "wb");
        if (o) { fwrite(f, 1, (size_t)fl, o); fclose(o); }
    }

    /* unchanged BRAM: nothing written */
    fake_mut_n = 0;
    CHECK_INT(om_pgs_backup(&env, "20261122-060000", &r, note), 0);
    CHECK_INT(r.same, 1); CHECK_INT(r.written, 0); CHECK_INT(fake_mut_n, 0);
    /* a byte changed, same name, date and size: a new generation (the body CRC sees bytes) */
    s_rb[5] ^= 1;
    fake_bram_del("GHEROES_002"); fake_bram_put("GHEROES_002", s_rb, 3001, 101);
    CHECK_INT(om_pgs_backup(&env, "", &r, note), 0);
    CHECK_INT(r.written, 1); CHECK_INT(r.seq, 2);
    n = gens(g, 64, &tmp);
    CHECK_INT(n, 2); CHECK_STR(g[1], "000/00002.BKP");
    /* a torn record is left out and counted */
    {
        extern void fake_bram_tear(const char *name);
        fake_bram_tear("GHEROES_001");
        CHECK_INT(om_pgs_backup(&env, "", &r, note), 0);
        CHECK_INT(r.torn, 1); CHECK_INT(r.records, 2);
    }

    /* add-only -- 130 launches, each a different BRAM: 130 generations, none deleted or written over (the
     * fake card's add-only property), in shards of 100 (_BACKUP/000: 1-99, 001: 100-130), so no listing holds more */
    wipe();
    om_bram_init(env.lib, env.work);
    for (i = 0; i < 130; i++) {
        brec("ROLLING_SAV", 100, (uint32_t)i, 1000u + (uint32_t)i);
        brec("DATE_ZERO", 50, (uint32_t)i, 0);                                   /* a game that never sets a date */
        if (i == 0) brec("ONLY_ONCE", 20, 1, 5);
        if (i == 1) fake_bram_del("ONLY_ONCE");
        CHECK_INT(om_pgs_backup(&env, "", &r, note), 0);
        CHECK_INT(r.rc, 0);
        CHECK_INT(r.written, 1);
    }
    {
        static char gg[256][64];
        n = gens(gg, 256, &tmp);
        CHECK_INT(n, 130);
        CHECK_STR(gg[0], "000/00001.BKP");                                       /* ONLY_ONCE's copy: still there */
        CHECK_STR(gg[129], "001/00130.BKP");
        for (i = 0; i < n; i++) CHECK(gen_ok(gg[i], a));
    }
    CHECK(exists("aether/SAVES/_BACKUP/000/00099.BKP") && exists("aether/SAVES/_BACKUP/001/00100.BKP"));
    CHECK(exists("aether/SAVES/_BACKUP/001/00130.BKP") && !exists("aether/SAVES/_BACKUP/000/00100.BKP"));
    /* the newest generation's check reads one shard: unchanged BRAM writes nothing */
    fake_mut_n = 0;
    CHECK_INT(om_pgs_backup(&env, "", &r, note), 0);
    CHECK_INT(r.same, 1); CHECK_INT(r.seq, 130); CHECK_INT(fake_mut_n, 0);

    /* a power cut after every mutating call of a backup: no partial .BKP, a .TMP never deleted, the next launch
     * writes a complete generation */
    wipe();
    om_bram_init(env.lib, env.work);
    for (i = 0; i < 8; i++) { char nm[16]; sprintf(nm, "BIGSAVE_%02d", i & 15); brec(nm, 3000, 50u + (uint32_t)i, 7); }
    for (i = 0; i < 17; i++) { brec("ROLLING_SAV", 3000, (uint32_t)i, 1000u + (uint32_t)i); om_pgs_backup(&env, "", &r, note); }
    {
        char cmd[700];
        snprintf(cmd, sizeof cmd, "rm -rf %s/keep && cp -r %s/aether %s/keep", root, root, root);
        if (system(cmd)) {}
        brec("ROLLING_SAV", 3000, 99, 2000);
        fake_mut_n = 0;
        om_pgs_backup(&env, "", &r, note);
        K = fake_mut_n;
        CHECK_INT(r.written, 1);
        for (k = 1; k <= K; k++) {
            int tmp2, j, ok = 1;
            snprintf(cmd, sizeof cmd, "rm -rf %s/aether && cp -r %s/keep %s/aether", root, root, root);
            if (system(cmd)) {}
            fake_reboot();
            fake_mut_cut = k;
            om_pgs_backup(&env, "", &r, note);
            fake_reboot(); fake_mut_cut = -1;
            n = gens(g, 64, &tmp);
            for (j = 0; j < n; j++) if (!gen_ok(g[j], a)) { ok = 0; printf("FAIL cut %d: %s partial\n", k, g[j]); }
            CHECK_INT(om_pgs_backup(&env, "", &r, note), 0);
            CHECK_INT(r.rc, 0);
            n = gens(g, 64, &tmp2);
            CHECK(tmp2 == tmp);                                                  /* a .TMP stays */
            CHECK(gen_ok(g[n - 1], a));
            bram_text(b);
            if (strcmp(a, b)) ok = 0;
            if (ok) th_pass++; else { th_fail++; printf("FAIL backup cut %d of %d\n", k, K); }
        }
        printf("backup: %d mutating calls swept\n", K);
    }
}

/* the save swap with the backup on: the generation holds BRAM as the last session left it, before any change; and
 * with per-game saves off and no session on the card, the backup reads BRAM and writes nothing to it */
static void cart_never_loaded(void)
{   /* a slot's CART/ sub-folder is never loaded into the internal memory */
    om_pgs_result r;
    wipe();
    put_bup("aether/SAVES/T-0000[U]/SMALLSAV_01.BUP", "SMALLSAV_01", 100, 1, 10);
    put_bup("aether/SAVES/T-0000[U]/CART/BIGSAVE_01.BUP", "BIGSAVE_01", 39000, 2, 10);
    CHECK_INT(om_pgs_run(&env, 2, 0, "T-0000@U", &r), 0);
    CHECK(fake_bram_find("SMALLSAV_01") >= 0);
    CHECK(fake_bram_find("BIGSAVE_01") < 0);
    CHECK(exists("aether/SAVES/T-0000[U]/CART/BIGSAVE_01.BUP"));
}

static void swap_tests(void)
{
    cart_never_loaded();
    om_pgs_result r;
    char g[64][64], a[8000];
    int n, tmp;
    static const char marker[] = "GS-9031@U 0 0000A001\n";
    wipe();
    env.backup = 1;
    env.date = "";
    fake_bram_put("GHEROES_001", (const uint8_t *)"0123456789", 10, 100);
    fake_bram_put(OM_PGS_MARKER, (const uint8_t *)marker, (uint32_t)strlen(marker), 0);
    put_file("aether/SAVES/ACTIVE.TXT", (const uint8_t *)"GS-9031@U 0 0000A001 00000000\n", 30);
    CHECK_INT(om_pgs_run(&env, 1, 0, "GS-9125@U", &r), 0);
    CHECK_INT(r.bak_written, 1);
    n = gens(g, 64, &tmp);
    CHECK_INT(n, 1);
    CHECK(gen_ok(g[0], a));
    CHECK(!strncmp(a, "GHEROES_001:10:", 15) && strlen(a) == 24);
    CHECK(fake_bram_find("GHEROES_001") < 0);                                     /* the swap ran after it */
    /* the gate: saves off, no ACTIVE.TXT, no journal -> the backup's reads only */
    wipe();
    fake_bram_put("SAKURA_0001", (const uint8_t *)"abc", 3, 5);
    CHECK_INT(om_pgs_run(&env, 0, 0, "GS-9031@U", &r), 0);
    CHECK_INT(fake_bram_writes, 0);
    CHECK_INT(r.bak_written, 1);
    CHECK_INT(r.ran, 1);
    /* an unformatted BRAM with saves off: a notice, the launch goes on */
    wipe();
    {
        extern int fake_bram_unformat;
        fake_bram_unformat = 1;
        CHECK_INT(om_pgs_run(&env, 0, 0, "GS-9031@U", &r), 0);
        CHECK_STR(r.notice, "CONSOLE SAVE MEMORY NOT FORMATTED - SAVES SKIPPED");
        /* per-game saves on (the default) -- the launch goes on, nothing is written to BRAM
         * or the card, nothing is formatted (no BUP_Format call exists in the executor) */
        wipe();
        fake_bram_unformat = 1;
        put_file("aether/SAVES/ACTIVE.TXT", (const uint8_t *)"GS-9031@U 0 0000A001 00000000\n", 30);
        CHECK_INT(om_pgs_run(&env, 2, 0, "GS-9125@U", &r), 0);
        CHECK_STR(r.notice, "CONSOLE SAVE MEMORY NOT FORMATTED - SAVES SKIPPED");
        CHECK_INT(fake_bram_writes, 0);
        CHECK_INT(fake_mut_n, 0);
        CHECK(!exists("aether/SAVES/_BACKUP"));
        CHECK(!exists("aether/SAVES/JOURNAL.TXT"));
        fake_bram_unformat = 0;
    }
    env.backup = 0;
}

/* --- fix round 1: the long run. 60 games, random play (LCG), 1,200 launches through the real save
 * swap with the backup on (Share, slot 0). Every launch: rc 0 and the backup's rc 0; the generation count stays bounded;
 * and no only copy is lost: the newest version of every game's save (and of the general save) is somewhere on the card
 * -- a game folder, _GENERAL, the quarantine or a generation's body -- or in BRAM, byte for byte. ---------------------- */
static uint32_t s_lcg = 12345;
static uint32_t lcg(void) { s_lcg = s_lcg * 1103515245u + 12345u; return (s_lcg >> 16) & 0x7FFF; }

typedef struct { uint32_t date, size, crc; } ver_t;
static ver_t s_ver[61];                                  /* [60] = the general save */

static int found_in(const uint8_t *b, long n, const char *nm, const ver_t *v)
{   /* a .BUP image or a .BKP body holding record nm at version v */
    long off;
    for (off = 0; off + 64 <= n; off++) {
        om_bup_dir d;
        if (memcmp(b + off, "Vmem", 4) || om_bup_decode_hdr(b + off, (uint32_t)(n - off), &d)) continue;
        if (!strcmp(d.name, nm) && d.date == v->date && d.datasize == v->size &&
            om_crc32(0, b + off + 64, d.datasize) == v->crc)
            return 1;
    }
    return 0;
}

static int on_card(const char *abs, const char *nm, const ver_t *v)
{
    DIR *d = opendir(abs);
    struct dirent *e;
    int hit = 0;
    if (!d) return 0;
    while (!hit && (e = readdir(d))) {
        char a[900];
        struct stat st;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(a, sizeof a, "%s/%s", abs, e->d_name);
        stat(a, &st);
        if (S_ISDIR(st.st_mode)) hit = on_card(a, nm, v);
        else if (strstr(e->d_name, ".BUP") || strstr(e->d_name, ".BKP")) {
            static uint8_t b[200000];
            FILE *f = fopen(a, "rb");
            long n = (long)fread(b, 1, sizeof b, f);
            fclose(f);
            hit = found_in(b, n, nm, v);
        }
    }
    closedir(d);
    return hit;
}

static int in_bram(const char *nm, const ver_t *v)
{
    int i = fake_bram_find(nm), br;
    const uint8_t *p;
    if (i < 0) return 0;
    p = fake_bram_data(i, &br);
    return !br && fake_bram_dir(i)->date == v->date && fake_bram_dir(i)->datasize == v->size &&
           om_crc32(0, p, v->size) == v->crc;
}

/* the current set of a save folder, by the reader rule (an independent copy of savegen.h's): its newest complete
 * generation G<sss>/G<nnnnn> (DONE.TXT in it), else the folder */
static void cur_folder(const char *abs, char *out)
{
    DIR *d, *d2;
    struct dirent *e, *e2;
    int best = 0;
    strcpy(out, abs);
    if (!(d = opendir(abs))) return;
    while ((e = readdir(d)))
        if (e->d_name[0] == 'G' && strlen(e->d_name) == 4) {
            char q[900];
            snprintf(q, sizeof q, "%s/%s", abs, e->d_name);
            if (!(d2 = opendir(q))) continue;
            while ((e2 = readdir(d2))) {
                char x[1000];
                struct stat st;
                int v;
                if (e2->d_name[0] != 'G' || strlen(e2->d_name) != 6) continue;
                v = atoi(e2->d_name + 1);
                snprintf(x, sizeof x, "%s/%s/DONE.TXT", q, e2->d_name);
                if (v > best && !stat(x, &st)) { best = v; snprintf(out, 900, "%s/%s", q, e2->d_name); }
            }
            closedir(d2);
        }
    closedir(d);
}
/* M6 Task 24b: savegen.h's reader rule -- the newest COMPLETE generation across shards, an incomplete one above it
 * ignored, an empty shard, none at all */
static void savegen_tests(void)
{
    static om_gen_scr gs;
    char out[300], p[300];
    uint32_t g, max;
    const char *d = "/aether/SAVES/T-00042[U]";
    wipe();
    om_gen_dir(p, d, 7);
    CHECK_STR(p, "/aether/SAVES/T-00042[U]/G000/G00007");
    om_gen_dir(p, d, 12345);
    CHECK_STR(p, "/aether/SAVES/T-00042[U]/G123/G12345");
    CHECK_INT(om_gen_num("G123", 3), 123); CHECK_INT(om_gen_num("G12345", 5), 12345);
    CHECK_INT(om_gen_num("G12", 3), -1); CHECK_INT(om_gen_num("01", 3), -1); CHECK_INT(om_gen_num("G1234", 3), -1);
    CHECK_INT(om_gen_cur(d, out, &g, &gs), 0);                         /* absent: the folder itself */
    CHECK_INT(g, 0); CHECK_STR(out, d);
    CHECK_INT(om_gen_max(d, &max, &gs), 0); CHECK_INT(max, 0);
    put_bup("aether/SAVES/T-00042[U]/OLD_SAV.BUP", "OLD_SAV", 10, 1, 1);
    CHECK_INT(om_gen_cur(d, out, &g, &gs), 0); CHECK_INT(g, 0);        /* its own files: no generation yet */
    put_file("aether/SAVES/T-00042[U]/G000/G00003/DONE.TXT", (const uint8_t *)"", 0);
    put_file("aether/SAVES/T-00042[U]/G000/G00099/DONE.TXT", (const uint8_t *)"", 0);
    put_bup("aether/SAVES/T-00042[U]/G001/G00100/A_SAV.BUP", "A_SAV", 10, 1, 1);   /* incomplete: no DONE.TXT */
    CHECK_INT(om_gen_cur(d, out, &g, &gs), 0);
    CHECK_INT(g, 99); CHECK_STR(out, "/aether/SAVES/T-00042[U]/G000/G00099");     /* the shard below */
    CHECK_INT(om_gen_max(d, &max, &gs), 0); CHECK_INT(max, 100);
    put_file("aether/SAVES/T-00042[U]/G001/G00100/DONE.TXT", (const uint8_t *)"", 0);
    CHECK_INT(om_gen_cur(d, out, &g, &gs), 0); CHECK_INT(g, 100);
    mkdir((snprintf(p, sizeof p, "%s/aether/SAVES/T-00042[U]/G002", root), p), 0755);   /* an empty shard on top */
    CHECK_INT(om_gen_cur(d, out, &g, &gs), 0); CHECK_INT(g, 100);
    CHECK_INT(om_gen_max(d, &max, &gs), 0); CHECK_INT(max, 199);       /* the next generation goes into it */
    mkdir((snprintf(p, sizeof p, "%s/aether/SAVES/T-00042[U]/G001/G00150", root), p), 0755);
    mkdir((snprintf(p, sizeof p, "%s/aether/SAVES/T-00042[U]/G001/X00160", root), p), 0755);   /* not one */
    CHECK_INT(om_gen_cur(d, out, &g, &gs), 0); CHECK_INT(g, 100);
}

/* the reviewer's sim I6 (task-24-review.md I6), ported. T-1111@U is archived to G00001 and is the game
 * BRAM holds; Settings > Import adds Kai's KAISAVE_001 into that current set. RED before the fix: the next launch of
 * T-1111 (case 1) loads nothing, and the following archive writes G00002 without KAISAVE_001 -- stranded. Now: case 1
 * writes it into BRAM (reconcile), and an archive carries it into the new generation (carry_cur). */
static int cur_has(const char *rel_dir, const char *file)
{
    char abs[600], c[900], q[1000];
    struct stat st;
    snprintf(abs, sizeof abs, "%s/%s", root, rel_dir);
    cur_folder(abs, c);
    snprintf(q, sizeof q, "%s/%s", c, file);
    return !stat(q, &st);
}
static void i6_card(void)
{   /* T-1111 archived to G00001 and back in BRAM (the marker), then Kai's save imported */
    om_pgs_result pr;
    om_pgs_imp r;
    char note[OM_PGS_NOTICE];
    wipe();
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-1111@U", &pr), 0);          /* case 3: T-1111 in, empty */
    brec("T1111_SAV01", 100, 1, 500);                                /* played */
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-2222@U", &pr), 0);          /* T-1111 archived: G00001 */
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-1111@U", &pr), 0);          /* back in BRAM */
    CHECK(exists("aether/SAVES/T-1111[U]/G000/G00001/DONE.TXT"));
    CHECK(fake_bram_find("T1111_SAV01") >= 0);
    put_bup("SAVES/BRAM/T-1111[U]/KAISAVE_001.BUP", "KAISAVE_001", 200, 9, 700);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.rc, 0); CHECK_INT(r.kai, 1);
    CHECK(cur_has("aether/SAVES/T-1111[U]", "KAISAVE_001.BUP"));    /* into the current set */
}
/* M6 Task 24b fix (in-game deletes stick). T-1111 holds two saves; back in BRAM, the game deletes one. RED
 * before: the relaunch (case 1) and the next archive both brought it back. Now it stays deleted -- its file still in
 * the folder's history (G00001) -- and an import of another version of it afterwards is restored. The general set's
 * deletes stick too. */
static int hist_has(const char *rel, const char *name, uint32_t date)
{   /* a .BUP of record name at that date anywhere under /aether/<rel> */
    char cmd[700], line[800];
    FILE *p;
    int hit = 0;
    snprintf(cmd, sizeof cmd, "find '%s/%s' -iname '*.BUP' 2>/dev/null", root, rel);
    if (!(p = popen(cmd, "r"))) return 0;
    while (!hit && fgets(line, sizeof line, p)) {
        static uint8_t b[70000];
        om_bup_dir d;
        FILE *f;
        long n;
        line[strcspn(line, "\n")] = 0;
        if (!(f = fopen(line, "rb"))) continue;
        n = (long)fread(b, 1, sizeof b, f);
        fclose(f);
        hit = !om_bup_decode_hdr(b, (uint32_t)n, &d) && !strcmp(d.name, name) && d.date == date;
    }
    pclose(p);
    return hit;
}
static void in_game_delete(void)
{
    om_pgs_result pr;
    om_pgs_imp r;
    char note[OM_PGS_NOTICE];
    int i;
    wipe();
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-1111@U", &pr), 0);
    brec("T1111_KEEP1", 100, 1, 500);
    brec("T1111_GONE1", 100, 2, 501);
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-2222@U", &pr), 0);          /* both archived: G00001 */
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-1111@U", &pr), 0);          /* loaded: ACTIVE.TXT lists both versions */
    CHECK(fake_bram_find("T1111_GONE1") >= 0);
    fake_bram_del("T1111_GONE1");                                    /* the game deletes it */
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-1111@U", &pr), 0);          /* relaunch (case 1) */
    CHECK(fake_bram_find("T1111_GONE1") < 0);                       /* RED before: restored */
    CHECK(fake_bram_find("T1111_KEEP1") >= 0);
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-2222@U", &pr), 0);          /* archived without it */
    CHECK(!cur_has("aether/SAVES/T-1111[U]", "T1111_GONE1.BUP"));  /* RED before: carried */
    CHECK(cur_has("aether/SAVES/T-1111[U]", "T1111_KEEP1.BUP"));
    CHECK(hist_has("aether/SAVES/T-1111[U]", "T1111_GONE1", 501)); /* still in the card's history */
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-1111@U", &pr), 0);
    CHECK(fake_bram_find("T1111_GONE1") < 0);
    /* an import of another version afterwards: restored at the relaunch */
    put_bup("SAVES/BRAM/T-1111[U]/T1111_GONE1.BUP", "T1111_GONE1", 120, 7, 900);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.kai, 1);
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-1111@U", &pr), 0);
    CHECK((i = fake_bram_find("T1111_GONE1")) >= 0 && fake_bram_dir(i)->date == 900);
    /* and the game deletes the restored copy: that sticks too (ACTIVE.TXT now lists it) */
    fake_bram_del("T1111_GONE1");
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-1111@U", &pr), 0);
    CHECK(fake_bram_find("T1111_GONE1") < 0);
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-2222@U", &pr), 0);
    CHECK(!cur_has("aether/SAVES/T-1111[U]", "T1111_GONE1.BUP") && !cur_has("aether/SAVES/T-1111[U]", "T1111_GONE1~1.BUP"));
    /* the general set: saves off (case 4 gives it back), a general save deleted, then a per-game launch (case 3) */
    wipe();
    brec("GEN_KEEP_01", 50, 3, 10);
    brec("GEN_GONE_01", 50, 4, 11);
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-1111@U", &pr), 0);          /* the general set archived to _GENERAL */
    CHECK_INT(om_pgs_run(&env, 0, 0, "T-1111@U", &pr), 0);          /* saves off: the general set back */
    CHECK(fake_bram_find("GEN_GONE_01") >= 0);
    fake_bram_del("GEN_GONE_01");                                    /* deleted in the memory manager */
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-1111@U", &pr), 0);
    CHECK(!cur_has("aether/SAVES/_GENERAL", "GEN_GONE_01.BUP"));
    CHECK(hist_has("aether/SAVES/_GENERAL", "GEN_GONE_01", 11));
    CHECK_INT(om_pgs_run(&env, 0, 0, "T-1111@U", &pr), 0);
    CHECK(fake_bram_find("GEN_GONE_01") < 0 && fake_bram_find("GEN_KEEP_01") >= 0);
}

/* the reviewer's sim C2 (task-24-review.md "Re-review (24b fixes)"), ported. X archived with
 * XSAVE_00001 V1 dated 2000, loaded; the console clock was reset, so the player's newer V2 is dated 100. RED before:
 * the relaunch wrote V1 over V2 (the date ranked the card copy first). Now the session's loaded version never wins
 * over BRAM: V2 stays, the next archive makes V2 current, V1 stays in history. And with no ACTIVE.TXT list (an older
 * card) the console's copy wins too. */
static void sim_c2(void)
{
    om_pgs_result pr;
    int i, pass;
    for (pass = 0; pass < 2; pass++) {
        wipe();
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-4444@U", &pr), 0);
        brec("XSAVE_00001", 100, 1, 2000);                            /* V1 */
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-5555@U", &pr), 0);       /* archived: G00001 */
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-4444@U", &pr), 0);       /* loaded */
        brec("XSAVE_00001", 100, 2, 100);                             /* V2, the clock reset */
        if (pass) {                                                    /* an ACTIVE.TXT from before the list */
            uint8_t a[400];
            long n = get_file("aether/SAVES/ACTIVE.TXT", a, sizeof a);
            char *nl = n > 0 ? memchr(a, '\n', (size_t)n) : NULL;
            CHECK(nl != NULL);
            if (nl) put_file("aether/SAVES/ACTIVE.TXT", a, (size_t)(nl - (char *)a) + 1);
        }
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-4444@U", &pr), 0);       /* relaunch (case 1) */
        CHECK((i = fake_bram_find("XSAVE_00001")) >= 0 && fake_bram_dir(i)->date == 100);   /* RED before: 2000 */
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-5555@U", &pr), 0);       /* archived */
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-4444@U", &pr), 0);       /* loaded again */
        CHECK((i = fake_bram_find("XSAVE_00001")) >= 0 && fake_bram_dir(i)->date == 100);
        CHECK(hist_has("aether/SAVES/T-4444[U]", "XSAVE_00001", 2000));   /* V1 never lost */
    }
    /* fix 4 (I7): a version the session never loaded (copied in by another menu or a PC), dated later, never replaces
     * BRAM's: the console keeps its copy, says so, and the other version is kept as an alternate */
    {
        char abs[600], c[900], rel[1000];
        static uint8_t img[200];
        om_bup_dir d;
        uint32_t k;
        snprintf(abs, sizeof abs, "%s/aether/SAVES/T-4444[U]", root);
        cur_folder(abs, c);
        memset(&d, 0, sizeof d);
        memcpy(d.name, "XSAVE_00001", 11);
        memcpy(d.comment, "TEST", 5);
        d.language = 1; d.date = 5000; d.datasize = 100;
        om_bup_encode_hdr(&d, img);
        for (k = 0; k < 100; k++) img[64 + k] = (uint8_t)(k * 3u + 77u);
        snprintf(rel, sizeof rel, "%s/XSAVE_00001~5.BUP", c + strlen(root) + 1);
        put_file(rel, img, 164);
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-4444@U", &pr), 0);
        CHECK((i = fake_bram_find("XSAVE_00001")) >= 0 && fake_bram_dir(i)->date == 100);
        CHECK(strstr(pr.notice, "IMPORTED SAVE DIFFERS - KEPT BOTH") != NULL);
        CHECK(!strncmp(pr.notice, "T-4444@U:", 9) && strlen(pr.notice) <= 80 && pr.notice[39] == ' ');
        /* to use it, the user deletes the save in the game: the alternate's version is not listed, so it fills */
        fake_bram_del("XSAVE_00001");
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-4444@U", &pr), 0);
        CHECK((i = fake_bram_find("XSAVE_00001")) >= 0 && fake_bram_dir(i)->date == 5000);
        CHECK(hist_has("aether/SAVES/T-4444[U]", "XSAVE_00001", 100));     /* the console's copy: history */
    }
}

/* the reviewer's sim B (task-24-review.md "Re-review (C2 fix)", I7), ported. The active game creates
 * NEWSAVE_001 on a reset clock (date 100), not archived yet; Settings > Import brings Kai's NEWSAVE_001 with its real
 * date (3000) into the current set (no file of that record there). RED before: the relaunch put the import over the
 * player's newer save. Now: BRAM keeps d100 (and says it kept both), the archive keeps d3000 as an alternate in the
 * generation's ALT/ (never loaded), the next load is d100; deleting the save in the game brings the import in. */
static void sim_b(void)
{
    om_pgs_result pr;
    om_pgs_imp r;
    char note[OM_PGS_NOTICE];
    int i, pass;
    for (pass = 0; pass < 2; pass++) {                 /* 0: relaunched first; 1: another game first (the archive) */
        wipe();
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-6666@U", &pr), 0);
        brec("OLDSAVE_001", 60, 1, 50);
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-7777@U", &pr), 0);
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-6666@U", &pr), 0);       /* loaded: the list holds OLDSAVE_001 */
        brec("NEWSAVE_001", 80, 2, 100);                              /* created on the reset clock */
        put_bup("SAVES/BRAM/T-6666[U]/NEWSAVE_001.BUP", "NEWSAVE_001", 80, 9, 3000);
        CHECK_INT(om_pgs_import(&env, &r, note), 0);
        CHECK_INT(r.kai, 1);
        if (!pass) {
            CHECK_INT(om_pgs_run(&env, 1, 0, "T-6666@U", &pr), 0);   /* relaunch */
            CHECK((i = fake_bram_find("NEWSAVE_001")) >= 0 && fake_bram_dir(i)->date == 100);   /* RED before: 3000 */
            CHECK(strstr(pr.notice, "IMPORTED SAVE DIFFERS - KEPT BOTH") != NULL);
        }
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-7777@U", &pr), 0);       /* archived */
        if (pass) CHECK(strstr(pr.notice, "IMPORTED SAVE DIFFERS - KEPT BOTH") != NULL);
        CHECK(cur_has("aether/SAVES/T-6666[U]", "NEWSAVE_001.BUP"));
        CHECK(cur_has("aether/SAVES/T-6666[U]", "ALT/NEWSAVE_001~1.BUP") || cur_has("aether/SAVES/T-6666[U]", "ALT/NEWSAVE_001.BUP"));
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-6666@U", &pr), 0);       /* loaded again */
        CHECK((i = fake_bram_find("NEWSAVE_001")) >= 0 && fake_bram_dir(i)->date == 100);
        CHECK(hist_has("aether/SAVES/T-6666[U]", "NEWSAVE_001", 3000));
        /* the user wants the import: deletes the save in the game, relaunches */
        fake_bram_del("NEWSAVE_001");
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-6666@U", &pr), 0);
        CHECK((i = fake_bram_find("NEWSAVE_001")) >= 0 && fake_bram_dir(i)->date == 3000);
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-7777@U", &pr), 0);
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-6666@U", &pr), 0);
        CHECK((i = fake_bram_find("NEWSAVE_001")) >= 0 && fake_bram_dir(i)->date == 3000);
        CHECK(hist_has("aether/SAVES/T-6666[U]", "NEWSAVE_001", 100));       /* the console's: history */
    }
}

/* user decision 2026-11-23: Rings' [SLOT10]..[SLOT19] (NetLink / XBAND games keep their saves there --
 * 102 of them on the user's card) go into the game's lowest empty VibeMenu slot, 1-9 then 0; with every slot full, the
 * save is kept as an alternate (ALT/), never skipped. A run again copies nothing. RED before: skipped. */
static void rings_high_slots(void)
{
    om_pgs_imp r;
    char note[OM_PGS_NOTICE];
    int k;
    wipe();
    put_bup("satiator-saves/[SLOT18]/T-300/NETSAVE_018.BUP", "NETSAVE_018", 40, 1, 5);
    put_bup("satiator-saves/[SLOT19]/T-300/NETSAVE_019.BUP", "NETSAVE_019", 40, 2, 5);
    put_bup("satiator-saves/[SLOT19]/T-300/NETSAVE_19B.BUP", "NETSAVE_19B", 40, 3, 5);
    put_bup("aether/SAVES/T-300/01/OWNSAVE_001.BUP", "OWNSAVE_001", 40, 4, 5);   /* slot 1 in use */
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.rc, 0);
    CHECK_INT(r.rings, 3); CHECK_INT(r.skipped, 0);
    CHECK(exists("aether/SAVES/T-300/02/NETSAVE_018.BUP"));       /* the lowest empty slot */
    CHECK(exists("aether/SAVES/T-300/03/NETSAVE_019.BUP") && exists("aether/SAVES/T-300/03/NETSAVE_19B.BUP"));
    CHECK(!exists("aether/SAVES/T-300/NETSAVE_018.BUP"));
    fake_mut_n = 0;                                                  /* a run again: nothing copied */
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.rings, 0); CHECK_INT(r.present, 3);
    CHECK(!exists("aether/SAVES/T-300/04"));
    /* 1-9 full, slot 0 empty: slot 0 */
    wipe();
    put_bup("satiator-saves/[SLOT12]/T-301/NETSAVE_012.BUP", "NETSAVE_012", 40, 5, 5);
    for (k = 1; k <= 9; k++) {
        char rel[200];
        snprintf(rel, sizeof rel, "aether/SAVES/T-301/0%d/OWN_SAVE_0%d.BUP", k, k);
        put_bup(rel, "OWN_SAVE_00", 40, (uint32_t)(10 + k), 5);
    }
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK(exists("aether/SAVES/T-301/NETSAVE_012.BUP"));
    /* every slot full: an alternate, counted, never lost */
    put_bup("satiator-saves/[SLOT13]/T-301/NETSAVE_013.BUP", "NETSAVE_013", 40, 6, 5);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.alternates, 1); CHECK_INT(r.skipped, 0);
    CHECK(exists("aether/SAVES/T-301/ALT/NETSAVE_013.BUP"));
    CHECK_INT(om_pgs_import(&env, &r, note), 0);                     /* again: present, no second copy */
    CHECK_INT(r.alternates, 0);
    CHECK_INT(count_files("aether/SAVES/T-301/ALT"), 1);
}

/* Hardware 2026-11-25 (the AETHER card): the user's Rings saves for Policenauts (T-9510G) and Panzer Dragoon Saga
 * (MK-81307) are only in Rings' [SLOT19] (next to Rings' SLOT.TXT), and Aether held no saves for either game. The
 * import put them in slot 1 (24c's "1-9, then 0"), so a launch with the Slot row's default 0 loaded nothing: the game
 * offered only "New game". Now a high slot of a game with no Aether saves in any slot goes to slot 0, the slot a
 * launch uses by default, and that launch loads it. RED before: T-9510G/01, and none of them in BRAM. */
static void user_slot19_layout(void)
{
    static const char *pol[6] = { "POLICENAUTS", "POLICENA_01", "POLICENA_02", "POLICENA_03", "POLICENA_04",
                                  "POLICENA_05" };
    static const char *pds[4] = { "ANDROMEDA_3", "PANDRA_3_01", "PANDRA_3_02", "PANDRA_3_03" };
    om_pgs_imp r;
    om_pgs_result pr;
    char note[OM_PGS_NOTICE], rel[200];
    uint32_t dt;
    int k;
    wipe();
    for (k = 0; k < 6; k++) {
        snprintf(rel, sizeof rel, "satiator-saves/[SLOT19]/T-9510G/%s.BUP", pol[k]);
        put_bup(rel, pol[k], k ? 3008 : 3224, (uint32_t)(40 + k), (uint32_t)(100 + k));
    }
    put_file("satiator-saves/[SLOT19]/T-9510G/SLOT.TXT", (const uint8_t *)"id=0\r\ndate=2025/02/10 18:59:13\r\n", 32);
    for (k = 0; k < 4; k++) {
        snprintf(rel, sizeof rel, "satiator-saves/[SLOT19]/MK-81307/%s.BUP", pds[k]);
        put_bup(rel, pds[k], k ? 1276 : 16, (uint32_t)(50 + k), (uint32_t)(200 + k));
    }
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.rc, 0);
    CHECK_INT(r.rings, 10); CHECK_INT(r.skipped, 0); CHECK_INT(r.conflicts, 0);
    for (k = 0; k < 6; k++) {                                        /* slot 0: the product folder (Kai's rule) */
        snprintf(rel, sizeof rel, "aether/SAVES/T-9510G/%s.BUP", pol[k]);
        CHECK(exists(rel));                                          /* RED before: in T-9510G/01 */
    }
    for (k = 0; k < 4; k++) {
        snprintf(rel, sizeof rel, "aether/SAVES/MK-81307/%s.BUP", pds[k]);
        CHECK(exists(rel));
    }
    CHECK(!exists("aether/SAVES/T-9510G/01") && !exists("aether/SAVES/MK-81307/01"));
    CHECK(!exists("aether/SAVES/T-9510G/SLOT.TXT"));
    fake_mut_n = 0;                                                  /* a run again: nothing copied */
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.rings, 0); CHECK_INT(r.present, 10);
    CHECK(!exists("aether/SAVES/T-9510G/01"));
    /* the next launch of Policenauts Disc 1 on Slot 0 (the default), saves on: BRAM holds the imported saves */
    wipe_bram_only();
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-9510G@JTUBKAEL", &pr), 0);
    CHECK_INT(pr.rc, 0);
    for (k = 0; k < 6; k++) CHECK(fake_bram_find(pol[k]) >= 0);     /* RED before: none */
    CHECK(fake_bram_find("PANDRA_3_01") < 0);
    /* two high slots of a game with no saves: the first, [SLOT18], to slot 0; [SLOT19] to slot 1 */
    wipe();
    put_bup("satiator-saves/[SLOT18]/T-9510G/POLICENA_01.BUP", "POLICENA_01", 3008, 60, 300);
    put_bup("satiator-saves/[SLOT19]/T-9510G/POLICENA_01.BUP", "POLICENA_01", 3008, 61, 301);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.rings, 2); CHECK_INT(r.conflicts, 0);
    CHECK_INT(ndatesz("aether/SAVES/T-9510G/POLICENA_01.BUP", &dt), 3008); CHECK_INT(dt, 300);
    CHECK_INT(ndatesz("aether/SAVES/T-9510G/01/POLICENA_01.BUP", &dt), 3008); CHECK_INT(dt, 301);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);                     /* again: both present, nothing copied */
    CHECK_INT(r.rings, 0); CHECK_INT(r.present, 2);
    CHECK(!exists("aether/SAVES/T-9510G/02"));
}

/* Kai's SAVES/LATEST (saves in no BRAM/<game> folder -- Sonic R, PANDRA_3_01 on the user's card) is
 * imported copy-only into _GENERAL; SAVES/2025 (raw BRAM dumps) is left alone. The import stays current through the
 * general set's next archive (SAVES/GENLIST.TXT: it was never loaded, so it is new) and loads with the general set.
 * RED before: not imported. */
static void kai_latest(void)
{
    om_pgs_imp r;
    om_pgs_result pr;
    char note[OM_PGS_NOTICE];
    wipe();
    put_bup("SAVES/LATEST/SONICR_0001.BUP", "SONICR_0001", 50, 1, 7);
    put_bup("SAVES/LATEST/PANDRA_3_01.BUP", "PANDRA_3_01", 60, 2, 8);
    put_file("SAVES/2025/BRAM_DUMP.BIN", (const uint8_t *)"rawrawraw", 9);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.rc, 0);
    CHECK_INT(r.kai, 2);
    CHECK(exists("aether/SAVES/_GENERAL/SONICR_0001.BUP") && exists("aether/SAVES/_GENERAL/PANDRA_3_01.BUP"));
    CHECK_INT(r.skipped, 0);                                         /* 2025: not a save folder, not counted */
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.kai, 0); CHECK_INT(r.present, 2);
    /* the general set in BRAM (another save), a per-game launch archives it: the imports stay current */
    wipe_bram_only();
    brec("GENERAL_SAV", 40, 3, 9);
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-1111@U", &pr), 0);           /* case 3 */
    CHECK(cur_has("aether/SAVES/_GENERAL", "SONICR_0001.BUP") && cur_has("aether/SAVES/_GENERAL", "GENERAL_SAV.BUP"));
    CHECK_INT(om_pgs_run(&env, 0, 0, "T-1111@U", &pr), 0);           /* saves off: the general set back */
    CHECK(fake_bram_find("SONICR_0001") >= 0 && fake_bram_find("PANDRA_3_01") >= 0 && fake_bram_find("GENERAL_SAV") >= 0);
    CHECK(exists("aether/SAVES/GENLIST.TXT"));
}

/* skipped saves are counted by file, not by folder, and named in IMPORT.TXT. RED before: folders were
 * counted (and '_' folders not at all), nothing named. */
static void skip_names(void)
{
    om_pgs_imp r;
    char note[OM_PGS_NOTICE];
    static uint8_t t[8192];
    long n;
    wipe();
    put_bup("SAVES/BRAM/_BACKUP/GENERAL_01.BUP", "GENERAL_01", 40, 1, 5);
    put_bup("SAVES/BRAM/_BACKUP/GENERAL_02.BUP", "GENERAL_02", 40, 2, 5);
    put_bup("SAVES/BRAM/not a game/ODD_SAVE_01.BUP", "ODD_SAVE_01", 40, 3, 5);
    put_bup("satiator-saves/_BACKUP/GENERAL_03.BUP", "GENERAL_03", 40, 4, 5);
    put_bup("satiator-saves/T-901/SAVRING.BUP", "SAVRING", 0, 4, 1);              /* Rings' own marker: reserved */
    put_bup("satiator-saves/T-902/GOODSAVE_01.BUP", "GOODSAVE_01", 40, 5, 5);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.rings, 1);
    CHECK_INT(r.skipped, 5);
    n = get_file("aether/SAVES/IMPORT.TXT", t, sizeof t - 1);
    CHECK(n > 0);
    if (n > 0) {
        t[n] = 0;
        CHECK(strstr((char *)t, " skipped=5 ") != NULL);
        CHECK(strstr((char *)t, "/SAVES/BRAM/_BACKUP/GENERAL_01.BUP") && strstr((char *)t, "/SAVES/BRAM/_BACKUP/GENERAL_02.BUP"));
        CHECK(strstr((char *)t, "/SAVES/BRAM/not a game/ODD_SAVE_01.BUP") != NULL);
        CHECK(strstr((char *)t, "/satiator-saves/_BACKUP/GENERAL_03.BUP") != NULL);
        CHECK(strstr((char *)t, "/satiator-saves/T-901/SAVRING.BUP") != NULL);
        CHECK(strstr((char *)t, "GOODSAVE_01") == NULL);
        printf("IMPORT.TXT (skips named):\n%s", (char *)t);
    }
}

/* M6 Task 24c (user decision: the import is picked per game). The scan's rows (impscan.h), IMPORT.SEL with a game
 * unticked: only the ticked games are copied; a stale, damaged or absent IMPORT.SEL imports nothing and says so; the
 * file is removed after a run; a run again (picked again) copies nothing twice. */
static void pick_layout(void)
{
    wipe();
    put_bup("SAVES/BRAM/T-100[U]/KAISAVE_100.BUP", "KAISAVE_100", 40, 1, 5);
    put_bup("SAVES/BRAM/T-100[U]/02/KAISAVE_10B.BUP", "KAISAVE_10B", 40, 2, 5);
    put_bup("SAVES/BRAM/T-200/KAISAVE_200.BUP", "KAISAVE_200", 40, 3, 5);
    put_bup("SAVES/BRAM/_BACKUP/GENERAL_01.BUP", "GENERAL_01", 40, 4, 5);         /* not a game: no row */
    put_bup("SAVES/LATEST/SONICR_0001.BUP", "SONICR_0001", 40, 5, 5);              /* row _GENERAL */
    put_bup("satiator-saves/T-100/RINGSAV_100.BUP", "RINGSAV_100", 40, 6, 5);      /* the same game as Kai's */
    put_bup("satiator-saves/[SLOT18]/T-300/NETSAVE_018.BUP", "NETSAVE_018", 40, 7, 5);
}
static int row_n(const om_isc_row *r, uint32_t n, const char *key)
{
    uint32_t i;
    for (i = 0; i < n; i++) if (!strcmp(r[i].key, key)) return r[i].n;
    return -1;
}
static void import_picker(void)
{
    static om_isc_row rows[64];
    static om_isc_name names[256];
    static uint32_t al[64], st[80];
    static char sel[4096];
    char gid[16], note[OM_PGS_NOTICE];
    uint32_t n, crc, i;
    int len;
    om_pgs_imp r;
    pick_layout();
    CHECK_INT(om_isc_scan(rows, 64, &n, names, 256, al, st), 0);
    CHECK_INT(n, 4);
    CHECK_INT(row_n(rows, n, "T-100"), 3);                           /* Kai's region folder, its 02, Rings' id */
    CHECK_INT(row_n(rows, n, "T-200"), 1);
    CHECK_INT(row_n(rows, n, "_GENERAL"), 1);
    CHECK_INT(row_n(rows, n, "T-300"), 1);                           /* Rings [SLOT18] */
    for (i = 0; i < n; i++) CHECK_INT(rows[i].on, 1);
    for (i = 0; i < n; i++) if (!strcmp(rows[i].key, "T-200")) rows[i].on = 0;   /* the user unticks one */
    CHECK((len = om_isel_fmt(rows, n, sel, sizeof sel, &crc)) > 0);
    put_file("aether/SAVES/IMPORT.SEL", (const uint8_t *)sel, (size_t)len);
    snprintf(gid, sizeof gid, "SEL%08X", crc);
    CHECK_INT(om_pgs_import_sel(&env, gid, &r, note), 0);
    CHECK_INT(r.rc, 0);
    CHECK_INT(r.kai + r.rings, 5);                                   /* T-100 x3, _GENERAL, T-300 */
    CHECK(exists("aether/SAVES/T-100[U]/KAISAVE_100.BUP") && exists("aether/SAVES/_GENERAL/SONICR_0001.BUP"));
    CHECK(exists("aether/SAVES/T-300/NETSAVE_018.BUP"));            /* the game's only saves: slot 0 */
    CHECK(!exists("aether/SAVES/T-200"));                          /* unticked: not copied */
    CHECK(!exists("aether/SAVES/IMPORT.SEL"));                     /* used once */
    /* the same list again: refused (it was removed), nothing written */
    fake_mut_n = 0;
    CHECK_INT(om_pgs_import_sel(&env, gid, &r, note), 0);
    CHECK(r.rc != 0); CHECK_STR(note, "IMPORT LIST IS STALE: NOTHING COPIED");
    CHECK_INT(fake_mut_n, 0);
    /* stale: a file from an earlier pick (another CRC) */
    put_file("aether/SAVES/IMPORT.SEL", (const uint8_t *)sel, (size_t)len);
    snprintf(gid, sizeof gid, "SEL%08X", crc ^ 1u);
    fake_mut_n = 0;
    CHECK_INT(om_pgs_import_sel(&env, gid, &r, note), 0);
    CHECK_INT(r.rc, OM_E_CRC); CHECK_INT(r.kai + r.rings, 0); CHECK_INT(fake_mut_n, 0);
    CHECK(!exists("aether/SAVES/T-200"));
    /* damaged: a count that does not match, a bad key, a cut file */
    {
        static const char *bad[] = { "VMSEL1 3\nT-100\n", "VMSEL1 1\nT 100\n", "VMSEL1 1\nT-100", "XXSEL1 1\nT-100\n", "" };
        int b;
        for (b = 0; b < 5; b++) {
            uint32_t bl = (uint32_t)strlen(bad[b]);
            put_file("aether/SAVES/IMPORT.SEL", (const uint8_t *)bad[b], bl);
            snprintf(gid, sizeof gid, "SEL%08X", om_crc32(0, bad[b], bl));
            fake_mut_n = 0;
            CHECK_INT(om_pgs_import_sel(&env, gid, &r, note), 0);
            CHECK(r.rc != 0); CHECK_INT(fake_mut_n, 0);
        }
    }
    CHECK(!exists("aether/SAVES/T-200"));
    /* picked again, T-200 ticked now: only it is new; the rest present */
    for (i = 0; i < n; i++) rows[i].on = 1;
    len = om_isel_fmt(rows, n, sel, sizeof sel, &crc);
    put_file("aether/SAVES/IMPORT.SEL", (const uint8_t *)sel, (size_t)len);
    snprintf(gid, sizeof gid, "SEL%08X", crc);
    CHECK_INT(om_pgs_import_sel(&env, gid, &r, note), 0);
    CHECK_INT(r.rc, 0);
    CHECK_INT(r.kai + r.rings, 1); CHECK_INT(r.present, 5);
    CHECK(exists("aether/SAVES/T-200/KAISAVE_200.BUP"));
}

/* hardware 2026-11-27: on a card no game has launched from, /aether/SAVES does not exist and Settings > Import said
 * "IMPORT.SEL NOT WRITTEN: CARD FULL?". The menu's write (sd.c om_sd_replace_in, as folders.c calls it) makes the
 * folder first; the import then runs. */
static void import_fresh_card(void)
{
    static om_isc_row rows[64];
    static om_isc_name names[256];
    static uint32_t al[64], st[80];
    static char sel[4096];
    char gid[16], note[OM_PGS_NOTICE];
    uint32_t n, crc;
    int len;
    om_pgs_imp r;
    pick_layout();
    CHECK(!exists("aether/SAVES"));
    CHECK_INT(om_isc_scan(rows, 64, &n, names, 256, al, st), 0);
    CHECK((len = om_isel_fmt(rows, n, sel, sizeof sel, &crc)) > 0);
    CHECK_INT(om_sd_replace(OM_ISC_SEL_PATH, sel, (uint32_t)len), -100 - FR_NO_PATH);   /* the bug: no folder */
    CHECK(!exists("aether/SAVES"));
    CHECK_INT(om_sd_replace_in("/aether/SAVES", OM_ISC_SEL_PATH, sel, (uint32_t)len), 0);
    CHECK(exists("aether/SAVES/IMPORT.SEL"));
    CHECK_INT(om_sd_replace_in("/aether/SAVES", OM_ISC_SEL_PATH, sel, (uint32_t)len), 0);   /* SAVES there: fine */
    snprintf(gid, sizeof gid, "SEL%08X", crc);
    CHECK_INT(om_pgs_import_sel(&env, gid, &r, note), 0);
    CHECK_INT(r.rc, 0);
    CHECK_INT(r.kai + r.rings, 6);
    CHECK(exists("aether/SAVES/T-200/KAISAVE_200.BUP"));
    CHECK(!exists("aether/SAVES/IMPORT.SEL"));
}

/* the reviewer's sim HS (task-24-review.md "Task 24c review", I8), ported. VibeMenu's T-300/01 holds
 * GAMEOPT_001 (options) and GAMESAV_001 d50; Rings [SLOT18]/T-300 the byte-identical GAMEOPT_001 and its own
 * GAMESAV_001 d90. RED before: merged into 01, d90 a conflict, copied nowhere. Now: slot 18 is not 01's (one shared
 * record is not a run again): it goes to 02, whole; a run again finds it there. */
static void sim_hs(void)
{
    om_pgs_imp r;
    char note[OM_PGS_NOTICE];
    uint32_t dt;
    wipe();
    put_bup("aether/SAVES/T-300/01/GAMEOPT_001.BUP", "GAMEOPT_001", 30, 1, 10);
    put_bup("aether/SAVES/T-300/01/GAMESAV_001.BUP", "GAMESAV_001", 60, 2, 50);
    put_bup("satiator-saves/[SLOT18]/T-300/GAMEOPT_001.BUP", "GAMEOPT_001", 30, 1, 10);
    put_bup("satiator-saves/[SLOT18]/T-300/GAMESAV_001.BUP", "GAMESAV_001", 60, 3, 90);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.rc, 0);
    CHECK_INT(r.conflicts, 0);
    CHECK_INT(ndatesz("aether/SAVES/T-300/02/GAMESAV_001.BUP", &dt), 60); CHECK_INT(dt, 90);   /* RED before */
    CHECK(exists("aether/SAVES/T-300/02/GAMEOPT_001.BUP"));
    CHECK_INT(ndatesz("aether/SAVES/T-300/01/GAMESAV_001.BUP", &dt), 60); CHECK_INT(dt, 50);   /* 01 untouched */
    fake_mut_n = 0;                                                   /* again: 02 is this slot's, nothing copied */
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.rings, 0); CHECK_INT(r.present, 2); CHECK_INT(r.conflicts, 0);
    CHECK(!exists("aether/SAVES/T-300/03"));
    /* a resume after a cut that placed only part of it: 02 holds GAMEOPT_001 alone -- still this slot's */
    wipe();
    put_bup("aether/SAVES/T-300/01/GAMEOPT_001.BUP", "GAMEOPT_001", 30, 1, 10);
    put_bup("aether/SAVES/T-300/01/GAMESAV_001.BUP", "GAMESAV_001", 60, 2, 50);
    put_bup("aether/SAVES/T-300/02/GAMEOPT_001.BUP", "GAMEOPT_001", 30, 1, 10);
    put_bup("satiator-saves/[SLOT18]/T-300/GAMEOPT_001.BUP", "GAMEOPT_001", 30, 1, 10);
    put_bup("satiator-saves/[SLOT18]/T-300/GAMESAV_001.BUP", "GAMESAV_001", 60, 3, 90);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(ndatesz("aether/SAVES/T-300/02/GAMESAV_001.BUP", &dt), 60); CHECK_INT(dt, 90);
    CHECK(!exists("aether/SAVES/T-300/03"));
    /* every slot full: the whole high slot into ALT/, nothing dropped; again: present */
    wipe();
    {
        int k;
        for (k = 0; k <= 9; k++) {
            char rel[200];
            if (k) snprintf(rel, sizeof rel, "aether/SAVES/T-300/0%d/GAMESAV_001.BUP", k);
            else snprintf(rel, sizeof rel, "aether/SAVES/T-300/GAMESAV_001.BUP");
            put_bup(rel, "GAMESAV_001", 60, (uint32_t)(20 + k), 50);
        }
    }
    put_bup("satiator-saves/[SLOT18]/T-300/GAMESAV_001.BUP", "GAMESAV_001", 60, 3, 90);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.alternates, 1); CHECK_INT(r.conflicts, 0);
    CHECK(exists("aether/SAVES/T-300/ALT/GAMESAV_001.BUP"));
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.alternates, 0); CHECK_INT(r.present, 1);
}

/* the reviewer's sim GL (I9), ported. The console's general save d100 (BRAM, no marker); Kai's LATEST
 * holds the same record d3000. With no GENLIST.TXT (the usual state while per-game saves stay on): RED before, the
 * import was neither carried nor kept as an alternate, with no notice. Now: the next archive keeps it in ALT/ with
 * "GENERAL SAVES: IMPORTED SAVE DIFFERS - KEPT BOTH", the console's copy stays what loads; deleting the general save
 * (the memory manager) brings the import in. And with a GENLIST.TXT (written by a saves-off launch) the same. */
static void sim_gl(void)
{
    om_pgs_result pr;
    om_pgs_imp r;
    char note[OM_PGS_NOTICE];
    int i, pass;
    for (pass = 0; pass < 2; pass++) {
        wipe();
        if (pass) {                                                   /* a GENLIST.TXT: archive and give back first */
            brec("OTHERGEN_01", 40, 2, 50);
            CHECK_INT(om_pgs_run(&env, 1, 0, "T-1111@U", &pr), 0);
            CHECK_INT(om_pgs_run(&env, 0, 0, "T-1111@U", &pr), 0);
            CHECK(exists("aether/SAVES/GENLIST.TXT"));
        }
        brec("GENSAVE_001", 70, 1, 100);                              /* the console's general save, not archived yet */
        put_bup("SAVES/LATEST/GENSAVE_001.BUP", "GENSAVE_001", 70, 9, 3000);
        CHECK_INT(om_pgs_import(&env, &r, note), 0);
        CHECK_INT(r.kai, 1);
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-2222@U", &pr), 0);       /* case 3: the general set archived */
        CHECK(strstr(pr.notice, "IMPORTED SAVE DIFFERS - KEPT BOTH") != NULL);       /* RED before */
        CHECK(!strncmp(pr.notice, "GENERAL SAVES:", 14));
        CHECK(cur_has("aether/SAVES/_GENERAL", "ALT/GENSAVE_001.BUP") || cur_has("aether/SAVES/_GENERAL", "ALT/GENSAVE_001~1.BUP"));
        CHECK_INT(om_pgs_run(&env, 0, 0, "T-2222@U", &pr), 0);       /* saves off: the general set back */
        CHECK((i = fake_bram_find("GENSAVE_001")) >= 0 && fake_bram_dir(i)->date == 100);     /* the console's */
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-2222@U", &pr), 0);       /* archived again: still kept */
        CHECK(cur_has("aether/SAVES/_GENERAL", "ALT/GENSAVE_001.BUP") || cur_has("aether/SAVES/_GENERAL", "ALT/GENSAVE_001~1.BUP"));
        CHECK(strstr(pr.notice, "IMPORTED SAVE DIFFERS") == NULL);   /* announced once */
        /* to use it: the general save deleted in the memory manager (saves off), then a per-game launch */
        CHECK_INT(om_pgs_run(&env, 0, 0, "T-2222@U", &pr), 0);
        fake_bram_del("GENSAVE_001");
        CHECK_INT(om_pgs_run(&env, 1, 0, "T-2222@U", &pr), 0);
        CHECK_INT(om_pgs_run(&env, 0, 0, "T-2222@U", &pr), 0);
        CHECK((i = fake_bram_find("GENSAVE_001")) >= 0 && fake_bram_dir(i)->date == 3000);
        CHECK(hist_has("aether/SAVES/_GENERAL", "GENSAVE_001", 100));      /* the console's: history */
    }
    /* the game-folder rule, both ways: _GENERAL's current set already holds the record in another version -- a
     * conflict, never written over (the Kai copy stays where it is) */
    put_bup("SAVES/LATEST/GENSAVE_001.BUP", "GENSAVE_001", 70, 8, 4000);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.kai, 0); CHECK_INT(r.conflicts, 1);
}

/* the Card Manager's finding: a card with no /aether/SAVES at all. Real FatFs answers a name under a
 * missing folder with FR_NO_PATH, not FR_NO_FILE (fake_nopath); every launcher path reads it as "absent": the import
 * and the backup run, the swap plans case 3. */
static void no_saves_card(void)
{
    om_pgs_imp ir;
    om_pgs_bak br;
    om_pgs_result pr;
    char note[OM_PGS_NOTICE];
    int was = fake_nopath;
    fake_nopath = 1;
    wipe();
    CHECK(!exists("aether/SAVES"));
    CHECK_INT(om_pgs_import(&env, &ir, note), 0); CHECK_INT(ir.rc, 0);
    wipe();
    om_bram_init(env.lib, env.work);
    brec("NOSAVES_001", 40, 3, 9);
    CHECK_INT(om_pgs_backup(&env, "", &br, note), 0); CHECK_INT(br.rc, 0); CHECK_INT(br.written, 1);
    wipe();
    brec("NOSAVES_001", 40, 3, 9);
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-3333@U", &pr), 0);             /* case 3: the general set archived */
    CHECK_STR(pr.line, "");
    CHECK(cur_has("aether/SAVES/_GENERAL", "NOSAVES_001.BUP"));
    fake_nopath = was;
}

static void sim_i6(void)
{
    om_pgs_result pr;
    int i;
    /* (a) launched again: case 1 loads it now; then archived and launched once more */
    i6_card();
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-1111@U", &pr), 0);
    CHECK(fake_bram_find("KAISAVE_001") >= 0);                       /* RED before the fix */
    CHECK(fake_bram_find("T1111_SAV01") >= 0);
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-2222@U", &pr), 0);
    CHECK(cur_has("aether/SAVES/T-1111[U]", "KAISAVE_001.BUP"));
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-1111@U", &pr), 0);
    CHECK(fake_bram_find("KAISAVE_001") >= 0);
    /* (b) the user plays (T1111_SAV01 changes), then another game: the archive is a new generation and carries the
     * imported record; the next T-1111 launch gets both */
    i6_card();
    brec("T1111_SAV01", 100, 2, 600);
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-2222@U", &pr), 0);
    CHECK(exists("aether/SAVES/T-1111[U]/G000/G00002/DONE.TXT"));
    CHECK(cur_has("aether/SAVES/T-1111[U]", "KAISAVE_001.BUP"));    /* RED before the fix */
    CHECK_INT(om_pgs_run(&env, 1, 0, "T-1111@U", &pr), 0);
    CHECK((i = fake_bram_find("KAISAVE_001")) >= 0 && fake_bram_dir(i)->date == 700);
    CHECK((i = fake_bram_find("T1111_SAV01")) >= 0 && fake_bram_dir(i)->date == 600);
}

/* the reviewer's sim I5. VibeMenu already reads SAVES/MK-81214 (an earlier
 * import); Kai has since made MK-81214[UT]. A second import merges into the folder VibeMenu uses and never creates the
 * region folder that would change the launcher's pick (sticky folders). */
static void sim_i5(void)
{
    om_pgs_imp r;
    om_pgs_result pr;
    char note[OM_PGS_NOTICE];
    uint32_t dt;
    wipe();
    put_bup("aether/SAVES/MK-81214/SCORCHER_01.BUP", "SCORCHER_01", 50, 1, 900);
    put_bup("aether/SAVES/MK-81214/03/SCORCHER_01.BUP", "SCORCHER_01", 60, 2, 950);
    put_bup("SAVES/BRAM/MK-81214[UT]/SCORCHER_01.BUP", "SCORCHER_01", 40, 3, 500);
    put_bup("SAVES/BRAM/MK-81214[UT]/SCORCHER_02.BUP", "SCORCHER_02", 40, 4, 500);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.rc, 0);
    CHECK(!exists("aether/SAVES/MK-81214[UT]"));
    CHECK_INT(ndatesz("aether/SAVES/MK-81214/SCORCHER_01.BUP", &dt), 50); CHECK_INT(dt, 900);   /* never over it */
    CHECK_INT(ndatesz("aether/SAVES/MK-81214/SCORCHER_02.BUP", 0), 40);                         /* merged in */
    CHECK_INT(r.conflicts, 1);
    wipe_bram_only();
    CHECK_INT(om_pgs_run(&env, 2, 3, "MK-81214@UT", &pr), 0);
    CHECK(fake_bram_find("SCORCHER_01") >= 0 && fake_bram_dir(fake_bram_find("SCORCHER_01"))->date == 950);
}

static void long_run(void)
{
    om_pgs_result r;
    char abs[600];
    int launch, i, bad_rc = 0, lost = 0, checks = 0;
    wipe();
    env.backup = 1;
    env.date = "";
    s_lcg = 12345;
    memset(s_ver, 0, sizeof s_ver);
    brec("GENERAL_SAV", 40, 7, 1);                                       /* the user's console save */
    s_ver[60].date = 1; s_ver[60].size = 40; s_ver[60].crc = om_crc32(0, s_rb, 40);
    for (launch = 1; launch <= 1200; launch++) {
        int game = (int)(lcg() % 60u);
        char gid[24], nm[12];
        uint32_t size = 64u + lcg() % 900u;
        sprintf(gid, "T-%05d@U", game);
        if (om_pgs_run(&env, 2, 0, gid, &r) != 0 || r.bak_rc != 0) {
            if (bad_rc++ < 5) printf("FAIL long run launch %d: rc %d bak %d (%s / %s)\n", launch, r.rc, r.bak_rc, r.line, r.bak_notice);
        }
        /* play: the game saves (a new date and new bytes), sometimes twice; now and then a save is deleted */
        sprintf(nm, "G%05d_SAV", game);
        brec(nm, size, (uint32_t)launch, 100000u + (uint32_t)launch);
        s_ver[game].date = 100000u + (uint32_t)launch; s_ver[game].size = size;
        s_ver[game].crc = om_crc32(0, s_rb, size);
        if (launch % 100 == 0 || launch == 1200) {                      /* nothing the user had is gone */
            snprintf(abs, sizeof abs, "%s/aether/SAVES", root);
            for (i = 0; i <= 60; i++) {
                char rn[12];
                if (!s_ver[i].date) continue;
                if (i == 60) strcpy(rn, "GENERAL_SAV"); else sprintf(rn, "G%05d_SAV", i & 0xFFFF);
                checks++;
                if (!in_bram(rn, &s_ver[i]) && !on_card(abs, rn, &s_ver[i])) {
                    lost++;
                    if (lost < 5) printf("FAIL long run launch %d: %s's newest save is gone\n", launch, rn);
                }
            }
        }
    }
    printf("long run: 1200 launches, %d checks, %d lost, %d bad rc\n", checks, lost, bad_rc);
    CHECK_INT(bad_rc, 0);
    CHECK_INT(lost, 0);
    CHECK(checks > 600);
    env.backup = 0;
}

/* Import speed (hardware 2026-11-24: "IMPORT: MK-81042" for minutes, no progress). The Satiator resolves every
 * path from the card's root (fake_paths: an s_open, a named s_stat, an s_opendir); on the user's exFAT card the root
 * holds ~700 entries. A cart save of 512 KB copied in pieces, then compared with its source: the compare reopened both
 * files for every 2 KB. Bounds: at most 4 path walks per 32 KB of the file (read piece, append, verify both sides) and
 * one s_mkdir walk per folder per run, not per file. */
extern int fake_paths, fake_syncs, fake_reads;
extern uint64_t fake_us;
static om_pgs_prog s_last, s_first;
static int s_nprog, s_beat_back, s_file_full;
static void rec_prog(const om_pgs_prog *p)
{
    if (!s_nprog++) s_first = *p;
    if (p->beat < s_last.beat) s_beat_back++;
    if (p->file_total && p->file_done == p->file_total) s_file_full++;
    s_last = *p;
}
static void cost_line(const char *what, int p0, int s0, int m0, uint64_t u0)
{
    printf("import cost, %s: %d path walks, %d synced 2 KB writes, %d s_mkdir, fake clock %llu ms\n", what,
           fake_paths - p0, fake_syncs - s0, fake_mkdir_calls - m0, (unsigned long long)((fake_us - u0) / 1000u));
}
static void import_cost(void)
{
    static uint8_t big[OM_BUP_HDR + 524288];
    om_pgs_imp r;
    char note[OM_PGS_NOTICE];
    int p0, s0, m0, i;
    uint64_t u0;
    size_t n;
    wipe();
    n = bup_img(big, "BIGCART_01", "CART", 524288, 9, 100);
    put_file("SAVES/BRAM/MK-81042[U]/BIGCART_01.BUP", big, n);
    p0 = fake_paths; s0 = fake_syncs; m0 = fake_mkdir_calls; u0 = fake_us;
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.rc, 0);
    CHECK_INT(r.cart, 1);
    cost_line("one 512 KB cart save", p0, s0, m0, u0);
    CHECK(fake_paths - p0 <= 4 * 16 + 40);                 /* HEAD: ~2 per 2 KB compared = 512+ */
    CHECK_INT(fake_syncs - s0, 258);                       /* 2 KB synced chunks (sdwrite.h), + IMPORT.TXT: unchanged */
    /* 16 small saves in one folder: the folder chain is made once, not per file */
    wipe();
    for (i = 0; i < 16; i++) {
        char rel[80], nm[12];
        snprintf(nm, sizeof nm, "SMALL_%02d", i);
        snprintf(rel, sizeof rel, "SAVES/BRAM/MK-81042[U]/%s.BUP", nm);
        put_bup(rel, nm, 8000, (uint32_t)i, 100);
    }
    p0 = fake_paths; s0 = fake_syncs; m0 = fake_mkdir_calls; u0 = fake_us;
    env.prog = rec_prog;
    s_nprog = s_beat_back = s_file_full = 0;
    memset(&s_last, 0, sizeof s_last);
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    env.prog = 0;
    CHECK_INT(r.kai, 16);
    cost_line("16 saves of 8 KB", p0, s0, m0, u0);
    CHECK(fake_mkdir_calls - m0 <= 8);                     /* HEAD: the whole chain again for every file */
    /* the live progress: the game total known before the first copy, every save and game counted by the end, the
     * beat never going back, each copied file reaching its full size */
    CHECK_INT(s_last.games_total, 1);
    CHECK_INT(s_last.games_done, 1);
    CHECK_INT(s_last.saves_done, 16);
    CHECK(s_last.beat >= 16);
    CHECK_INT(s_beat_back, 0);
    CHECK(s_file_full >= 16);
    CHECK(!strncmp(s_last.game, "MK-81042", 8));
    {
        char b[40];
        om_pgs_bar(b, 10, 1, 4);
        CHECK_STR(b, "##--------  25%");
        om_pgs_bar(b, 10, 4, 4);
        CHECK_STR(b, "########## 100%");
        om_pgs_bar(b, 10, 0, 0);
        CHECK_STR(b, "----------   0%");
    }
}

/* Cancel (user requirement 2026-11-24: B held stops the import at the next safe point). Canceled at every point the
 * import asks: what was copied is whole (no .IMP left, no IMPORT.TXT), and a run again ends exactly where a run that
 * was never canceled ends. */
static int s_ask, s_cancel_at;
static int cancel_at(void) { return ++s_ask == s_cancel_at; }
static void lay_cancel_fixture(void)
{
    static uint8_t big[OM_BUP_HDR + 100000];
    int i;
    for (i = 0; i < 6; i++) {
        char rel[80], nm[12];
        snprintf(nm, sizeof nm, "SMALL_%02d", i);
        snprintf(rel, sizeof rel, "SAVES/BRAM/MK-81042[U]/%s.BUP", nm);
        put_bup(rel, nm, 1500u + 300u * (uint32_t)i, (uint32_t)i, 100);
    }
    put_file("SAVES/BRAM/T-777[J]/BIGCART_02.BUP", big, bup_img(big, "BIGCART_02", "CART", 100000, 3, 100));
    put_bup("satiator-saves/T-778/RING_01.BUP", "RING_01", 900, 5, 100);
}
static void import_cancel(void)
{
    om_pgs_imp r;
    char note[OM_PGS_NOTICE], *want, *got, *mid;
    int k, asks, bad = 0, runs = 0;
    wipe();
    lay_cancel_fixture();
    env.cancel = cancel_at;
    s_ask = 0; s_cancel_at = -1;
    CHECK_INT(om_pgs_import(&env, &r, note), 0);
    CHECK_INT(r.rc, 0);
    asks = s_ask;
    want = tree("aether", 1);
    CHECK(asks >= 10);                                   /* 3 games + 8 files: a safe point before each */
    for (k = 1; k <= asks; k++) {
        wipe();
        lay_cancel_fixture();
        s_ask = 0; s_cancel_at = k;
        CHECK_INT(om_pgs_import(&env, &r, note), 0);
        CHECK_INT(r.rc, OM_PGS_E_CANCEL);
        CHECK_INT(r.canceled, 1);
        CHECK(!strncmp(note, "IMPORT CANCELED", 15));
        mid = tree("aether", 0);
        if (strstr(mid, ".IMP:") || strstr(mid, "IMPORT.TXT")) { bad++; printf("cancel at %d: %s\n", k, mid); }
        free(mid);
        s_ask = 0; s_cancel_at = -1;
        CHECK_INT(om_pgs_import(&env, &r, note), 0);
        CHECK_INT(r.rc, 0);
        got = tree("aether", 1);
        if (strcmp(got, want)) { bad++; printf("cancel at %d, then a run: differs\n%s\nvs\n%s\n", k, got, want); }
        free(got);
        runs++;
    }
    env.cancel = 0;
    printf("import cancel: %d points, %d bad\n", runs, bad);
    CHECK_INT(bad, 0);
    CHECK_INT(runs, asks);
    free(want);
}

int main(void)
{
    if (!mkdtemp(root)) { printf("FAIL mkdtemp\n"); return 1; }
    setenv("FAKE_SD_ROOT", root, 1);
    om_pgs_arena_split(&env, (uint8_t *)arena32);
    env.say = say;
    fake_r1_on = 1; fake_addonly = 1;
    fake_nopath = 1;                                     /* FatFs's FR_NO_PATH for a missing folder */                     /* the R1 property over every test and sweep below */
    import_tests();
    backup_tests();
    swap_tests();
    savegen_tests();
    sim_i5();
    sim_i6();
    no_saves_card();
    in_game_delete();
    sim_c2();
    sim_b();
    rings_high_slots();
    user_slot19_layout();
    kai_latest();
    skip_names();
    import_picker();
    import_fresh_card();
    sim_hs();
    sim_gl();
    long_run();
    import_cost();
    import_cancel();
    {
        char cmd[300];
        snprintf(cmd, sizeof cmd, "rm -rf %s", root);
        if (system(cmd)) {}
    }
    printf("R1 property: %d violations over every delete and overwrite of a record file\n", fake_r1_violations);
    CHECK_INT(fake_r1_violations, 0);
    printf("add-only property: %d violations over every delete, overwrite, move and rename-over of a record file\n", fake_addonly_violations);
    CHECK_INT(fake_addonly_violations, 0);
    return th_done("test_pgsbak");
}
