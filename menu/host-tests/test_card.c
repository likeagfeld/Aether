#include <sys/stat.h>
#include <unistd.h>
#include "th.h"
#include "card.h"
#include "satiator.h"
#include "sd.h"
#include "satfs.h"

extern int fake_reads, fake_max_read, fake_force_timeout;
static uint8_t gdb[300000], art[140000], lib[220000], vrt[16384];
static om_scan_work work;
static int lines;

static void st(void *ctx, const char *line) { (void)ctx; (void)line; lines++; }

static void put(const char *root, const char *rel, const uint8_t *data, uint32_t n)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    f = fopen(p, "wb");
    fwrite(data, 1, n, f);
    fclose(f);
}

static void sysid_iso(const char *root, const char *rel, const char *product)
{
    uint8_t b[2048];
    memset(b, ' ', 0xD0); memset(b + 0xD0, 0, sizeof b - 0xD0);
    memcpy(b, "SEGA SEGASATURN ", 16);
    memcpy(b + 0x20, product, strlen(product));
    memcpy(b + 0x38, "CD-1/1", 6);
    memcpy(b + 0x40, "U", 1);
    memcpy(b + 0x60, "HOMEBREW", 8);
    put(root, rel, b, sizeof b);
}

int main(void)
{
    char root[] = "/tmp/omsat_card_XXXXXX", p[600], sum[3][41];
    uint32_t n;
    uint8_t *f;
    om_card c;
    om_card_bufs b = { gdb, sizeof gdb, art, sizeof art, lib, sizeof lib, &work, vrt, sizeof vrt };
    /* pins the constant main.c's "SATIATOR: NO RESPONSE (card load)" path depends on. Nothing
     * else in this binary exercises exec_cmd (fake_satiator.c replaces the s_* layer directly, below
     * where SAT_ETIMEDOUT/om_sd_err ever appear), so this is the only check in the suite that would
     * catch om_sd_err's -100+fr mapping or sd.h's OM_SD_TIMEOUT literal drifting out of sync with each
     * other. */
    CHECK_INT(om_sd_err(SAT_ETIMEDOUT), OM_SD_TIMEOUT);
    om_sysid s;
    CHECK(mkdtemp(root) != NULL);
    setenv("FAKE_SD_ROOT", root, 1);
    snprintf(p, sizeof p, "%s/aether", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/Racing", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/RPG", root); mkdir(p, 0755);
    sysid_iso(root, "unknown.iso", "T-8113H");
    f = th_load(FIX("tiny.games.db"), &n); put(root, "aether/GAMES.DB", f, n); free(f);
    f = th_load(FIX("tiny.art.pak"), &n); put(root, "aether/ART.PAK", f, n); free(f);
    f = th_load(FIX("tiny.library.idx"), &n); put(root, "aether/LIBRARY.IDX", f, n); free(f);
    /* 1. root = {Racing, RPG, unknown.iso, aether}: fingerprint = the fixture's -> LIBRARY.IDX is used */
    CHECK_INT(om_card_load(&c, &b, st, NULL), OM_OK);
    CHECK_INT(c.db_rc, OM_OK); CHECK_INT(c.art_rc, OM_OK); CHECK_INT(c.idx_rc, OM_OK);
    CHECK_INT(c.lib_source, OM_LIB_FROM_IDX);
    CHECK_INT(c.lib.count, 4);
    CHECK_INT(c.card_fp, 0xa4c2dc0eu);
    CHECK_INT(om_card_old_folder, 0);
    /* the Aether rename: a card that still has the pre-rename /vibemenu as well (both folders) -- the fingerprint does
     * not change (both names hidden), and the menu learns it must not swap saves (satfs.h om_card_old_folder) */
    snprintf(p, sizeof p, "%s/VibeMenu", root); mkdir(p, 0755);
    CHECK_INT(om_card_load(&c, &b, st, NULL), OM_OK);
    CHECK_INT(c.lib_source, OM_LIB_FROM_IDX);
    CHECK_INT(c.card_fp, 0xa4c2dc0eu);
    CHECK_INT(om_card_old_folder, 1);
    rmdir(p);
    CHECK_INT(om_card_load(&c, &b, st, NULL), OM_OK);
    CHECK_INT(om_card_old_folder, 0);
    put(root, "vibemenu", (const uint8_t *)"x", 1);                  /* a FILE of that name is not the old folder */
    CHECK_INT(om_card_load(&c, &b, st, NULL), OM_OK);
    CHECK_INT(om_card_old_folder, 0);
    snprintf(p, sizeof p, "%s/vibemenu", root); unlink(p);
    CHECK(fake_max_read <= 2048 && fake_reads > 0);
    printf("test_card: load 1 used %d s_read calls (max %d B each)\n", fake_reads, fake_max_read);
    om_card_summary(&c, sum);
    CHECK_STR(sum[2], "LIBRARY.IDX: 4 games, 3 folders");
    /* no VRTILES.BIN on this card -> the tiles are unavailable (named rc), the card still loads */
    CHECK_INT(c.vrt_rc, OM_SD_NOFILE);
    CHECK(c.vrt == NULL && c.vrt_len == 0);
    f = th_load("../app/assets/VRTILES.BIN", &n); put(root, "aether/VRTILES.BIN", f, n);
    CHECK_INT(om_card_load(&c, &b, st, NULL), OM_OK);
    CHECK_INT(c.vrt_rc, OM_OK);
    CHECK(c.vrt == vrt + 64 && c.vrt_len == n - 64);
    CHECK_INT(c.lib_source, OM_LIB_FROM_IDX);
    /* a launch's LAUNCHER.BIN body overwrites LWRAM (launch.c clobbered): om_card_reload_lw reads the tiles again */
    memset(vrt, 0xA5, sizeof vrt);
    c.vrt = NULL; c.vrt_len = 0; c.vrt_rc = OM_E_SHORT;
    CHECK_INT(om_card_reload_lw(&c, &b), OM_OK);
    CHECK_INT(c.vrt_rc, OM_OK);
    CHECK(c.vrt == vrt + 64 && memcmp(vrt, f, n) == 0);
    /* a corrupt file is refused by its CRC, and nothing points at it */
    f[64 + 7] ^= 0x40; put(root, "aether/VRTILES.BIN", f, n); f[64 + 7] ^= 0x40;
    CHECK_INT(om_card_reload_lw(&c, &b), OM_OK);
    CHECK_INT(c.vrt_rc, OM_E_CRC);
    CHECK(c.vrt == NULL && c.vrt_len == 0);
    /* a file too big for its LWRAM region: buffer full, as SPEC §5.9 says */
    {
        om_card_bufs small = b;
        small.vrt_cap = n;                                             /* sd.c needs size + 4 <= cap */
        CHECK_INT(om_card_reload_lw(&c, &small), OM_OK);
        CHECK_INT(c.vrt_rc, OM_E_FULL);
    }
    put(root, "aether/VRTILES.BIN", f, n); free(f);
    /* 2. a new root folder makes the index stale -> on-console scan */
    snprintf(p, sizeof p, "%s/New", root); mkdir(p, 0755);
    CHECK_INT(om_card_load(&c, &b, st, NULL), OM_OK);
    CHECK_INT(c.lib_source, OM_LIB_SCAN_STALE);
    CHECK_INT(c.scan.entries, 1); CHECK_INT(c.scan.matched, 1);    /* unknown.iso -> T-8113H, area U */
    CHECK_INT(c.lib.nfolders, 4);                                   /* "", New, RPG, Racing */
    om_card_summary(&c, sum);
    CHECK_STR(sum[2], "SCAN (index stale): 1 games, 4 folders");
    /* 3. missing index -> scan; corrupt GAMES.DB -> no metadata but still lists (spec §5.6) */
    snprintf(p, sizeof p, "%s/aether/LIBRARY.IDX", root); remove(p);
    put(root, "aether/GAMES.DB", (const uint8_t *)"OMSATGDBgarbage", 15);
    CHECK_INT(om_card_load(&c, &b, st, NULL), OM_OK);
    CHECK_INT(c.lib_source, OM_LIB_SCAN_MISSING);
    CHECK_INT(c.idx_rc, -104);                                     /* missing LIBRARY.IDX = SD no file */
    CHECK(c.db_rc != OM_OK);
    CHECK_INT(c.scan.entries, 1); CHECK_INT(c.scan.matched, 0);
    om_card_summary(&c, sum);
    CHECK(strstr(sum[0], "GAMES.DB: ") == sum[0] && strstr(sum[0], " rc -") != NULL);   /* named error + code */
    /* 4. sysid through a cue: FILE line -> track file */
    {
        const char *cue = "FILE \"sub/track.bin\" BINARY\r\n  TRACK 01 MODE1/2048\r\n";
        put(root, "RPG/g.cue", (const uint8_t *)cue, (uint32_t)strlen(cue));
    }
    sysid_iso(root, "RPG/track.bin", "GS-9001");
    CHECK_INT(om_satfs_sysid("RPG/g.cue", &s), OM_OK);
    CHECK_STR(s.product, "GS-9001");
    CHECK(om_satfs_sysid("RPG/missing.cue", &s) < 0);
    CHECK(lines > 0);
    /* 5. The root listing's readdir times out mid-scan. menu/app/satfs.c's sat_list
     * must report OM_SD_TIMEOUT (via om_sd_err(SAT_ETIMEDOUT)), not silently treat the timeout as "end of
     * directory" -- fake_force_timeout (fake_satiator.c) makes the NEXT s_stat(NULL,...) call return
     * SAT_ETIMEDOUT directly, one-shot, so this drives that return value through the real satfs.c code
     * this binary links (menu/host-tests/test_sattimeout.c proves the same branch against a real HIRQ
     * timeout instead, for s_read/s_stat in isolation -- this is the satfs.c-level half). */
    fake_force_timeout = 1;
    {
        uint32_t fp = 0;
        CHECK_INT(om_satfs_root_fp(&work, &fp), OM_SD_TIMEOUT);
    }
    CHECK(!fake_force_timeout);   /* consumed exactly once, not left armed for a later call */
    return th_done("test_card");
}
