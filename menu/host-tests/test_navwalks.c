/* menu/host-tests/test_navwalks.c -- big-card stutter (hardware report 2026-11-23, SAROO card, 703-name root): every
 * path the menu hands the Satiator (s_open, a named s_stat, s_opendir) is resolved from the card's root, which costs
 * O(root names) on the card. Symptom: the list froze ~150 ms after the cursor rested and the music cut out, because a
 * rest ran 4 of them in one frame (the IP.BIN read) and every cover job 2-3 more (BOX.TGA stat+open, ART.PAK open).
 * Gates:
 *   G1 the rest frame issues at most 1 path command: folders.c's navigation loop never reads IP.BIN (om_info_load is
 *      reached only from game_gid, i.e. the launch panel and the launch), and a cover job's first frame opens at most
 *      one file, with no stat;
 *   G2 a game whose cover is in ART.PAK costs 0 path commands once ART.PAK is open -- even with LIBRARY.IDX's BOX flag
 *      set, as the card builder sets it (the user's BOX.TGA is already the ART.PAK cover, bdf5540).
 * Investigation: .superpowers/sdd/2026-11-20-m6-loaders-saves-cheats/perf-bigcard.md. BSD-3-Clause. */
#include <sys/stat.h>
#include "th.h"
#include "artjob.h"
#include "sd.h"

extern int fake_paths, fake_open_now;
static uint32_t stage32[(130 * 1024) / 4];
static om_art_out out;
static om_artjob j;

static void put(const char *root, const char *rel, const uint8_t *d, uint32_t n)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    f = fopen(p, "wb");
    fwrite(d, 1, n, f);
    fclose(f);
}

static void tga2x2(uint8_t *f)
{
    static const uint8_t px[12] = { 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255 };
    memset(f, 0, 18);
    f[2] = 2; f[12] = 2; f[14] = 2; f[16] = 24; f[17] = 0x20;
    memcpy(f + 18, px, 12);
}

/* one cover job as menu/ui/art.c runs it: job_start + the first step in one frame, then one step a frame.
 * Returns the path commands of the first frame; *rest gets those of every later frame. */
static int job(const om_artpak *a, const om_lib_entry *e, int *rest, int *src)
{
    int p0 = fake_paths, first, n;
    om_artjob_start(&j, a, e, NULL, (uint8_t *)stage32, sizeof stage32);
    n = om_artjob_step(&j, &out, OM_AJ_READS_PER_STEP);
    first = fake_paths - p0;
    p0 = fake_paths;
    while (!n) n = om_artjob_step(&j, &out, OM_AJ_READS_PER_STEP);
    *rest = fake_paths - p0;
    *src = j.hero_src;
    return first;
}

static int count(const char *text, const char *needle)
{
    int k = 0;
    const char *s = text;
    while ((s = strstr(s, needle)) != NULL) { k++; s += strlen(needle); }
    return k;
}

int main(void)
{
    char root[] = "/tmp/omsat_nav_XXXXXX", p[600];
    uint8_t tga[30], *pak, *src;
    uint32_t pak_n, src_n;
    om_artpak a;
    om_lib_entry e;
    int i, first, rest, hs, worst = 0, total = 0;
    CHECK(mkdtemp(root) != NULL);
    setenv("FAKE_SD_ROOT", root, 1);
    snprintf(p, sizeof p, "%s/aether", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/Racing", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/Racing/Daytona USA", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/Homebrew", root); mkdir(p, 0755);
    pak = th_load(FIX("tiny.art.pak"), &pak_n);
    put(root, "aether/ART.PAK", pak, pak_n);
    tga2x2(tga);
    put(root, "Racing/Daytona USA/BOX.TGA", tga, 30);
    put(root, "Homebrew/BOX.TGA", tga, 30);
    CHECK_INT(om_artpak_open(&a, pak, pak_n, pak_n), OM_OK);

    /* G1 (source): the navigation loop never reads IP.BIN; game_gid (launch panel, launch) is the one caller */
    src = th_load("../views/folders.c", &src_n);
    src[src_n] = 0;
    CHECK_INT(count((const char *)src, "om_info_load("), 1);
    /* 2026-11-24 (multi-disc): game_gid reads the set's lead disc first, then the game's own, in one loop */
    CHECK(strstr((const char *)src, "ip = om_info_find(at);\n        if (!ip && om_info_load(x, at) == 0)") != NULL);
    printf("test_navwalks: om_info_load call sites in folders.c: %d (want 1, game_gid)\n", count((const char *)src, "om_info_load("));

    /* G2: GS-9001 has its cover in ART.PAK and the BOX flag set (the card builder packs the user's BOX.TGA) */
    memset(&e, 0, sizeof e);
    strcpy(e.key, "GS-9001"); e.disc = 1; e.flags = OM_LIB_BOX; e.path = "Racing/Daytona USA/daytona.cue"; e.title = "";
    first = job(&a, &e, &rest, &hs);                               /* the first job opens ART.PAK: 1 */
    CHECK_INT(hs, OM_HERO_PAK);
    CHECK(first + rest <= 1);
    for (i = 0; i < 10; i++) {                                     /* cursor moves on and back: same game, then JP-only */
        int f2, r2;
        f2 = job(&a, &e, &r2, &hs);
        CHECK_INT(hs, OM_HERO_PAK);
        total += f2 + r2;
        if (f2 > worst) worst = f2;
    }
    printf("test_navwalks: G2 ART.PAK cover, 10 jobs after the first: %d path commands (want 0)\n", total);
    CHECK_INT(total, 0);
    CHECK(fake_open_now <= 1);                                     /* the 2-file rule: ART.PAK is the art's one file */

    /* G1 (art): a BOX.TGA-only game (no ART.PAK key) -- its first frame opens BOX.TGA with no separate stat */
    memset(&e, 0, sizeof e);
    e.flags = OM_LIB_BOX; e.path = "Homebrew/game.iso"; e.title = "";
    first = job(&a, &e, &rest, &hs);
    CHECK_INT(hs, OM_HERO_BOX);
    printf("test_navwalks: G1 BOX.TGA fallback first frame: %d path commands (want <= 1)\n", first);
    CHECK(first <= 1);
    CHECK(fake_open_now <= 1);                                     /* BOX.TGA done: at most ART.PAK still open */
    if (first > worst) worst = first;
    printf("test_navwalks: worst rest-frame path commands %d\n", worst);

    om_artjob_release();                                           /* om_art_invalidate: no art file left open */
    CHECK_INT(fake_open_now, 0);
    return th_done("test_navwalks");
}
