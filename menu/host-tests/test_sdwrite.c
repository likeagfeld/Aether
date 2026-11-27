/* menu/host-tests/test_sdwrite.c -- menu/src/sdwrite.c's chunking, sync, retry, verify, the two-phase
 * replace and its startup recovery rule, over fake_satiator.c (fault injection at create / write / sync / close /
 * unlink / rename, a stored-byte corruption and a power cut). BSD-3-Clause. */
#include <stdlib.h>
#include <sys/stat.h>
#include "th.h"
#include "om_fmt.h"
#include "satiator.h"
#include "sdwrite.h"

extern char fake_log[8192];
extern int fake_open_now, fake_open_max;
extern int fake_fail_op, fake_fail_skip, fake_fail_times, fake_fail_rc;
extern int fake_cut_op, fake_cut_skip, fake_dead;
extern long fake_corrupt_at;
extern int fake_rename_overwrites;
void fake_reboot(void);

static char root[] = "/tmp/omsat_sdw_XXXXXX";
static uint32_t scratch32[2048 / 4];
static uint32_t rbuf32[16384 / 4];
#define SCR ((uint8_t *)scratch32)
#define RB ((uint8_t *)rbuf32)
static int gaps;
static void gap(void) { gaps++; }

static void reset(void)
{
    fake_log[0] = 0;
    fake_open_max = fake_open_now;
    fake_fail_op = fake_fail_skip = fake_fail_times = fake_fail_rc = 0;
    fake_cut_op = fake_cut_skip = 0;
    fake_corrupt_at = -1;
    gaps = 0;
}

static long disk(const char *rel, uint8_t *out, long cap)
{   /* the "card" file's bytes; -1 when absent */
    char p[512];
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
    char p[512];
    struct stat sb;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    return !stat(p, &sb);
}

static void fill(uint8_t *b, uint32_t n, uint32_t seed)
{
    uint32_t i;
    for (i = 0; i < n; i++) b[i] = (uint8_t)(i * 7u + seed);
}

/* a whole-line source like persist.c's FAVS/RECENT emitter: pieces of 9 B, never split (2,044 % 9 == 1, so a chunk
 * runs out of room mid-line and the 4-multiple flush + carry is exercised) */
typedef struct { uint32_t left; } lines;
static int32_t from_lines(void *ctx, uint8_t *dst, uint32_t cap)
{
    lines *l = (lines *)ctx;
    uint32_t o = 0;
    while (l->left && o + 9 <= cap) { memcpy(dst + o, "abcdefgh\n", 9); o += 9; l->left--; }
    return (int32_t)o;
}

static void seed(const char *rel, const uint8_t *d, size_t n)
{   /* a card file put in place directly, as an earlier (interrupted) run left it */
    char p[512];
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    f = fopen(p, "wb");
    fwrite(d, 1, n, f);
    fclose(f);
}

static void unseed(const char *rel) { char p[512]; snprintf(p, sizeof p, "%s/%s", root, rel); remove(p); }

static int all_mult4_but_last(const char *log)
{   /* every "W<n>" but the last has n % 4 == 0 */
    const char *p = log, *last = NULL;
    while ((p = strstr(p, "W")) != NULL) { if (last && atoi(last + 1) % 4) return 0; last = p; p++; }
    return last != NULL;
}

int main(void)
{
    static uint8_t data[5000], got[20000], d2[3000], d3[1500];
    om_sdw_stats st;
    uint32_t len;
    char p[600];
    CHECK(mkdtemp(root) != NULL);
    setenv("FAKE_SD_ROOT", root, 1);
    snprintf(p, sizeof p, "%s/aether", root);
    mkdir(p, 0755);
    fill(data, sizeof data, 1);

    /* 1. 5,000 B: 2,044 + 2,044 + 912, s_sync after each, a gap between chunks only, one file open */
    reset(); memset(&st, 0, sizeof st);
    CHECK_INT(om_sdw_write_file("/aether/A.BIN", data, sizeof data, 0, SCR, gap, &st), OM_OK);
    CHECK_STR(fake_log, "O W2044 S W2044 S W912 S C ");
    CHECK_INT(gaps, 2);
    CHECK_INT(fake_open_max, 1);
    CHECK_INT(st.chunks, 3); CHECK_INT(st.retries, 0); CHECK_INT((int32_t)st.last_rc, 0);
    CHECK_INT(disk("aether/A.BIN", got, sizeof got), 5000);
    CHECK(!memcmp(got, data, 5000));

    /* 2. a fault on chunk 2's write is retried once after a gap (seek back to its offset) and succeeds */
    reset(); memset(&st, 0, sizeof st);
    fake_fail_op = 'W'; fake_fail_skip = 1; fake_fail_times = 1; fake_fail_rc = -FR_DISK_ERR;
    CHECK_INT(om_sdw_write_file("/aether/A.BIN", data, sizeof data, 1, SCR, gap, &st), OM_OK);
    CHECK_STR(fake_log, "O W2044 S K2044 W2044 S W912 S C o C ");
    CHECK_INT(st.retries, 1); CHECK_INT(st.chunks, 3);
    CHECK_INT(gaps, 3);
    CHECK_INT(disk("aether/A.BIN", got, sizeof got), 5000);
    CHECK(!memcmp(got, data, 5000));

    /* 3. two faults on chunk 2: the call fails with that rc, st.retries == 1, the file is closed */
    reset(); memset(&st, 0, sizeof st);
    fake_fail_op = 'W'; fake_fail_skip = 1; fake_fail_times = 2; fake_fail_rc = -FR_DISK_ERR;
    CHECK_INT(om_sdw_write_file("/aether/A.BIN", data, sizeof data, 0, SCR, gap, &st), OM_SDW_ERR(-FR_DISK_ERR));
    CHECK_INT(st.retries, 1);
    CHECK_INT((int32_t)st.last_rc, OM_SDW_ERR(-FR_DISK_ERR));
    CHECK_INT(fake_open_now, 0);

    /* 4. a failed sync is a failed chunk too: retried once */
    reset(); memset(&st, 0, sizeof st);
    fake_fail_op = 'S'; fake_fail_skip = 0; fake_fail_times = 1; fake_fail_rc = -FR_INT_ERR;
    CHECK_INT(om_sdw_write_file("/aether/A.BIN", data, sizeof data, 0, SCR, gap, &st), OM_OK);
    CHECK_STR(fake_log, "O W2044 K0 W2044 S W2044 S W912 S C ");
    CHECK_INT(st.retries, 1);
    fake_fail_op = 'S'; fake_fail_skip = 0; fake_fail_times = 2; fake_fail_rc = -FR_INT_ERR;
    CHECK_INT(om_sdw_write_file("/aether/A.BIN", data, sizeof data, 0, SCR, gap, NULL), OM_SDW_ERR(-FR_INT_ERR));

    /* 5. create and close faults are the call's rc (no retry: nothing was written / the data is already out) */
    reset();
    fake_fail_op = 'O'; fake_fail_times = 1; fake_fail_rc = -FR_DENIED;
    CHECK_INT(om_sdw_write_file("/aether/A.BIN", data, 100, 0, SCR, gap, NULL), OM_SDW_ERR(-FR_DENIED));
    reset();
    fake_fail_op = 'C'; fake_fail_times = 1; fake_fail_rc = -FR_DISK_ERR;
    CHECK_INT(om_sdw_write_file("/aether/A.BIN", data, 100, 0, SCR, gap, NULL), OM_SDW_ERR(-FR_DISK_ERR));
    CHECK_INT(fake_open_now, 0);

    /* 6. verify catches a card that corrupts byte 3,000; without verify the same write reads as OK */
    reset(); memset(&st, 0, sizeof st);
    fake_corrupt_at = 3000;
    CHECK_INT(om_sdw_write_file("/aether/A.BIN", data, sizeof data, 1, SCR, gap, &st), OM_E_CRC);
    CHECK_INT(st.verify_fail, 1);
    CHECK_INT(fake_open_now, 0);
    CHECK_INT(om_sdw_write_file("/aether/A.BIN", data, sizeof data, 0, SCR, gap, NULL), OM_OK);
    fake_corrupt_at = -1;
    CHECK_INT(om_sdw_write_file("/aether/A.BIN", data, sizeof data, 1, SCR, gap, NULL), OM_OK);

    /* 7. a whole-line source is repacked: every chunk but the last a multiple of 4, the bytes unchanged */
    {
        lines l = { 1000 };                                     /* 9,000 B of 9 B lines */
        uint32_t i;
        reset();
        CHECK_INT(om_sdw_write_src("/aether/L.TXT", from_lines, &l, 1, SCR, gap, NULL), OM_OK);
        CHECK(all_mult4_but_last(fake_log));
        CHECK(strstr(fake_log, "W2040 ") != NULL);            /* out of room at 2,043 B: 2,040 out, 3 B carried */
        CHECK_INT(disk("aether/L.TXT", got, sizeof got), 9000);
        for (i = 0; i < 9000; i += 9) if (memcmp(got + i, "abcdefgh\n", 9)) break;
        CHECK_INT(i, 9000);
        l.left = 0;                                              /* an empty list truncates the file */
        CHECK_INT(om_sdw_write_src("/aether/L.TXT", from_lines, &l, 1, SCR, gap, NULL), OM_OK);
        CHECK_INT(disk("aether/L.TXT", got, sizeof got), 0);
    }

    /* 8. om_sdw_replace, first save (no file yet): path written, no .NEW left, unlink before rename */
    fill(d2, sizeof d2, 2);
    fill(d3, sizeof d3, 3);
    reset(); memset(&st, 0, sizeof st);
    CHECK_INT(om_sdw_replace("/aether/P.INI", data, 1000, SCR, gap, &st), OM_OK);
    CHECK_INT(disk("aether/P.INI", got, sizeof got), 1000);
    CHECK(!memcmp(got, data, 1000));
    CHECK(!exists("aether/P.INI.NEW"));
    CHECK(strstr(fake_log, "U N ") != NULL);                   /* the old file goes first, then the rename */
    CHECK_INT(fake_open_max, 1);

    /* 9. the .NEW verify fails: the old file intact, .NEW removed */
    reset(); memset(&st, 0, sizeof st);
    fake_corrupt_at = 10;
    CHECK_INT(om_sdw_replace("/aether/P.INI", d2, sizeof d2, SCR, gap, &st), OM_E_CRC);
    fake_corrupt_at = -1;
    CHECK_INT(disk("aether/P.INI", got, sizeof got), 1000);
    CHECK(!memcmp(got, data, 1000));
    CHECK(!exists("aether/P.INI.NEW"));
    CHECK(strstr(fake_log, "N") == NULL);                      /* no rename on a failed verify */

    /* 10. a create fault on .NEW, and an unlink fault on the old file: the old file stays the one read */
    reset();
    /* path exists, so step 1 opens nothing (2026-11-24): the first open for write is .NEW's create */
    fake_fail_op = 'O'; fake_fail_skip = 0; fake_fail_times = 1; fake_fail_rc = -FR_DENIED;
    CHECK(om_sdw_replace("/aether/P.INI", d2, sizeof d2, SCR, gap, NULL) < 0);
    CHECK_INT(om_sdw_read_file("/aether/P.INI", RB, sizeof rbuf32, &len), OM_OK);
    CHECK_INT(len, 1000); CHECK(!memcmp(RB, data, 1000));
    reset();
    fake_fail_op = 'U'; fake_fail_times = 1; fake_fail_rc = -FR_DISK_ERR;
    CHECK_INT(om_sdw_replace("/aether/P.INI", d2, sizeof d2, SCR, gap, NULL), OM_SDW_ERR(-FR_DISK_ERR));
    CHECK(exists("aether/P.INI.NEW"));                       /* a complete .NEW next to path: path still wins */
    CHECK_INT(om_sdw_read_file("/aether/P.INI", RB, sizeof rbuf32, &len), OM_OK);
    CHECK_INT(len, 1000); CHECK(!memcmp(RB, data, 1000));

    /* 11. the rename (step 4) fails after the unlink: path absent, the verified .NEW is read; the next replace
     * finishes the interrupted one first (step 0), then writes */
    reset();
    fake_fail_op = 'N'; fake_fail_times = 1; fake_fail_rc = -FR_DISK_ERR;   /* path exists: step 0 renames nothing */
    CHECK_INT(om_sdw_replace("/aether/P.INI", d2, sizeof d2, SCR, gap, NULL), OM_SDW_ERR(-FR_DISK_ERR));
    CHECK(!exists("aether/P.INI"));
    CHECK_INT(om_sdw_read_file("/aether/P.INI", RB, sizeof rbuf32, &len), OM_OK);
    CHECK_INT(len, sizeof d2); CHECK(!memcmp(RB, d2, sizeof d2));

    /* 12. power cut mid-write of the NEXT replace's .NEW (the case the brief's rule alone loses): step 0 puts the
     * recovered file back as path first, so after the reboot d2 is what reads, never an empty path or a partial .NEW.
     * The state is set up directly (path absent, a complete .NEW), independent of case 11. */
    {
        FILE *f;
        snprintf(p, sizeof p, "%s/aether/P.INI", root);
        remove(p);
        snprintf(p, sizeof p, "%s/aether/P.INI.NEW", root);
        f = fopen(p, "wb");
        fwrite(d2, 1, sizeof d2, f);
        fclose(f);
    }
    reset();
    fake_cut_op = 'W';                                         /* the first chunk of .NEW, then the power goes */
    CHECK(om_sdw_replace("/aether/P.INI", data, sizeof data, SCR, gap, NULL) < 0);
    fake_reboot();
    CHECK(exists("aether/P.INI"));
    CHECK_INT(om_sdw_read_file("/aether/P.INI", RB, sizeof rbuf32, &len), OM_OK);
    CHECK_INT(len, sizeof d2); CHECK(!memcmp(RB, d2, sizeof d2));

    /* 13. the brief's case: power cut right after the unlink -> .NEW left behind, read instead */
    reset();
    fake_cut_op = 'U';
    CHECK(om_sdw_replace("/aether/P.INI", d3, sizeof d3, SCR, gap, NULL) < 0);
    fake_reboot();
    CHECK(!exists("aether/P.INI"));
    CHECK(exists("aether/P.INI.NEW"));
    CHECK_INT(om_sdw_read_file("/aether/P.INI", RB, sizeof rbuf32, &len), OM_OK);
    CHECK_INT(len, sizeof d3); CHECK(!memcmp(RB, d3, sizeof d3));
    reset();                                                   /* the next save completes normally */
    CHECK_INT(om_sdw_replace("/aether/P.INI", data, 2000, SCR, gap, NULL), OM_OK);
    CHECK_INT(disk("aether/P.INI", got, sizeof got), 2000);
    CHECK(!memcmp(got, data, 2000));
    CHECK(!exists("aether/P.INI.NEW"));

    /* 14. first-ever save cut mid-.NEW: path exists (empty, step 1), so the partial .NEW is never read */
    reset();
    fake_cut_op = 'W';
    CHECK(om_sdw_replace("/aether/Q.INI", data, sizeof data, SCR, gap, NULL) < 0);
    fake_reboot();
    CHECK_INT(om_sdw_read_file("/aether/Q.INI", RB, sizeof rbuf32, &len), OM_OK);
    CHECK_INT(len, 0);

    /* 15. om_sdw_read_file: absent, too big for cap, unaligned buffer */
    CHECK_INT(om_sdw_read_file("/aether/NONE.INI", RB, sizeof rbuf32, &len), OM_SDW_NOFILE);
    CHECK_INT(om_sdw_read_file("/aether/A.BIN", RB, 4000, &len), OM_E_FULL);
    CHECK_INT(om_sdw_read_file("/aether/A.BIN", RB + 1, 8000, &len), OM_E_FIELD);
    CHECK_INT(om_sdw_read_file("/aether/A.BIN", RB, 5004, &len), OM_OK);
    CHECK_INT(len, 5000); CHECK(!memcmp(RB, data, 5000));
    CHECK_INT(fake_open_now, 0);

    /* 16-18, under both rename semantics (refuse, as FatFs; overwrite) */
    {
        int mode;
        for (mode = 0; mode < 2; mode++) {
            fake_rename_overwrites = mode;
            /* 16 (I1): a good path next to a torn .NEW (a cut mid-step 2 earlier); the next replace is cut at its
             * first chunk: path must still be the good file, whatever rename-onto-existing does */
            seed("aether/R.INI", d2, sizeof d2);
            seed("aether/R.INI.NEW", data, 100);
            reset();
            fake_cut_op = 'W';
            CHECK(om_sdw_replace("/aether/R.INI", data, sizeof data, SCR, gap, NULL) < 0);
            fake_reboot();
            CHECK_INT(om_sdw_read_file("/aether/R.INI", RB, sizeof rbuf32, &len), OM_OK);
            CHECK_INT(len, sizeof d2); CHECK(!memcmp(RB, d2, sizeof d2));
            /* 17 (I3): path absent, the verified .NEW is the only copy; step 0's rename fails: the call stops there,
             * so a cut later in the same call cannot reach the .NEW */
            unseed("aether/R.INI");
            seed("aether/R.INI.NEW", d3, sizeof d3);
            reset();
            fake_fail_op = 'N'; fake_fail_times = 1; fake_fail_rc = -FR_DISK_ERR;
            fake_cut_op = 'W';
            CHECK_INT(om_sdw_replace("/aether/R.INI", data, sizeof data, SCR, gap, NULL), OM_SDW_ERR(-FR_DISK_ERR));
            fake_reboot();
            CHECK_INT(om_sdw_read_file("/aether/R.INI", RB, sizeof rbuf32, &len), OM_OK);
            CHECK_INT(len, sizeof d3); CHECK(!memcmp(RB, d3, sizeof d3));
            /* 18 (I3): step 0's existence check fails with anything but "no file": nothing is opened for writing */
            reset();
            fake_fail_op = 'T'; fake_fail_times = 1; fake_fail_rc = -FR_DISK_ERR;
            CHECK_INT(om_sdw_replace("/aether/R.INI", data, sizeof data, SCR, gap, NULL), OM_SDW_ERR(-FR_DISK_ERR));
            CHECK(strchr(fake_log, 'O') == NULL);
            CHECK_INT(om_sdw_read_file("/aether/R.INI", RB, sizeof rbuf32, &len), OM_OK);
            CHECK_INT(len, sizeof d3);
            unseed("aether/R.INI.NEW");
        }
        fake_rename_overwrites = 0;
    }
    {   /* a path over 250 B is refused, never cut to a different name */
        static char lp[300];
        memset(lp, 'a', 260);
        memcpy(lp, "/aether/", 8);
        lp[260] = 0;
        reset();
        CHECK_INT(om_sdw_write_file(lp, data, 10, 0, SCR, gap, NULL), OM_E_FIELD);
        CHECK_INT(om_sdw_replace(lp, data, 10, SCR, gap, NULL), OM_E_FIELD);
        CHECK_INT(om_sdw_read_file(lp, RB, sizeof rbuf32, &len), OM_E_FIELD);
        CHECK_STR(fake_log, "");
    }
    {   /* a file built in pieces (a cart-only save over the import's buffers): off 0 creates or
         * truncates, a later off continues it; one file open at a time, chunks of OM_SDW_CHUNK, a sync each */
        static uint8_t big[9000], back[9000];
        fill(big, sizeof big, 3);
        reset();
        CHECK_INT(om_sdw_write_at("/aether/BIG.IMP", 0, big, 4088, SCR, gap, NULL), OM_OK);
        CHECK_INT(om_sdw_write_at("/aether/BIG.IMP", 4088, big + 4088, sizeof big - 4088, SCR, gap, NULL), OM_OK);
        CHECK_INT(disk("aether/BIG.IMP", back, sizeof back), (long)sizeof big);
        CHECK(!memcmp(big, back, sizeof big));
        CHECK_INT(fake_open_max, 1);
        CHECK_INT(om_sdw_write_at("/aether/BIG.IMP", 0, big, 100, SCR, gap, NULL), OM_OK);   /* off 0: truncates */
        CHECK_INT(disk("aether/BIG.IMP", back, sizeof back), 100L);
        CHECK_INT(om_sdw_write_at("/aether/BIG.IMP", 101, big, 10, SCR, gap, NULL), OM_E_RANGE); /* past the end */
        reset();
        fake_fail_op = 'W'; fake_fail_times = 2; fake_fail_rc = -FR_DISK_ERR;
        CHECK(om_sdw_write_at("/aether/BIG.IMP", 100, big, 5000, SCR, gap, NULL) != OM_OK);
    }
    return th_done("test_sdwrite");
}
