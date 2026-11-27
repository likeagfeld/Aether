/* menu/host-tests/test_sattimeout.c -- M2-R7 (menu controller ruling): proves exec_cmd's bounded HIRQ wait
 * over the REAL menu/lib/satiator/satiator.c (built with -DSAT_HOST_TEST, see that file's top-of-file
 * comment), not a reimplementation of its loop. The fake below is the "iteration-counting" register bank
 * the ruling asked for: every CDB_REG_HIRQ access (read or write) increments a counter, so the two
 * scenarios below assert a measured poll count, not just a return code.
 *
 * Entry point: s_mode(s_api), the exact call menu/app/main.c makes first at boot (ss_main). Tracing it:
 * cur_mode starts s_cdrom (HWF-54: the 0x9300/MPED bootstrap is gone) -> the FIRST-ever exec_cmd is the
 * API switch (cmd 0xE000, wait=HIRQ_EFLS) -- it is this call that hangs on a cold boot with no Satiator
 * attached (the ruling's own example).
 * BSD-3-Clause. */
#include "th.h"
#include "satiator.h"

/* DATA_HANG (fix round 1, coordinator review): forces every HIRQ bit EXCEPT DRDY (0x0002) set, so
 * exec_cmd's own command-phase waits (CMOK, and whatever "wait" the caller passed -- HIRQ_MPED for
 * simplecall, HIRQ_EHST for buffer_xfer's own exec_cmd call) succeed on the first check, and only
 * buffer_xfer's SEPARATE, later DRDY wait ever times out. This isolates "the data phase hangs" from
 * "the command phase hangs" (NEVER, below), which is what exposed the 17-site ignored-return bug: with
 * the command phase always fast, the ONLY way to reach a timeout at all is through buffer_xfer's DRDY
 * loop -- exactly the bug's trigger condition. */
typedef enum { NEVER, ALWAYS, DATA_HANG } resp_t;
static resp_t g_mode = NEVER;
static uint32_t g_polls;
static uint16_t g_hirq;
static uint16_t g_cr[4];
static uint32_t g_datatrns;

volatile uint16_t *sat_test_hirq(void)
{
    g_polls++;
    if (g_mode == ALWAYS)
        g_hirq = 0xFFFF;   /* force "every awaited bit already set" before the caller's read/write lands */
    else if (g_mode == DATA_HANG)
        g_hirq = 0xFFFD;   /* everything set except bit1 (HIRQ_DRDY=0x0002, internal to satiator.c, not
                             * in satiator.h -- the public header), forever */
    return &g_hirq;
}

volatile uint16_t *sat_test_cr(int n)
{
    n &= 3;
    /* Mirrors sat_test_hirq()'s force-before-return trick: exec_cmd always WRITES CR1-CR4 from its own
     * cmd[] first (clobbering any preset value), so forcing on every access -- not just once -- is what
     * makes the LAST access see the forced value instead of whatever that call's cmd[] happened to be.
     * - CR3 (n==2), ALWAYS only: is_satiator_present()'s MPEG-version-byte check reads 2 on a real
     *   Satiator; without this, ALWAYS would still legitimately report "not present" (rc=-1).
     * - CR1 (n==0), ALWAYS or DATA_HANG: a real Satiator reports success by leaving CR1's high byte 0
     *   after a command completes. Without this, get_check_stat() (simplecall's status check) and
     *   buffer_xfer's own "if (CDB_REG_CR1) return -1" both read back whatever cmd[0] a LATER exec_cmd
     *   call happened to write (e.g. get_stat()'s own 0x9000), misreading it as a protocol-level error
     *   and masking the data-phase timeout DATA_HANG exists to isolate -- measured this session: without
     *   this force, s_read/s_stat(NULL) under DATA_HANG returned -144, not SAT_ETIMEDOUT. */
    if (g_mode == ALWAYS && n == 2)
        g_cr[2] = 2;
    if ((g_mode == ALWAYS || g_mode == DATA_HANG) && n == 0)
        g_cr[0] = 0;
    return &g_cr[n];
}
volatile uint32_t *sat_test_datatrns(void) { return &g_datatrns; }
void sat_test_reset_mode(void);   /* satiator.c, SAT_HOST_TEST-only: cur_mode is file-static there */

static void reset(resp_t m)
{
    g_mode = m; g_polls = 0; g_hirq = 0; g_cr[0] = g_cr[1] = g_cr[2] = g_cr[3] = 0;
    if (m == ALWAYS)
        g_cr[2] = 2;   /* is_satiator_present()'s MPEG-version-byte check (satiator.c): a real Satiator
                        * reads 2 here; without this the ALWAYS scenario would still legitimately report
                        * "not present" (rc=-1) since nothing else in this fake emulates that byte. */
    sat_test_reset_mode();
}

int main(void)
{
    int rc;

    /* Scenario 1 (RED/GREEN target): the Satiator never responds. Measured by this test, not asserted:
     * exec_cmd's CMOK wait loop in s_mode(s_api)'s 0xE000 call reads CDB_REG_HIRQ exactly
     * SAT_HIRQ_WAIT_CAP times (the loop body is "read, decrement, check" -- it returns on the read that
     * makes the cap hit zero) plus the one write that clears the register before the loop starts. */
    reset(NEVER);
    rc = s_mode(s_api);
    printf("test_sattimeout: NEVER -> s_mode(s_api) rc=%d polls=%u (cap=%u)\n", rc, g_polls, SAT_HIRQ_WAIT_CAP);
    CHECK_INT(rc, SAT_ETIMEDOUT);
    /* Exact, measured claim: 1 write (CDB_REG_HIRQ = ~mask, arming the wait) + SAT_HIRQ_WAIT_CAP reads
     * (the CMOK loop's "read, decrement, check" runs exactly that many times before returning) == the
     * total HIRQ-register accesses for the one exec_cmd call this entry point ever reaches. Against
     * SAT_HIRQ_WAIT_CAP (satiator.h), not a hardcoded literal, so a later, measured S3 retune of the cap
     * cannot make this check silently stale. */
    CHECK_INT(g_polls, (long long)SAT_HIRQ_WAIT_CAP + 1);

    /* Scenario 2: same NEVER fake, but through simplecall/get_check_stat instead of s_mode's own direct
     * exec_cmd calls -- s_open() calls buffer_write() first. Fix round 1 (coordinator review) closed the
     * gap where that call's return was ignored, so this now costs exactly 1x(CAP+1): the FIRST exec_cmd
     * (inside buffer_write) times out, is checked, and s_open returns immediately -- it never reaches the
     * second, simplecall exec_cmd at all. Before that fix this was 2x(CAP+1) (the first timeout silently
     * absorbed, the second one propagating instead) -- this assertion is what would have gone RED at the
     * fix, and is the permanent regression guard against the gap reopening. */
    reset(NEVER);
    rc = s_open("/aether/GAMES.DB", FA_READ | FA_OPEN_EXISTING);
    printf("test_sattimeout: NEVER -> s_open rc=%d polls=%u\n", rc, g_polls);
    CHECK_INT(rc, SAT_ETIMEDOUT);
    CHECK_INT(g_polls, (long long)SAT_HIRQ_WAIT_CAP + 1);
    /* hardware 2026-11-27: the hang watchdog's SAT row (satiator.h s_diag) -- the two timeouts so far, the last one
     * the write_buffer (0x9100) that starts s_open, as in the photo's PR s_open+0x34; nothing completed yet (s_diag is
     * never reset: these counts are the scenarios' running totals) */
    CHECK_INT(s_diag.tmo, 2);
    CHECK_INT(s_diag.tmo_cmd, 0x9100);
    CHECK_INT(s_diag.ok, 0);
    reset(ALWAYS);
    rc = s_close(3);                                 /* c_close (MPED) + get_status: two completed at once */
    CHECK_INT(rc, 0);
    CHECK_INT(s_diag.ok, 2);
    CHECK_INT(s_diag.tmo, 2);
    CHECK_INT(s_diag.max_polls, 0);                  /* polls past the first read: none */

    /* Scenario 2a (fix round 1, RED/GREEN target): a DATA-PHASE timeout, not a command-phase one.
     * s_read()'s own command phase (simplecall) succeeds immediately (DATA_HANG keeps CMOK/MPED set);
     * get_length() then returns 0 (sat_result[2..3] == CR3/CR4 == g_cr[2..3], left at their reset() value
     * of 0 in this scenario); buffer_read(buf, 0) still calls buffer_xfer, whose OWN exec_cmd(EHST)
     * succeeds immediately too, and only then hits the DRDY wait, which never clears -- so this call
     * never even reaches the n*CAP range exec_cmd itself produces; it is driven entirely by
     * buffer_xfer's separate DRDY loop. Before fix round 1, s_read ignored buffer_read's return and
     * returned len (0) as a successful zero-byte read instead of SAT_ETIMEDOUT -- the exact "stale data
     * read as success" bug the coordinator flagged. */
    reset(DATA_HANG);
    {
        uint8_t buf[4];
        rc = s_read(0, buf, 0);
    }
    printf("test_sattimeout: DATA_HANG -> s_read rc=%d polls=%u\n", rc, g_polls);
    CHECK_INT(rc, SAT_ETIMEDOUT);
    CHECK(g_polls > SAT_HIRQ_WAIT_CAP && g_polls <= SAT_HIRQ_WAIT_CAP + 64u);   /* M2 deferred (e): one capped wait */

    /* Scenario 2b: same data-phase hang, through s_stat(NULL,...) (the readdir path satfs.c's sat_list
     * loops on). Before fix round 1, s_stat ignored buffer_read's return and returned len-9 (a small,
     * plausible-looking length) as if the readdir entry had been read successfully. */
    reset(DATA_HANG);
    {
        uint32_t sb[20];
        rc = s_stat(NULL, (s_stat_t *)sb, (int)sizeof sb - 4);
    }
    printf("test_sattimeout: DATA_HANG -> s_stat(NULL) rc=%d polls=%u\n", rc, g_polls);
    CHECK_INT(rc, SAT_ETIMEDOUT);
    CHECK(g_polls > SAT_HIRQ_WAIT_CAP && g_polls <= SAT_HIRQ_WAIT_CAP + 64u);

    /* M2 deferred (e): s_opendir's buffer_write data phase (satiator.c:348-355) under the same DATA_HANG. */
    reset(DATA_HANG);
    {
        static uint32_t name32[4];
        memcpy(name32, "/x", 3);
        rc = s_opendir((const char *)name32);
    }
    printf("test_sattimeout: DATA_HANG -> s_opendir rc=%d polls=%u\n", rc, g_polls);
    CHECK_INT(rc, SAT_ETIMEDOUT);
    CHECK(g_polls > SAT_HIRQ_WAIT_CAP && g_polls <= SAT_HIRQ_WAIT_CAP + 64u);

    /* Positive control (an A/B needs both legs): a Satiator that responds immediately must NOT be
     * reported as "no response", and must need only a handful of register touches, not anywhere near
     * SAT_HIRQ_WAIT_CAP -- otherwise this test could not tell "bounded-but-slow" from "always wrong". */
    reset(ALWAYS);
    rc = s_mode(s_api);
    printf("test_sattimeout: ALWAYS -> s_mode(s_api) rc=%d polls=%u\n", rc, g_polls);
    CHECK_INT(rc, 0);
    CHECK(g_polls > 0 && g_polls < 1000u);

    return th_done("test_sattimeout");
}
