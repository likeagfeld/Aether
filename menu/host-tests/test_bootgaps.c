/* menu/host-tests/test_bootgaps.c -- boot-animation stutter (hardware report on 3c64908, the SAROO card: 703-name root,
 * an 842-entry LIBRARY.IDX, a 168 KB GAMES.DB). Symptom: the light orbiting the logo on "LOADING YOUR LIBRARY" jumped
 * instead of moving smoothly. The loading screen draws a frame only when the boot code offers one -- main.c status()
 * between card.c's steps, the boot-step hook, and om_yield (yield.h) inside the loads -- and the comet's angle is
 * time-based, so a long stretch with no chance to draw shows as a jump. The REAL om_card_load (card.c, satfs.c, sd.c)
 * runs over a fake card of that shape; fake_satiator.c's virtual clock costs a read at 296 KB/s and any other command
 * 1 ms (a model). Gate: no stretch longer than one NTSC V-blank (16,683 us) without a chance to draw.
 * Part 2 (hardware report 2026-11-25, a 690-game card on FW163: "the animation starts fine and smoothly and then
 * briefly stops as [the] number of games [is] printed ... then moves again choppily until [the] menu loads"): the
 * start-up after the card load -- the folder tree, SETTINGS.INI, the path index's sort, FAVS.TXT, RECENT.TXT,
 * ACTIVE.TXT, the sort contexts and the first list -- ran with no chance to draw: the hook was set for om_card_load
 * only. On the card each path command walks the ~700-name exFAT root (perf-bigcard.md's L, 50-180 ms), so those eight
 * commands back to back were the stop, and the CPU work between the step frames (167 ms for the sort contexts on
 * Mednafen) the choppiness. Gates: at most one path command between two chances to draw (a single command is the
 * one thing that cannot be split), no stretch over a V-blank on the model clock, the CPU builders offer a chance every
 * few dozen items, and the results are byte-identical with and without the hook. BSD-3-Clause. */
#include <sys/stat.h>
#include "th.h"
#include "browse.h"
#include "card.h"
#include "libidx.h"
#include "libsort.h"
#include "persist.h"
#include "plist.h"
#include "satfs.h"
#include "sd.h"
#include "sdwrite.h"
#include "tree.h"
#include "yield.h"

#define NROOT  703u
#define NGAMES 842u
#define NFOLD  63u
#define VBLANK_US 16683u

extern uint64_t fake_us;
extern int fake_paths;
static uint8_t gdb[300000], art[140000], lib[220000], vrt[16384];
static om_scan_work work;
static om_lib_entry ent[NGAMES];
static uint16_t order[NGAMES];
static om_lib_folder fold[NFOLD + 1];
static char pool[200000], paths[NGAMES][80], fnames[NFOLD + 1][24];
static uint8_t idx[200000];
static uint64_t s_last, s_worst;
static int s_paths_last, s_paths_worst;
static const char *s_step = "start", *s_worst_at = "-", *s_paths_at = "-";
static uint32_t s_yields;

static void chance(void)
{
    uint64_t gap = fake_us - s_last;
    int np = fake_paths - s_paths_last;
    if (gap > s_worst) { s_worst = gap; s_worst_at = s_step; }
    if (np > s_paths_worst) { s_paths_worst = np; s_paths_at = s_step; }
    s_last = fake_us;
    s_paths_last = fake_paths;
}
static void st(void *ctx, const char *line) { (void)ctx; chance(); s_step = line; }
static void yield(void) { s_yields++; chance(); }

static void put(const char *root, const char *rel, const uint8_t *d, uint32_t n)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    f = fopen(p, "wb");
    if (n) fwrite(d, 1, n, f);
    fclose(f);
}

/* the start-up after om_card_load, in main.c's and folders.c's order (load_lists: om_persist_load, read_active, the
 * theme scan -- themes.c, VDP-bound, not built here -- om_sortctx_build, om_browse_init; then apply_view) */
typedef struct {
    om_tree tree;
    om_persist ps;
    om_sortctx sort;
    om_browse b;
    uint32_t act[80];
    uint32_t y_tree, y_persist, y_sort, y_browse;
} boot2;
static uint8_t stage[0x1F000] __attribute__((aligned(4)));

static void step(const char *name) { chance(); s_step = name; }   /* the boot-step hook's frame (interstitial.c) */

static void run2(boot2 *r, const om_card *c)
{
    uint32_t len = 0, y0;
    memset(r, 0, sizeof *r);
    y0 = s_yields;
    s_step = "folder tree";
    CHECK_INT(om_tree_build(&r->tree, &c->lib), OM_OK);
    r->y_tree = s_yields - y0;
    step("PERSIST: SETTINGS/FAVS/RECENT/ACTIVE");
    y0 = s_yields;
    om_persist_load(&r->ps, &c->lib, r->b.scratch, r->b.tmp, stage, sizeof stage);
    r->y_persist = s_yields - y0;
    (void)om_sdw_read_head(OM_PATH_ACTIVE, (uint8_t *)r->act, sizeof r->act, &len);   /* folders.c read_active */
    step("SORT: sort contexts, browse lists");
    y0 = s_yields;
    om_sortctx_build(&r->sort, &c->lib, NULL);
    r->y_sort = s_yields - y0;
    y0 = s_yields;
    om_browse_init(&r->b, &r->tree, &c->lib, &r->sort, NULL, &r->ps.favs, &r->ps.recent);
    om_browse_set(&r->b, OM_SORT_AZ, OM_FILTER_ANY, 0);                   /* folders.c apply_view */
    r->y_browse = s_yields - y0;
    step("ready");
}

static boot2 r_on, r_off;

int main(void)
{
    char root[] = "/tmp/omsat_boot_XXXXXX", p[600], favs[2048];
    static uint8_t junk[168014];
    om_card c;
    om_card_bufs b = { gdb, sizeof gdb, art, sizeof art, lib, sizeof lib, &work, vrt, sizeof vrt };
    om_libw w;
    uint32_t i, fp, ilen = 0, nfav = 0;
    CHECK(mkdtemp(root) != NULL);
    setenv("FAKE_SD_ROOT", root, 1);
    snprintf(p, sizeof p, "%s/aether", root); mkdir(p, 0755);
    for (i = 0; i < NROOT; i++) {                                  /* 34-35 B names, as the card's (G:\ mirror) */
        snprintf(p, sizeof p, "%s/Synthetic Game Title %04u (USA) [!]", root, (unsigned)i);
        mkdir(p, 0755);
    }
    memset(junk, 0x5A, sizeof junk);
    put(root, "aether/GAMES.DB", junk, sizeof junk);            /* the full DB's size: read whole, then rejected */
    om_put32(junk + 0x10, 64u); om_put32(junk + 0x14, 571u * 48u); /* an ART.PAK header naming a 571-entry table */
    put(root, "aether/ART.PAK", junk, 64u + 571u * 48u);
    CHECK_INT(om_satfs_root_fp(&work, &fp), OM_OK);               /* the index is current: no scan */
    om_libw_init(&w, ent, NGAMES, order, fold, NFOLD + 1, pool, sizeof pool);
    CHECK_INT(om_libw_folder(&w, "", 0xFFFF), 0);
    for (i = 1; i <= NFOLD; i++) {                                /* the user's card: 63 folders below the root */
        snprintf(fnames[i], sizeof fnames[i], "Folder %02u", (unsigned)i);
        CHECK_INT(om_libw_folder(&w, fnames[i], 0), (int)i);
    }
    favs[0] = 0;
    for (i = 0; i < NGAMES; i++) {
        om_lib_entry e;
        memset(&e, 0, sizeof e);
        snprintf(paths[i], sizeof paths[i], "Synthetic Game Title %04u (USA) [!]/Synthetic Game Title %04u.cue",
                 (unsigned)(i % NROOT), (unsigned)i);
        e.path = paths[i]; e.title = ""; e.image_type = 1; e.disc = e.disc_total = 1;
        e.folder = (uint16_t)(i % 4 ? 0 : 1 + (i / 4) % NFOLD);   /* a quarter in folders, the rest at the root */
        CHECK_INT(om_libw_entry(&w, &e), OM_OK);
        if (i % 97 == 5 && nfav < 8) { strcat(favs, paths[i]); strcat(favs, "\n"); nfav++; }
    }
    CHECK_INT(om_libw_finish(&w, 1, fp, 0, idx, sizeof idx, &ilen), OM_OK);
    put(root, "aether/LIBRARY.IDX", idx, ilen);
    put(root, "aether/SETTINGS.INI", (const uint8_t *)"theme=saturn\nclock=24\n", 22);
    put(root, "aether/FAVS.TXT", (const uint8_t *)favs, (uint32_t)strlen(favs));
    put(root, "aether/RECENT.TXT", (const uint8_t *)favs, (uint32_t)strlen(favs));   /* ACTIVE.TXT: absent */
    printf("test_bootgaps: fake card %u root names, LIBRARY.IDX %u B (%u folders), GAMES.DB %u B, ART.PAK table %u B\n",
           (unsigned)NROOT, (unsigned)ilen, (unsigned)NFOLD, (unsigned)sizeof junk, 571u * 48u);

    om_yield_fn = yield;                                           /* as interstitial.c sets it from the fade-in */
    s_last = fake_us;
    s_paths_last = fake_paths;
    CHECK_INT(om_card_load(&c, &b, st, NULL), OM_OK);
    CHECK_INT(c.lib_source, OM_LIB_FROM_IDX);
    CHECK_INT(c.lib.count, NGAMES);
    printf("test_bootgaps: symptom: the loading screen's orbiting light jumps instead of moving smoothly\n");
    printf("test_bootgaps: card load: worst stretch without a chance to draw %llu us (limit %u), in \"%s\"; %u yields\n",
           (unsigned long long)s_worst, VBLANK_US, s_worst_at, (unsigned)s_yields);
    CHECK(s_worst <= VBLANK_US);

    /* part 2: the rest of the start-up, the hook still set (interstitial.c clears it at om_istl_ready) */
    s_worst = 0;
    s_paths_worst = 0;
    s_worst_at = s_paths_at = "-";
#ifdef BOOTGAPS_OLD_LIFETIME
    om_yield_fn = 0;                                               /* RED check: main.c's old clear after the load */
#endif
    run2(&r_on, &c);
    om_yield_fn = 0;
    printf("test_bootgaps: symptom: the light stops as the counts appear, then moves choppily until the menu\n");
    printf("test_bootgaps: start-up: worst stretch %llu us in \"%s\"; most path commands in one stretch %d, in \"%s\" "
           "(each a walk of the card's root)\n", (unsigned long long)s_worst, s_worst_at, s_paths_worst, s_paths_at);
    printf("test_bootgaps: CPU chances: tree %u (%u folders), persist %u, sort contexts %u (%u games), browse %u\n",
           (unsigned)r_on.y_tree, (unsigned)c.lib.nfolders, (unsigned)r_on.y_persist, (unsigned)r_on.y_sort,
           (unsigned)c.lib.count, (unsigned)r_on.y_browse);
    CHECK(s_worst <= VBLANK_US);
    CHECK_INT(s_paths_worst, 1);
    CHECK(r_on.y_tree >= c.lib.nfolders);                         /* one a folder: folders x entries */
    CHECK(r_on.y_sort >= c.lib.count / 16u);                      /* one a 16 entries (a GAMES.DB search each) */
    CHECK(r_on.y_persist >= 64u);                                 /* the path index's sort: one a 64 merges (~5,700) */
    CHECK(r_on.y_browse >= 50u);                                  /* the root's list, sorted twice */
    CHECK_INT(r_on.ps.favs.n, (int)nfav);
    CHECK_INT(r_on.ps.recent.n, (int)nfav);

    /* the same start-up with no hook: every result byte-identical (the hook only draws) */
    run2(&r_off, &c);
    CHECK_INT(r_off.y_tree + r_off.y_persist + r_off.y_sort + r_off.y_browse, 0);
    r_off.y_tree = r_on.y_tree; r_off.y_persist = r_on.y_persist;
    r_off.y_sort = r_on.y_sort; r_off.y_browse = r_on.y_browse;
    CHECK(!memcmp(&r_on.tree, &r_off.tree, sizeof r_on.tree));
    CHECK(!memcmp(&r_on.ps, &r_off.ps, sizeof r_on.ps));
    CHECK(!memcmp(&r_on.sort, &r_off.sort, sizeof r_on.sort));
    CHECK(!memcmp(r_on.b.view, r_off.b.view, sizeof r_on.b.view));
    CHECK_INT(r_on.b.n, r_off.b.n);
    return th_done("test_bootgaps");
}
