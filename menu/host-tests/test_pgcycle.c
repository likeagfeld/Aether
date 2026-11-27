/* menu/host-tests/test_pgcycle.c -- hardware report on 3c64908 (the user's SAROO card, a 703-name root): A on a game,
 * then B, many times in a row, eventually froze VibeMenu. This walks the SD side of 200 launch-panel open/close cycles
 * through the real art job (artjob.c, ART.PAK kept open across jobs since f66770f), the real IP.BIN read the panel
 * makes on open (info.c om_info_load -> satfs.c om_satfs_sysid), the PERGAME.INI read (sd.c) and the LOADBG.BIN
 * read-ahead's open / read / pause, in folders.c's order, with the music's stream file held open throughout, over the
 * fake libsatiator on a card with the user's 703-name root. Gates, every cycle:
 *   - at most 2 files open at once (the music + one), and none left open but the music after the close;
 *   - no growth: the IP.BIN read happens once (info.c caches it), path commands per cycle stay constant;
 *   - no stuck state: every art job ends, every read succeeds.
 * menu/views/loading.c (SGL) is not host-built: its read-ahead's file use is mirrored (preload_step: one open, 2 KB
 * reads, om_loading_preload_pause closes). BSD-3-Clause. */
#include <sys/stat.h>
#include "th.h"
#include "artjob.h"
#include "info.h"
#include "satiator.h"
#include "sd.h"

extern int fake_paths, fake_open_now, fake_open_max;
static uint32_t stage32[(130 * 1024) / 4];
static uint32_t buf32[(17 * 1024) / 4];
static om_art_out out;
static om_artjob j;

static void put(const char *root, const char *rel, const void *d, uint32_t n)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    f = fopen(p, "wb");
    fwrite(d, 1, n, f);
    fclose(f);
}

static int art_job(const om_artpak *a, const om_lib_entry *e)
{   /* the cover job after the panel closes (art.c om_art_tick): to its end */
    int n, steps = 0;
    om_artjob_start(&j, a, e, NULL, (uint8_t *)stage32, sizeof stage32);
    while (!(n = om_artjob_step(&j, &out, OM_AJ_READS_PER_STEP)) && steps < 1000) steps++;
    return n && steps < 1000;
}

int main(void)
{
    char root[] = "/tmp/omsat_pgc_XXXXXX", p[600];
    uint8_t ip[2048], *pak, lbg[8192];
    uint32_t pak_n, len, i;
    om_artpak a;
    om_lib_entry e;
    int music, fd, c, ok_art = 0, ok_ip = 0, ok_ini = 0, ok_lbg = 0, max_open = 0, left = 0, paths0 = -1, grow = 0;
    CHECK(mkdtemp(root) != NULL);
    setenv("FAKE_SD_ROOT", root, 1);
    snprintf(p, sizeof p, "%s/aether", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/aether/MUSIC", root); mkdir(p, 0755);
    for (i = 0; i < 700; i++) { snprintf(p, sizeof p, "%s/Game %03u", root, (unsigned)i); mkdir(p, 0755); }
    snprintf(p, sizeof p, "%s/Racing", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/Racing/Daytona USA", root); mkdir(p, 0755);
    pak = th_load(FIX("tiny.art.pak"), &pak_n);
    put(root, "aether/ART.PAK", pak, pak_n);
    memset(lbg, 0x5A, sizeof lbg);
    put(root, "aether/LOADBG.BIN", lbg, sizeof lbg);
    put(root, "aether/MUSIC/BGM.MUS", lbg, sizeof lbg);
    put(root, "aether/PERGAME.INI", "[GS-9001@JTUE]\nloader=cwx\n", 26);
    memset(ip, ' ', sizeof ip);                                    /* a 2,048 B sector: ST-040-R4 section 4.2 */
    memcpy(ip, "SEGA SEGASATURN SEGA ENTERPRISESGS-9001   V1.00019950401CD-1/1  JTUE      ", 74);
    put(root, "Racing/Daytona USA/daytona.bin", ip, sizeof ip);
    {
        static const char cue[] = "FILE \"daytona.bin\" BINARY\n  TRACK 01 MODE1/2048\n";
        put(root, "Racing/Daytona USA/daytona.cue", cue, (uint32_t)strlen(cue));
    }
    CHECK_INT(om_artpak_open(&a, pak, pak_n, pak_n), OM_OK);
    memset(&e, 0, sizeof e);
    strcpy(e.key, "GS-9001"); e.disc = 1; e.path = "Racing/Daytona USA/daytona.cue"; e.title = "";
    om_info_reset();
    music = om_sd_open("/aether/MUSIC/BGM.MUS");                  /* audio.c: the stream stays open */
    CHECK(music >= 0);
    CHECK(art_job(&a, &e));                                        /* the cover is up before the first A */
    for (c = 0; c < 200; c++) {
        int p0 = fake_paths;
        fake_open_max = fake_open_now;
        /* A: pg_open_selected */
        om_artjob_cancel(&j);                                      /* om_art_invalidate: the job's file, then */
        om_artjob_release();                                       /* ART.PAK */
        ok_ip += om_info_load(&e, 0) == 0 && om_info_find(0) != NULL;   /* game_gid */
        ok_ini += om_sd_read_all("/aether/PERGAME.INI", buf32, sizeof buf32, &len) == 0;   /* pg_read */
        /* the read-ahead (loading.c preload_step / om_loading_preload_tick_vb): one open, 2 KB steps */
        if ((fd = om_sd_open("/aether/LOADBG.BIN")) >= 0) {
            ok_lbg += om_sd_read_at(fd, 0, buf32, 2048) == 0 && om_sd_read_at(fd, 2048, buf32, 2048) == 0;
            /* B: pg_after_key -> om_loading_preload_pause */
            s_close(fd);
        }
        ok_art += art_job(&a, &e);                                 /* the list again: the cover job reopens ART.PAK */
        if (fake_open_max > max_open) max_open = fake_open_max;
        om_artjob_release();
        if (fake_open_now != 1) left++;                            /* the music only */
        CHECK(art_job(&a, &e));                                    /* ART.PAK back for the next cycle */
        if (c == 1) paths0 = fake_paths - p0;
        else if (c > 1 && fake_paths - p0 != paths0) grow++;
    }
    printf("test_pgcycle: 200 cycles: max open files %d, left open after a close %d, IP reads ok %d, PERGAME %d, "
           "LOADBG %d, art jobs %d, path commands a cycle %d (changed %d times)\n",
           max_open, left, ok_ip, ok_ini, ok_lbg, ok_art, paths0, grow);
    CHECK(max_open <= 2);
    CHECK_INT(left, 0);
    CHECK_INT(ok_ip, 200); CHECK_INT(ok_ini, 200); CHECK_INT(ok_lbg, 200); CHECK_INT(ok_art, 200);
    CHECK_INT(grow, 0);
    s_close(music);
    om_artjob_release();
    CHECK_INT(fake_open_now, 0);
    return th_done("test_pgcycle");
}
