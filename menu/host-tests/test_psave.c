/* menu/host-tests/test_psave.c -- the Manage saves popup's model (menu/pop/psave.c) over svmgr.c, fake_bram.c and
 * fake_satiator.c: the tabs and their rows, the Card tab's sets (newest first, the console's set tagged, alternates
 * apart) and a set's saves, the options per device, Delete asking twice and a copy over a same-named save asking
 * again, the operations through svmgr (the add-only card checked on every write: fake_addonly), the refusals as
 * messages, Settings' manager (the folders first, B back to them), and B closing. BSD-3-Clause. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "th.h"
#include "om_fmt.h"
#include "satiator.h"
#include "bupfile.h"
#include "psave.h"

extern int fake_addonly, fake_addonly_violations, fake_nopath, fake_bram_cart;
void fake_bram_reset(void);
void fake_bram_put(const char *name, const uint8_t *data, uint32_t size, uint32_t date);
int fake_bram_find(const char *name);

static char root[] = "/tmp/omsat_psave_XXXXXX";
static uint32_t arena32[OM_PGS_ARENA / 4 + 4];
static uint32_t big32[(OM_BUP_HDR + 0x18000u) / 4];
static om_sv_item items[300];
static char fold[64][OM_PGS_DIR];
static om_pgs_env env;
static om_sv v;
static om_ps ps;

#define K_U OM_PS_K_U
#define K_D OM_PS_K_D
#define K_L OM_PS_K_L
#define K_R OM_PS_K_R
#define K_A OM_PS_K_A
#define K_B OM_PS_K_B

static void sh(const char *cmd) { if (system(cmd)) printf("note: %s failed\n", cmd); }
static void wipe(void)
{
    char c[700];
    snprintf(c, sizeof c, "rm -rf %s/aether && mkdir -p %s/aether/SAVES", root, root);
    sh(c);
    fake_bram_reset();
    fake_bram_cart = 0;
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
    snprintf(p, sizeof p, "mkdir -p '%s/aether/%s'", root, rel_dir);
    sh(p);
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
static uint8_t data[4096];
static void bram(const char *name, uint32_t size, uint32_t date)
{
    uint32_t i;
    for (i = 0; i < size; i++) data[i] = (uint8_t)(i * 7u + date);
    fake_bram_put(name, data, size, date);
}
static int open_(const char *gid, int global)
{
    int rc = om_sv_open(&v, &env, gid, (uint8_t *)big32, sizeof big32, items, 300, "20261125-101500");
    om_ps_open(&ps, &v, global, fold, 64, 9);
    return rc;
}
static const char *row_name(uint16_t r)
{
    const om_sv_item *it;
    const om_ps_set *st;
    om_ps_row(&ps, r, &it, &st);
    return it ? it->d.name : st ? "<set>" : "";
}
static int row_of(const char *name)
{
    uint16_t r;
    for (r = 0; r < ps.nrow; r++) if (!strcmp(row_name(r), name)) return r;
    return -1;
}
static void go(int r)
{
    while (ps.row != r) om_ps_key(&ps, K_D);
}
static void run_pending(void)
{
    CHECK(om_ps_pending(&ps));
    om_ps_run(&ps);
    CHECK(!om_ps_pending(&ps));
}

#define GID "MK-81045@U"
#define GDIR "SAVES/MK-81045[U]"

static void tabs_and_sets(void)
{
    const om_sv_item *it;
    const om_ps_set *st;
    wipe();
    put(GDIR, "SAVE_A.BUP", "SAVE_A", 100, 5);
    put(GDIR "/G000/G00001", "SAVE_B.BUP", "SAVE_B", 50, 30);
    put(GDIR "/G000/G00001", "SAVE_C.BUP", "SAVE_C", 60, 31);
    touch(GDIR "/G000/G00001/DONE.TXT");
    put(GDIR "/G000/G00001/ALT", "SAVE_B.BUP", "SAVE_B", 70, 2);
    put(GDIR "/03", "SLOT3.BUP", "SLOT3", 30, 90);                 /* the newest set */
    bram("SAVE_B", 50, 30);
    bram("OTHER", 40, 1);
    CHECK_INT(open_(GID, 0), 0);
    CHECK_INT(ps.page, OM_PS_LIST);
    CHECK_INT(ps.tab, OM_SV_CON);
    CHECK_INT(ps.nrow, 1);                                         /* this game's console saves only */
    CHECK_STR(row_name(0), "SAVE_B");
    CHECK_INT(om_ps_key(&ps, K_R), OM_PS_MOVED);                   /* Cartridge: none here */
    CHECK_INT(ps.tab, OM_SV_CART);
    CHECK_INT(ps.nrow, 0);
    CHECK_INT(ps.list_rc, OM_SVR_NODEV);
    CHECK_INT(om_ps_key(&ps, K_R), OM_PS_MOVED);                   /* Card: its sets */
    CHECK_INT(ps.tab, OM_SV_SD);
    CHECK_INT(ps.nrow, 3);                                         /* slot 0, its ALT, slot 3 */
    om_ps_row(&ps, 0, &it, &st);
    CHECK(st && st->set == 1 && st->n == 1 && st->newest == 90);   /* newest first: slot 3 */
    om_ps_row(&ps, 1, &it, &st);
    CHECK(st && st->set == 0 && !st->alt && st->n == 2);
    CHECK(st && !st->loaded);                                      /* no marker: the console holds the general set */
    om_ps_row(&ps, 2, &it, &st);
    CHECK(st && st->alt && st->n == 1);
    CHECK_INT(om_ps_key(&ps, K_L), OM_PS_MOVED);                   /* wraps the other way too */
    CHECK_INT(ps.tab, OM_SV_CART);
    CHECK_INT(om_ps_key(&ps, K_L), OM_PS_MOVED);
    CHECK_INT(om_ps_key(&ps, K_L), OM_PS_MOVED);
    CHECK_INT(ps.tab, OM_SV_SD);
    go(1);
    CHECK_INT(om_ps_key(&ps, K_A), OM_PS_OK);                      /* open the set */
    CHECK_INT(ps.sdlev, 1);
    CHECK_INT(ps.nrow, 2);
    CHECK(row_of("SAVE_B") >= 0 && row_of("SAVE_C") >= 0);
    CHECK_INT(om_ps_key(&ps, K_B), OM_PS_BACK);                    /* back to the sets, on that set */
    CHECK_INT(ps.sdlev, 0);
    CHECK_INT(ps.row, 1);
    /* a fresh press wraps, a held repeat stops */
    go(2);
    CHECK_INT(om_ps_key(&ps, K_D | OM_PS_K_REP), 0);
    CHECK_INT(om_ps_key(&ps, K_D), OM_PS_MOVED);
    CHECK_INT(ps.row, 0);
    CHECK_INT(om_ps_key(&ps, K_B), OM_PS_CLOSE);
}

static void options_and_ops(void)
{
    int r;
    wipe();
    put(GDIR, "SAVE_A.BUP", "SAVE_A", 100, 5);
    put(GDIR, "SAVE_B.BUP", "SAVE_B", 200, 6);
    bram("SAVE_A", 100, 5);
    CHECK_INT(open_(GID, 0), 0);
    /* console: Copy to the card, Copy to the cartridge, Back up now, Delete... */
    CHECK_INT(om_ps_key(&ps, K_A), OM_PS_OK);
    CHECK_INT(ps.page, OM_PS_ACT);
    CHECK_INT(ps.nact, 4);
    CHECK_STR(om_ps_act_label(&ps, 0), "Copy to the card");
    CHECK_STR(om_ps_act_label(&ps, 1), "Copy to the cartridge");
    CHECK_STR(om_ps_act_label(&ps, 2), "Back up now");
    CHECK_STR(om_ps_act_label(&ps, 3), "Delete\xE2\x80\xA6");
    CHECK_INT(om_ps_key(&ps, K_A), OM_PS_OK);                      /* Copy to the card: runs next frame */
    CHECK_INT(om_ps_key(&ps, K_D), 0);                             /* nothing else while it is pending */
    run_pending();
    CHECK_INT(ps.msgk, OM_PSM_OK);
    CHECK(exists("SAVES/_MANAGER/00001/SAVE_A.BUP"));
    CHECK_INT(ps.page, OM_PS_LIST);
    /* Back up now: a .BKP generation of the whole console; again: the same, nothing written */
    om_ps_key(&ps, K_A);
    om_ps_key(&ps, K_D); om_ps_key(&ps, K_D);
    CHECK_INT(ps.acts[ps.act], OM_PSA_BACKUP_ALL);
    om_ps_key(&ps, K_A);
    run_pending();
    CHECK_STR(ps.msg, "Console memory backed up");
    om_ps_key(&ps, K_A);
    om_ps_key(&ps, K_D); om_ps_key(&ps, K_D);
    om_ps_key(&ps, K_A);
    run_pending();
    CHECK_STR(ps.msg, "Already backed up: nothing changed");
    /* Copy to the cartridge with none there: refused, said why */
    om_ps_key(&ps, K_A);
    om_ps_key(&ps, K_D);
    om_ps_key(&ps, K_A);
    run_pending();
    CHECK_INT(ps.msgk, OM_PSM_ERR);
    CHECK_STR(ps.msg, om_sv_why(OM_SVR_NODEV));
    /* Delete asks twice; a move disarms; B disarms first, then goes back */
    om_ps_key(&ps, K_A);
    om_ps_key(&ps, K_U);                                           /* wraps to Delete... */
    CHECK_INT(ps.acts[ps.act], OM_PSA_DELETE);
    CHECK_INT(om_ps_key(&ps, K_A), OM_PS_OK);
    CHECK(ps.armed && !om_ps_pending(&ps));
    CHECK_STR(om_ps_act_label(&ps, ps.act), "Press A again to delete");
    om_ps_key(&ps, K_U);
    CHECK(!ps.armed);
    om_ps_key(&ps, K_D);
    om_ps_key(&ps, K_A);
    CHECK_INT(om_ps_key(&ps, K_B), OM_PS_BACK);
    CHECK(!ps.armed && ps.page == OM_PS_ACT);
    om_ps_key(&ps, K_A);
    CHECK_INT(om_ps_key(&ps, K_A), OM_PS_OK);
    run_pending();
    CHECK_INT(ps.msgk, OM_PSM_OK);
    CHECK_INT(fake_bram_find("SAVE_A"), -1);                       /* gone from the console... */
    CHECK(exists("SAVES/_MANAGER/00002/SAVE_A.BUP"));              /* ...after its copy and a .BKP */
    CHECK_INT(ps.nrow, 0);
    /* the card: SAVE_A into the console (no marker: the general set takes any save) */
    om_ps_key(&ps, K_L);                                           /* Card */
    CHECK_INT(ps.tab, OM_SV_SD);
    om_ps_key(&ps, K_A);                                           /* its one set */
    r = row_of("SAVE_A");
    CHECK(r >= 0);
    go(r);
    om_ps_key(&ps, K_A);
    CHECK_INT(ps.nact, 3);
    CHECK_STR(om_ps_act_label(&ps, 0), "Copy to the console");
    om_ps_key(&ps, K_A);
    run_pending();
    CHECK_INT(ps.msgk, OM_PSM_OK);
    CHECK(fake_bram_find("SAVE_A") >= 0);
    /* again: the console has it -- asks, then replaces (backed up first) */
    go(row_of("SAVE_A"));
    om_ps_key(&ps, K_A);
    om_ps_key(&ps, K_A);
    run_pending();
    CHECK_INT(ps.msgk, OM_PSM_ASK);
    CHECK(ps.armed && ps.page == OM_PS_ACT);
    CHECK_STR(om_ps_act_label(&ps, ps.act), "A again replaces it");
    om_ps_key(&ps, K_A);
    run_pending();
    CHECK_INT(ps.msgk, OM_PSM_OK);
    CHECK(exists("SAVES/_MANAGER/00003/SAVE_A.BUP"));
    /* delete from the card: a new generation without it; the old file stays */
    go(row_of("SAVE_B"));
    om_ps_key(&ps, K_A);
    om_ps_key(&ps, K_U);
    om_ps_key(&ps, K_A);
    om_ps_key(&ps, K_A);
    run_pending();
    CHECK_INT(ps.msgk, OM_PSM_OK);
    CHECK(exists(GDIR "/SAVE_B.BUP"));
    CHECK(exists(GDIR "/G000/G00001/DONE.TXT"));
    CHECK(!exists(GDIR "/G000/G00001/SAVE_B.BUP"));
    CHECK_INT(ps.sdlev, 1);                                        /* still in the set */
    CHECK(row_of("SAVE_B") < 0);
    CHECK_INT(fake_addonly_violations, 0);
}

static void refusals(void)
{
    wipe();
    put(GDIR, "SAVE_A.BUP", "SAVE_A", 100, 5);
    touch("SAVES/JOURNAL.TXT");                                    /* a save swap unfinished: nothing is written */
    CHECK_INT(open_(GID, 0), 0);
    om_ps_key(&ps, K_L);
    om_ps_key(&ps, K_A);
    om_ps_key(&ps, K_A);
    om_ps_key(&ps, K_A);
    run_pending();
    CHECK_INT(ps.msgk, OM_PSM_ERR);
    CHECK_STR(ps.msg, om_sv_why(OM_SVR_JOURNAL));
    CHECK_INT(fake_bram_find("SAVE_A"), -1);
    /* another game's set in the console: an SD save does not go in */
    wipe();
    put(GDIR, "SAVE_A.BUP", "SAVE_A", 100, 5);
    put("SAVES/T-1234G[J]", "OTH.BUP", "OTH", 10, 1);
    {
        static const char mk[] = "T-1234G@J 0 12345678\n";
        static const char act[] = "T-1234G@J 0 12345678 00000000\n";
        char p[700];
        FILE *f;
        fake_bram_put("VIBEMENU_PG", (const uint8_t *)mk, (uint32_t)strlen(mk), 0);
        snprintf(p, sizeof p, "%s/aether/SAVES/ACTIVE.TXT", root);
        f = fopen(p, "wb"); fputs(act, f); fclose(f);
    }
    CHECK_INT(open_(GID, 0), 0);
    CHECK(v.marker && !v.foreign);
    om_ps_key(&ps, K_L);
    {
        const om_sv_item *it;
        const om_ps_set *st;
        om_ps_row(&ps, 0, &it, &st);
        CHECK(st && !st->loaded);                                  /* the console holds the other game's set */
    }
    om_ps_key(&ps, K_A);
    om_ps_key(&ps, K_A);
    om_ps_key(&ps, K_A);
    run_pending();
    CHECK_INT(ps.msgk, OM_PSM_ERR);
    CHECK_STR(ps.msg, om_sv_why(OM_SVR_OTHERSET));
    CHECK_INT(fake_bram_find("SAVE_A"), -1);
    /* that game's own folder: its set is tagged loaded */
    CHECK_INT(om_sv_set_folder(&v, "T-1234G[J]"), 0);
    CHECK_INT(om_sv_set_loaded(&v, 0), 1);
    CHECK_INT(om_sv_set_loaded(&v, 5), 0);
    /* Back up now refuses while a save swap is unfinished */
    touch("SAVES/JOURNAL.TXT");
    CHECK_INT(open_(GID, 0), 0);
    CHECK_INT(om_sv_backup_all(&v, NULL, NULL), OM_SVR_JOURNAL);
}

static void global_view(void)
{
    wipe();
    put(GDIR, "SAVE_A.BUP", "SAVE_A", 100, 5);
    put("SAVES/_GENERAL", "GEN.BUP", "GEN", 20, 3);
    put("SAVES/T-1234G[J]", "OTH.BUP", "OTH", 10, 1);
    bram("OTHER", 40, 1);
    CHECK_INT(open_("", 1), 0);
    CHECK_INT(ps.page, OM_PS_GAMES);
    CHECK_INT(ps.nfold, 3);
    CHECK_STR(om_ps_folder(&ps, 0), "_GENERAL");
    CHECK_INT(ps.grow, 0);                                         /* row 0: Import from Rings / Kai */
    CHECK_INT(om_ps_key(&ps, K_A), OM_PS_IMPORT | OM_PS_OK);       /* the popup closes, the menu imports */
    CHECK_INT(ps.page, OM_PS_GAMES);
    om_ps_key(&ps, K_U);                                           /* wraps to the last folder */
    CHECK_INT(ps.grow, 3);
    om_ps_key(&ps, K_D);                                           /* and back to the import row */
    CHECK_INT(ps.grow, 0);
    om_ps_key(&ps, K_D);                                           /* _GENERAL */
    om_ps_key(&ps, K_D);
    CHECK_INT(om_ps_key(&ps, K_A), OM_PS_OK);
    CHECK_INT(ps.page, OM_PS_LIST);
    CHECK_INT(ps.tab, OM_SV_SD);
    CHECK_INT(ps.nrow, 1);
    om_ps_key(&ps, K_A);
    CHECK_INT(ps.nrow, 1);                                         /* that folder's save */
    CHECK_INT(om_ps_key(&ps, K_B), OM_PS_BACK);                    /* the sets */
    CHECK_INT(om_ps_key(&ps, K_B), OM_PS_BACK);                    /* the game list */
    CHECK_INT(ps.page, OM_PS_GAMES);
    om_ps_key(&ps, K_A);
    om_ps_key(&ps, K_R);                                           /* Console: every save (no game chosen by id) */
    CHECK_INT(ps.tab, OM_SV_CON);
    CHECK_INT(ps.nrow, 1);
    CHECK_STR(row_name(0), "OTHER");
    om_ps_key(&ps, K_B);
    CHECK_INT(om_ps_key(&ps, K_B), OM_PS_CLOSE);
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
    tabs_and_sets();
    options_and_ops();
    refusals();
    global_view();
    printf("test_psave: %d passed, %d failed\n", th_pass, th_fail);
    return th_fail || !th_pass;
}
