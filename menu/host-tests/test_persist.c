/* menu/host-tests/test_persist.c -- SETTINGS/FAVS/RECENT load + save through the real menu/app/sd.c over
 * fake_satiator.c (which enforces libsatiator's 2 KB per call and 4-byte alignment). BSD-3-Clause. */
#include <stdlib.h>
#include <sys/stat.h>
#include "th.h"
#include "persist.h"
#include "sd.h"
#include "satiator.h"

static uint32_t stage32[65536 / 4];
static char setout[OM_SETTINGS_MAX];     /* the merge output the caller lends */
static uint32_t setin32[OM_SETTINGS_IN / 4];   /* and the file read again at each save */
#define setin ((uint8_t *)setin32)

static void put(const char *root, const char *rel, const char *s)
{
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    f = fopen(p, "wb");
    fwrite(s, 1, strlen(s), f);
    fclose(f);
}

static long slurp(const char *root, const char *rel, char *out, long cap)
{
    char p[512];
    FILE *f;
    long n;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    if (!(f = fopen(p, "rb"))) return -1;
    n = (long)fread(out, 1, (size_t)cap - 1, f);
    fclose(f);
    out[n] = 0;
    return n;
}

int main(void)
{
    char root[] = "/tmp/omsat_persist_XXXXXX", p[600], got[8192];
    static char big[5000];
    uint32_t n;
    uint8_t *f = th_load(FIX("tiny.library.idx"), &n);
    om_libidx l;
    om_persist ps;
    uint16_t order[8], tmp[8];
    CHECK(mkdtemp(root) != NULL);
    setenv("FAKE_SD_ROOT", root, 1);
    snprintf(p, sizeof p, "%s/aether", root);
    mkdir(p, 0755);
    CHECK_INT(om_libidx_open(&l, f, n), OM_OK);
    om_persist_load(&ps, &l, order, tmp, (uint8_t *)stage32, sizeof stage32);   /* nothing on the card yet */
    CHECK_INT(ps.set_rc, OM_OK); CHECK_INT(ps.favs_rc, OM_OK); CHECK_INT(ps.recent_rc, OM_OK);
    CHECK_STR(ps.set.theme, "saturn"); CHECK_INT(ps.favs.n, 0);
    put(root, "aether/SETTINGS.INI", "foo=1\ntheme=openmenu\n[GS-9001]\nregion=U\n");
    put(root, "aether/FAVS.TXT", "unknown.iso\nRPG/Grandia/disc2.cue\n");
    put(root, "aether/RECENT.TXT", "RPG/Grandia/disc1.cue\nlost.iso\n");
    om_persist_load(&ps, &l, order, tmp, (uint8_t *)stage32, sizeof stage32);
    CHECK_STR(ps.set.theme, "openmenu");      /* Task 13r addendum: an old/unknown theme name round-trips verbatim
                                                * (the preserve-and-message rule) -- persist.c does not validate it */
    CHECK_INT(ps.favs.n, 2); CHECK_INT(ps.favs.idx[0], 3); CHECK_INT(ps.favs.idx[1], 2);
    CHECK_INT(ps.recent.n, 1); CHECK_INT(ps.recent.dropped, 1); CHECK_INT(ps.recent.cap, OM_RECENT_MAX);
    ps.set.sfx = 0;
    CHECK_INT(om_persist_save_settings(&ps, setin, setout), OM_OK);
    slurp(root, "aether/SETTINGS.INI", got, sizeof got);
    CHECK(strstr(got, "sfx=off\n") != NULL);
    CHECK(strstr(got, "foo=1\n[GS-9001]\nregion=U\n") != NULL);   /* unknown key + section survive */
    CHECK(strstr(got, "theme=openmenu\n") != NULL);
    CHECK_INT(om_persist_save_settings(&ps, setin, setout), OM_OK);             /* second save merges against the first */
    CHECK_INT(slurp(root, "aether/SETTINGS.INI", got, sizeof got) > 0, 1);
    CHECK(strstr(strstr(got, "foo=1") + 1, "foo=1") == NULL);    /* no duplication across saves */
    om_plist_push_front(&ps.recent, 0);
    CHECK_INT(om_persist_save_list(&ps.recent, &l, OM_PATH_RECENT), OM_OK);
    slurp(root, "aether/RECENT.TXT", got, sizeof got);
    CHECK_STR(got, "Racing/Daytona USA/daytona.cue\nRPG/Grandia/disc1.cue\n");
    ps.favs.n = 0;
    CHECK_INT(om_persist_save_list(&ps.favs, &l, OM_PATH_FAVS), OM_OK);
    CHECK_INT(slurp(root, "aether/FAVS.TXT", got, sizeof got), 0);   /* emptied list truncates the file */
    memset(big, 'z', sizeof big);
    CHECK_INT(om_sd_write_buf("/aether/BIG.TXT", big, sizeof big), OM_OK);   /* 3 calls: 2044+2044+912 (sdwrite.h) */
    CHECK_INT(slurp(root, "aether/BIG.TXT", got, sizeof got), 5000);
    CHECK(got[4999] == 'z');
    CHECK(om_sd_write_buf("/nodir/X.TXT", big, 10) < 0);         /* failure is a code, not a crash */
    {   /* review-final-m3 I2/I4: one s_sync per <= 2 KB chunk; a failed sync or close is the named error, not OK.
         * a failed chunk is retried once (sdwrite.h), so a sync fault is injected twice (fake_fail_op 'S',
         * two times) to keep the same asserted codes; a close is not retried */
        extern int fake_fail_sync, fake_fail_close, fake_syncs;
        extern int fake_fail_op, fake_fail_times, fake_fail_rc;
        fake_syncs = 0;
        CHECK_INT(om_sd_write_buf("/aether/BIG.TXT", big, sizeof big), OM_OK);
        CHECK_INT(fake_syncs, 3);                                  /* 2044 + 2044 + 912 */
        fake_fail_close = -FR_DISK_ERR;
        CHECK_INT(om_sd_write_buf("/aether/BIG.TXT", big, 100), om_sd_err(-FR_DISK_ERR));
        CHECK_INT(fake_fail_close, 0);
        fake_fail_op = 'S'; fake_fail_times = 2; fake_fail_rc = -FR_DISK_ERR;
        CHECK_INT(om_sd_write_buf("/aether/BIG.TXT", big, sizeof big), om_sd_err(-FR_DISK_ERR));
        fake_fail_op = 'S'; fake_fail_times = 2; fake_fail_rc = -FR_INT_ERR;
        fake_fail_close = -FR_DISK_ERR;                             /* the first failure wins */
        CHECK_INT(om_persist_save_settings(&ps, setin, setout), om_sd_err(-FR_INT_ERR));
        CHECK(strcmp(om_strerror(om_sd_err(-FR_DISK_ERR)), "") != 0);
        fake_fail_sync = fake_fail_close = fake_fail_op = fake_fail_times = 0;
    }
    {   /* each save reads the card's file again: a key written there since the load survives the next save */
        put(root, "aether/SETTINGS.INI", "bar=2\ntheme=openmenu\n");
        CHECK_INT(om_persist_save_settings(&ps, setin, setout), OM_OK);
        slurp(root, "aether/SETTINGS.INI", got, sizeof got);
        CHECK(strstr(got, "bar=2\n") != NULL);
        CHECK(strstr(got, "sfx=off\n") != NULL);
    }
    {   /* a malformed file at load: defaults, and the next save writes a clean file, not merged against it */
        static char bad[400];
        memset(bad, 'k', 300);
        strcpy(bad + 300, "=1\nfoo=1\n");
        put(root, "aether/SETTINGS.INI", bad);
        om_persist_load(&ps, &l, order, tmp, (uint8_t *)stage32, sizeof stage32);
        CHECK(ps.set_rc != OM_OK);
        CHECK_STR(ps.set.theme, "saturn");
        CHECK_INT(om_persist_save_settings(&ps, setin, setout), OM_OK);
        slurp(root, "aether/SETTINGS.INI", got, sizeof got);
        CHECK(strstr(got, "foo=1") == NULL);
        CHECK(strstr(got, "theme=saturn\n") != NULL);
        put(root, "aether/SETTINGS.INI", "foo=3\ntheme=saturn\n");   /* once saved, saves merge again */
        CHECK_INT(om_persist_save_settings(&ps, setin, setout), OM_OK);
        slurp(root, "aether/SETTINGS.INI", got, sizeof got);
        CHECK(strstr(got, "foo=3\n") != NULL);
    }
    free(f);
    return th_done("test_persist");
}
