#include <sys/stat.h>
#include "th.h"
#include "artjob.h"
#include "sd.h"

extern int fake_reads;
static uint32_t stage32[(130 * 1024) / 4];
static om_art_out out;
static om_artjob j;
static uint8_t *pak;
static uint32_t pak_n;
static char longpath[300];   /* coordinator fix round 1, point 3: longer than box[] can ever hold */

static void put(const char *root, const char *rel, const uint8_t *d, uint32_t n)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    f = fopen(p, "wb");
    fwrite(d, 1, n, f);
    fclose(f);
}

static int run(int *steps)
{
    int before, n = 0;
    for (;;) {
        before = fake_reads;
        n++;
        if (om_artjob_step(&j, &out, OM_AJ_READS_PER_STEP)) break;
        CHECK(fake_reads - before <= OM_AJ_READS_PER_STEP);       /* never more than 4 reads per frame */
        if (n > 1000) { CHECK(0); break; }
    }
    *steps = n;
    return n;
}

static void tga2x2(uint8_t *f, uint8_t type)
{
    static const uint8_t px[12] = { 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255 };
    memset(f, 0, 18);
    f[2] = type; f[12] = 2; f[14] = 2; f[16] = 24; f[17] = 0x20;
    memcpy(f + 18, px, 12);
}

int main(void)
{
    char root[] = "/tmp/omsat_art_XXXXXX", p[600];
    uint8_t tga[30];
    om_artpak a;
    om_lib_entry e;
    om_art_entry ae;
    int steps;
    uint8_t *m;
    uint32_t k;
    CHECK(mkdtemp(root) != NULL);
    setenv("FAKE_SD_ROOT", root, 1);
    snprintf(p, sizeof p, "%s/aether", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/Racing", root); mkdir(p, 0755);
    pak = th_load(FIX("tiny.art.pak"), &pak_n);
    put(root, "aether/ART.PAK", pak, pak_n);
    CHECK_INT(om_artpak_open(&a, pak, pak_n, pak_n), OM_OK);
    /* 1. folder art for "RPG" (key FD6B34428), 25 hero sectors */
    om_artjob_start(&j, &a, NULL, "RPG", (uint8_t *)stage32, sizeof stage32);
    run(&steps);
    CHECK_INT(j.hero_src, OM_HERO_PAK); CHECK_INT(j.tall, 0); CHECK_INT(j.err, 0);
    om_artpak_find(&a, "FD6B34428", 0, &ae);
    CHECK_INT(out.hero[0], om_be16(pak + om_art_blob_pos(&a, ae.hero_sec)));
    CHECK(steps >= 7);                                             /* 51,200 B / (4 x 2 KB) per step */
    printf("test_artjob: folder art took %d steps\n", steps);
    /* 2. game without BOX: (GS-9001, disc 1) -> (GS-9001, 0) */
    memset(&e, 0, sizeof e);
    strcpy(e.key, "GS-9001"); e.disc = 1; e.path = "Racing/Daytona USA/daytona.cue"; e.title = "";
    om_artjob_start(&j, &a, &e, NULL, (uint8_t *)stage32, sizeof stage32);
    run(&steps);
    CHECK_INT(j.hero_src, OM_HERO_PAK); CHECK_INT(j.has_jp, 0);
    /* 3. JP only: (T-4507G, 1) has thumb + JP, no hero -> placeholder hero + JP bitmap */
    strcpy(e.key, "T-4507G"); e.disc = 1;
    om_artjob_start(&j, &a, &e, NULL, (uint8_t *)stage32, sizeof stage32);
    run(&steps);
    CHECK_INT(j.hero_src, OM_HERO_PLACEHOLDER); CHECK_INT(j.has_jp, 1);
    om_artpak_find(&a, "T-4507G", 1, &ae);
    CHECK(memcmp(out.jp, pak + om_art_blob_pos(&a, ae.jp_sec), 8) == 0);   /* row 0: 16 px = 8 bytes */
    CHECK_INT(out.jp[8], 0);                                       /* rest of the 160-px canvas row is transparent */
    CHECK(memcmp(out.jp + 80, pak + om_art_blob_pos(&a, ae.jp_sec) + 8, 8) == 0);   /* row 1 at stride 80 */
    /* 4. ART.PAK's cover comes first (spec §5.6 step 1): the card builder packs the game's own BOX.TGA as it, so the
     * file on the card is not read (big-card stutter, 2026-11-23: a BOX.TGA costs a path resolution from the root) */
    snprintf(p, sizeof p, "%s/Racing/Daytona USA", root); mkdir(p, 0755);
    tga2x2(tga, 2); put(root, "Racing/Daytona USA/BOX.TGA", tga, 30);
    strcpy(e.key, "GS-9001"); e.disc = 1; e.flags = OM_LIB_BOX;
    om_artjob_start(&j, &a, &e, NULL, (uint8_t *)stage32, sizeof stage32);
    run(&steps);
    CHECK_INT(j.hero_src, OM_HERO_PAK); CHECK_INT(j.err, 0);
    /* 4b. no ART.PAK cover for the key -> BOX.TGA (step 3), its size found by its reads (no stat) */
    strcpy(e.key, "NOPE-0001"); e.disc = 0;
    om_artjob_start(&j, &a, &e, NULL, (uint8_t *)stage32, sizeof stage32);
    run(&steps);
    CHECK_INT(j.hero_src, OM_HERO_BOX); CHECK_INT(j.err, 0); CHECK_INT(j.seg[0].len, 30);
    /* 5. refused BOX.TGA (RLE) -> placeholder + visible error, no silent fallback */
    tga2x2(tga, 10); put(root, "Racing/Daytona USA/BOX.TGA", tga, 30);
    om_artjob_start(&j, &a, &e, NULL, (uint8_t *)stage32, sizeof stage32);
    run(&steps);
    CHECK_INT(j.hero_src, OM_HERO_PLACEHOLDER); CHECK_INT(j.err, OM_TGA_E_TYPE);
    CHECK_STR(j.errmsg, "TGA type not supported");
    /* 5b. a BOX.TGA bigger than the stage -> named error, nothing decoded */
    m = (uint8_t *)calloc(1, sizeof stage32);
    put(root, "Racing/Daytona USA/BOX.TGA", m, (uint32_t)sizeof stage32 - 3u);
    free(m);
    om_artjob_start(&j, &a, &e, NULL, (uint8_t *)stage32, sizeof stage32);
    run(&steps);
    CHECK_INT(j.hero_src, OM_HERO_PLACEHOLDER); CHECK_INT(j.err, OM_E_FULL);
    CHECK_STR(j.errmsg, "ART: BOX.TGA too big");
    /* 5c. BOX flag, no ART.PAK cover, file gone -> placeholder, no error (the flag is stale) */
    snprintf(p, sizeof p, "%s/Racing/Daytona USA/BOX.TGA", root); remove(p);
    om_artjob_start(&j, &a, &e, NULL, (uint8_t *)stage32, sizeof stage32);
    run(&steps);
    CHECK_INT(j.hero_src, OM_HERO_PLACEHOLDER); CHECK_INT(j.err, 0);
    strcpy(e.key, "GS-9001"); e.disc = 1;
    /* 6. BOX flag but file gone -> ART.PAK */
    snprintf(p, sizeof p, "%s/Racing/Daytona USA/BOX.TGA", root); remove(p);
    om_artjob_start(&j, &a, &e, NULL, (uint8_t *)stage32, sizeof stage32);
    run(&steps);
    CHECK_INT(j.hero_src, OM_HERO_PAK); CHECK_INT(j.err, 0);
    /* 7. corrupt a hero byte on the card -> CRC failure is visible, placeholder shown */
    m = th_dup(pak, pak_n);
    om_artpak_find(&a, "GS-9001", 0, &ae);
    m[om_art_blob_pos(&a, ae.hero_sec) + 100] ^= 0x01;
    put(root, "aether/ART.PAK", m, pak_n);
    e.flags = 0;
    om_artjob_start(&j, &a, &e, NULL, (uint8_t *)stage32, sizeof stage32);
    run(&steps);
    CHECK_INT(j.hero_src, OM_HERO_PLACEHOLDER); CHECK_INT(j.err, OM_E_CRC);
    CHECK_STR(j.errmsg, "ART: ART.PAK CRC");
    /* 8. no ART.PAK at all (art == NULL) -> placeholder, no error */
    om_artjob_start(&j, NULL, &e, NULL, (uint8_t *)stage32, sizeof stage32);
    run(&steps);
    CHECK_INT(j.hero_src, OM_HERO_PLACEHOLDER); CHECK_INT(j.err, 0);
    /* 9. coordinator fix round 1, point 2: a stage-buffer clobber mid-load (e.g. a failed
     * LAUNCHER.BIN body read landing on OM_LW_STAGE, which lies inside the launcher body window
     * 0x00200000-0x0027F000 -- omlayout.h) must not surface as a false CRC once the job is
     * cancelled and re-requested. First demonstrate the bug: letting the clobbered job run to
     * completion anyway reports a false CRC error, even though the ART.PAK file on disk (restored
     * clean below, undoing case 7's corruption) and the ART.PAK key table in memory are both fine. */
    put(root, "aether/ART.PAK", pak, pak_n);
    memset(&e, 0, sizeof e);
    strcpy(e.key, "GS-9001"); e.disc = 0; e.flags = 0;
    om_artjob_start(&j, &a, &e, NULL, (uint8_t *)stage32, sizeof stage32);
    CHECK(om_artjob_step(&j, &out, 1) == 0);                       /* one partial step: not finished */
    memset(stage32, 0xFF, sizeof stage32);                         /* simulate the clobber */
    run(&steps);                                                   /* BUG: letting it finish anyway... */
    CHECK_INT(j.err, OM_E_CRC);                                    /* ...reports a false CRC */
    CHECK_STR(j.errmsg, "ART: ART.PAK CRC");
    printf("test_artjob: an uncancelled clobbered job reports a false CRC error (as expected)\n");
    /* FIX: cancel the clobbered job instead of letting it finish, then re-request -- a fresh job
     * reads clean bytes off disk and reports no error (this is what om_art_invalidate() in
     * menu/ui/art.c now does on a failed launch; that function itself is Saturn-only and not
     * host-testable, but the artjob-level mechanism it relies on is exercised here). */
    om_artjob_cancel(&j);
    om_artjob_start(&j, &a, &e, NULL, (uint8_t *)stage32, sizeof stage32);
    run(&steps);
    CHECK_INT(j.err, 0); CHECK_INT(j.hero_src, OM_HERO_PAK);
    /* 10. coordinator fix round 1, point 3: a folder path long enough that "/" + dirname +
     * "/BOX.TGA" would overflow om_artjob's box[] must fail named, never overflow. */
    for (k = 0; k < sizeof longpath - 7; k++) longpath[k] = 'a';
    strcpy(longpath + sizeof longpath - 7, "/x.cue");
    memset(&e, 0, sizeof e);
    strcpy(e.key, "NOPE-0001"); e.disc = 0; e.path = longpath; e.flags = OM_LIB_BOX;   /* no ART.PAK cover */
    om_artjob_start(&j, &a, &e, NULL, (uint8_t *)stage32, sizeof stage32);
    run(&steps);
    CHECK_INT(j.err, OM_E_FULL);
    CHECK_STR(j.errmsg, "ART: BOX.TGA path too long");
    CHECK_INT(j.hero_src, OM_HERO_PLACEHOLDER);
    free(m); free(pak);
    return th_done("test_artjob");
}
