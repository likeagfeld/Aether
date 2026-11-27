/* menu/host-tests/test_svmgr.c -- the save manager's operations (launcher/svmgr.c) over fake_bram.c and fake_satiator.c:
 * listing per device and per game, the add-only card (a delete is a new generation without the save; backups are new
 * files; no card file is ever removed or written over: fake_addonly), the guard before a console or cartridge save
 * goes (a .BKP generation and a SAVES/_MANAGER copy), and the refusals (journal, Kai, another game's set). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "th.h"
#include "om_fmt.h"
#include "satiator.h"
#include "bupfile.h"
#include "svmgr.h"

extern int fake_addonly, fake_addonly_violations, fake_nopath, fake_bram_cart;
void fake_bram_reset(void);
void fake_bram_put(const char *name, const uint8_t *data, uint32_t size, uint32_t date);
int fake_bram_count(void);
int fake_bram_find(const char *name);
int om_bram_use(uint32_t dev);

static char root[] = "/tmp/omsat_svm_XXXXXX";
static uint32_t arena32[OM_PGS_ARENA / 4 + 4];
static uint32_t big32[(OM_BUP_HDR + 0x40000u) / 4];
static om_sv_item items[300];
static om_pgs_env env;
static om_sv v;

static void sh(const char *cmd) { if (system(cmd)) printf("note: %s failed\n", cmd); }
static void wipe(void)
{
    char c[700];
    snprintf(c, sizeof c, "rm -rf %s/aether && mkdir -p %s/aether/SAVES", root, root);
    sh(c);
    fake_bram_reset();
}
static void mkdirs(const char *rel)
{
    char c[700];
    snprintf(c, sizeof c, "mkdir -p '%s/aether/%s'", root, rel);
    sh(c);
}
static void put(const char *rel_dir, const char *file, const char *name, uint32_t size, uint32_t date)
{
    char p[700];
    uint8_t h[OM_BUP_HDR], *d = malloc(size);
    om_bup_dir b;
    FILE *f;
    uint32_t i;
    memset(&b, 0, sizeof b);
    strncpy(b.name, name, 11);
    memcpy(b.comment, "TEST", 5);
    b.language = 1;
    b.date = date;
    b.datasize = size;
    om_bup_encode_hdr(&b, h);
    for (i = 0; i < size; i++) d[i] = (uint8_t)(i * 7u + date);
    mkdirs(rel_dir);
    snprintf(p, sizeof p, "%s/aether/%s/%s", root, rel_dir, file);
    f = fopen(p, "wb");
    fwrite(h, 1, sizeof h, f);
    fwrite(d, 1, size, f);
    fclose(f);
    free(d);
}
static void touch(const char *rel)
{
    char p[700];
    FILE *f;
    snprintf(p, sizeof p, "%s/aether/%s", root, rel);
    if ((f = fopen(p, "wb"))) fclose(f);
}
static int exists(const char *rel)
{
    char p[700];
    struct stat st;
    snprintf(p, sizeof p, "%s/aether/%s", root, rel);
    return stat(p, &st) == 0;
}
static int count_dir(const char *rel)
{
    char p[700];
    DIR *d;
    struct dirent *e;
    int n = 0;
    snprintf(p, sizeof p, "%s/aether/%s", root, rel);
    if (!(d = opendir(p))) return -1;
    while ((e = readdir(d))) if (e->d_name[0] != '.') n++;
    closedir(d);
    return n;
}
static uint8_t data[4096];
static void bram(const char *name, uint32_t size, uint32_t date)
{
    uint32_t i;
    for (i = 0; i < size; i++) data[i] = (uint8_t)(i * 7u + date);
    fake_bram_put(name, data, size, date);
}
static int find(const char *name, int dev)
{
    uint32_t i;
    for (i = 0; i < v.n; i++)
        if (!strncmp(v.it[i].d.name, name, 11) && v.it[i].dev == dev) return (int)i;
    return -1;
}
static int open_(const char *gid)
{
    return om_sv_open(&v, &env, gid, (uint8_t *)big32, sizeof big32, items, 300, "20261124-120000");
}

#define GID "MK-81045@U"
#define GDIR "SAVES/MK-81045[U]"

static void listing(void)
{
    wipe();
    put(GDIR, "SAVE_A.BUP", "SAVE_A", 100, 5);
    put(GDIR, "SAVE_A~1.BUP", "SAVE_A", 100, 9);                  /* the later date wins (§5.16) */
    put(GDIR "/G000/G00001", "SAVE_B.BUP", "SAVE_B", 50, 3);
    touch(GDIR "/G000/G00001/DONE.TXT");                          /* G00001 is the current set now */
    put(GDIR "/G000/G00001", "SAVE_C.BUP", "SAVE_C", 60, 3);
    put(GDIR "/G000/G00001/ALT", "SAVE_B.BUP", "SAVE_B", 70, 2);
    put(GDIR "/03", "SLOT3.BUP", "SLOT3", 30, 1);
    bram("SAVE_B", 50, 3);
    bram("OTHER", 40, 1);
    bram("VIBEMENU_PG", 8, 0);                                    /* reserved: never listed */
    CHECK_INT(open_(GID), 0);
    CHECK_INT(v.nset, 2);
    CHECK_INT(om_sv_list(&v, OM_SV_SD), 0);
    CHECK_INT(v.n, 4);                                            /* B, C from G00001; B in ALT; SLOT3 */
    CHECK(find("SAVE_A", OM_SV_SD) < 0);                          /* not in the current set */
    CHECK(find("SAVE_C", OM_SV_SD) >= 0);
    CHECK(find("SLOT3", OM_SV_SD) >= 0 && v.it[find("SLOT3", OM_SV_SD)].set == 1);
    CHECK_INT(om_sv_list(&v, OM_SV_CON), 0);
    CHECK_INT(v.n, 1);                                            /* this game's only (a name its sets hold) */
    CHECK(find("SAVE_B", OM_SV_CON) >= 0);
    CHECK(find("OTHER", OM_SV_CON) < 0);
    CHECK_INT(open_(""), 0);                                      /* every save */
    CHECK_INT(om_sv_list(&v, OM_SV_CON), 0);
    CHECK_INT(v.n, 2);
    CHECK_INT(open_(GID), 0);
    CHECK_INT(om_sv_list(&v, OM_SV_CART), OM_SVR_NODEV);          /* no cartridge */
    /* the older set (the folder's own files) is history: the global view lists folders */
    {
        char f[8][OM_PGS_DIR];
        uint32_t n;
        mkdirs("SAVES/_GENERAL");
        mkdirs("SAVES/_BACKUP");
        CHECK_INT(om_sv_folders(&v, f, 8, &n), 0);
        CHECK_INT(n, 2);
        CHECK_STR(f[0], "_GENERAL");
        CHECK_STR(f[1], "MK-81045[U]");
    }
}

static void backup_and_delete(void)
{
    int k;
    wipe();
    put(GDIR, "SAVE_A.BUP", "SAVE_A", 100, 5);
    put(GDIR, "SAVE_B.BUP", "SAVE_B", 200, 6);
    bram("SAVE_A", 100, 5);
    CHECK_INT(open_(GID), 0);
    /* back up a console save: a new SAVES/_MANAGER/00001/ */
    CHECK_INT(om_sv_list(&v, OM_SV_CON), 0);
    {
        uint32_t seq = 0;
        CHECK_INT(om_sv_backup(&v, &v.it[find("SAVE_A", OM_SV_CON)], &seq), 0);
        CHECK_INT(seq, 1);
        CHECK(exists("SAVES/_MANAGER/00001/SAVE_A.BUP"));
        CHECK(!exists("SAVES/_MANAGER/00001/SAVE_A.TMP"));
        CHECK_INT(om_sv_backup(&v, &v.it[find("SAVE_A", OM_SV_CON)], &seq), 0);
        CHECK_INT(seq, 2);                                         /* never over the first */
    }
    /* delete from the card: a new complete generation without it; the old files stay */
    CHECK_INT(om_sv_list(&v, OM_SV_SD), 0);
    k = find("SAVE_B", OM_SV_SD);
    CHECK_INT(om_sv_delete(&v, &v.it[k]), 0);
    CHECK(exists(GDIR "/SAVE_B.BUP"));
    CHECK(exists(GDIR "/G000/G00001/DONE.TXT"));
    CHECK(exists(GDIR "/G000/G00001/SAVE_A.BUP"));
    CHECK(!exists(GDIR "/G000/G00001/SAVE_B.BUP"));
    CHECK_INT(om_sv_list(&v, OM_SV_SD), 0);
    CHECK_INT(v.n, 1);
    CHECK(find("SAVE_B", OM_SV_SD) < 0);
    /* no marker: the console holds the general set, so a game folder's save is not "also in console" */
    {
        om_sv_item c;
        CHECK_INT(om_sv_in_console(&v, &v.it[find("SAVE_A", OM_SV_SD)], &c), 0);
    }
    /* delete from the console: the whole memory backed up (.BKP) and the save copied first */
    CHECK_INT(om_sv_list(&v, OM_SV_CON), 0);
    CHECK_INT(om_sv_delete(&v, &v.it[find("SAVE_A", OM_SV_CON)]), 0);
    CHECK_INT(fake_bram_find("SAVE_A"), -1);
    CHECK(exists("SAVES/_MANAGER/00003/SAVE_A.BUP"));
    CHECK(count_dir("SAVES/_BACKUP") >= 1);
    CHECK_INT(fake_addonly_violations, 0);
}

static void copies(void)
{
    int k;
    wipe();
    put(GDIR, "SAVE_A.BUP", "SAVE_A", 100, 5);
    put("SAVES/MK-81500[U]", "OTHERS.BUP", "OTHERS", 50, 5);
    bram("SAVE_A", 100, 4);                                       /* another version in the console */
    fake_bram_cart = 1;
    CHECK_INT(open_(GID), 0);
    CHECK_INT(om_sv_list(&v, OM_SV_SD), 0);
    k = find("SAVE_A", OM_SV_SD);
    CHECK_INT(om_sv_copy(&v, &v.it[k], OM_SV_CON, 0), OM_SV_ASK);   /* a save of that name: asks */
    CHECK_INT(om_sv_copy(&v, &v.it[k], OM_SV_CON, 1), 0);          /* confirmed: guarded, then written over */
    CHECK(exists("SAVES/_MANAGER/00001/SAVE_A.BUP"));             /* the console's version kept first */
    CHECK_INT(om_sv_copy(&v, &v.it[k], OM_SV_CART, 0), 0);         /* to the cartridge */
    CHECK_INT(om_bram_use(1), 0);
    CHECK(fake_bram_find("SAVE_A") >= 0);
    CHECK_INT(om_bram_use(0), 0);
    CHECK_INT(om_sv_list(&v, OM_SV_CART), 0);
    CHECK_INT(v.n, 1);
    /* with another game's set in the console: refused */
    {
        static const char line[] = "MK-81500@U 0 12345678\n";
        uint32_t i;
        om_bram_use(0);
        for (i = 0; i < sizeof line - 1; i++) data[i] = (uint8_t)line[i];
        fake_bram_put("VIBEMENU_PG", data, sizeof line - 1, 0);
    }
    CHECK_INT(open_(GID), 0);                                     /* no ACTIVE.TXT of its own: the set is unknown */
    CHECK(v.marker && v.foreign);
    CHECK_INT(om_sv_list(&v, OM_SV_SD), 0);
    CHECK_INT(om_sv_copy(&v, &v.it[find("SAVE_A", OM_SV_SD)], OM_SV_CON, 1), OM_SVR_FOREIGN);
    {
        char p[700];
        FILE *fa;
        snprintf(p, sizeof p, "%s/aether/SAVES/ACTIVE.TXT", root);
        fa = fopen(p, "wb");
        fputs("MK-81500@U 0 12345678 00000000\n", fa);
        fclose(fa);
    }
    CHECK_INT(open_(GID), 0);
    CHECK(v.marker && !v.foreign);
    CHECK_INT(om_sv_list(&v, OM_SV_SD), 0);
    CHECK_INT(om_sv_copy(&v, &v.it[find("SAVE_A", OM_SV_SD)], OM_SV_CON, 1), OM_SVR_OTHERSET);
    /* that game's own save: its set is the console's -> allowed, and "also in console" */
    CHECK_INT(om_sv_set_folder(&v, "MK-81500[U]"), 0);
    CHECK_INT(om_sv_list(&v, OM_SV_SD), 0);
    k = find("OTHERS", OM_SV_SD);
    CHECK_INT(om_sv_copy(&v, &v.it[k], OM_SV_CON, 0), 0);
    {
        om_sv_item c;
        CHECK_INT(om_sv_in_console(&v, &v.it[k], &c), 1);
    }
    /* a save swap unfinished, even with only the journal's .NEW left by a cut: nothing is written */
    touch("SAVES/JOURNAL.TXT.NEW");
    CHECK_INT(open_(GID), 0);
    CHECK(v.journal);
    touch("SAVES/JOURNAL.TXT");
    CHECK_INT(open_(GID), 0);
    CHECK_INT(om_sv_list(&v, OM_SV_SD), 0);
    CHECK_INT(om_sv_delete(&v, &v.it[0]), OM_SVR_JOURNAL);
    CHECK_INT(om_sv_backup(&v, &v.it[0], 0), OM_SVR_JOURNAL);
    CHECK_INT(fake_addonly_violations, 0);
}

/* hardware 2026-11-27: a card no game has launched from has no SAVES folder; Manage saves' backup to the card makes
 * it (SAVES, then SAVES/_MANAGER) instead of failing */
static void fresh_card(void)
{
    char c[700];
    uint32_t seq = 0;
    wipe();
    snprintf(c, sizeof c, "rmdir %s/aether/SAVES", root);
    sh(c);
    CHECK(!exists("SAVES"));
    bram("SAVE_A", 100, 5);
    CHECK_INT(open_(""), 0);
    CHECK_INT(om_sv_list(&v, OM_SV_CON), 0);
    CHECK_INT(om_sv_backup(&v, &v.it[find("SAVE_A", OM_SV_CON)], &seq), 0);
    CHECK_INT(seq, 1);
    CHECK(exists("SAVES/_MANAGER/00001/SAVE_A.BUP"));
    CHECK_INT(fake_addonly_violations, 0);
}

int main(void)
{
    if (!mkdtemp(root)) { printf("FAIL mkdtemp\n"); return 1; }
    setenv("FAKE_SD_ROOT", root, 1);
    om_pgs_arena_split(&env, (uint8_t *)arena32);
    env.rel = 1;
    fake_addonly = 1;
    fake_nopath = 1;
    {
        static uint32_t d[4];
        wipe();
        memcpy(d, "/aether", 8);
        s_chdir((const char *)d);
    }
    listing();
    backup_and_delete();
    copies();
    fresh_card();
    printf("test_svmgr: %d passed, %d failed\n", th_pass, th_fail);
    return th_fail || !th_pass;
}
