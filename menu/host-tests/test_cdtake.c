/* menu/host-tests/test_cdtake.c -- cdblk.c's sector takes against the LIBCD/CD-block model (fake_cdc.c).
 * Hardware CWX launch 2026-11-23 (Gex, FW163, JP V-Saturn): "CWX: CD block command failed rc -46" at the first
 * program file (271 sectors, many takes in one play), HIRQ 0x0F55 (EHST clear). LIBCD refuses a get-and-delete with
 * CMDBUSY until the previous host I/O has raised EHST; om_cd_take waited only for CMOK. BSD-3-Clause. */
#include "th.h"
#include "fake_cdc.h"
#include "cdblk.h"

static long s_vbl;
void om_txt_wait_vblank(void)                  /* a V-blank: the CD block runs on (500 ticks) */
{
    int i;
    s_vbl++;
    for (i = 0; i < 500; i++)
        (void)CD_HIRQ;
}

static uint8_t buf[300 * 2048] __attribute__((aligned(4)));

/* Two get-and-deletes back to back in one play, the second as soon as the count says a sector is there. */
static int two_takes(long latency, int32_t *second_err)
{
    int rc;
    fk_reset(latency, 0);
    om_cd_spin_cap(10000);
    if (om_cd_stream_start(1000, 3) != 0)
        return -1000;
    fk_add_sectors(3);
    if ((rc = om_cd_take(buf, 1)) != 0)
        return -2000 + rc;
    if (om_cd_stream_count() != 2)
        return -3000;
    rc = om_cd_take(buf, 1);
    *second_err = om_cd_last_error();
    return rc;
}

/* A take, then a delete straight away (no LIBCD call between them to fold the risen EHST into LIBCD's shadow). */
static int take_then_drop(long latency, int32_t *err)
{
    int rc;
    fk_reset(latency, 0);
    om_cd_spin_cap(10000);
    if (om_cd_stream_start(1000, 3) != 0)
        return -1000;
    fk_add_sectors(3);
    if ((rc = om_cd_take(buf, 1)) != 0)
        return -2000 + rc;
    rc = om_cd_drop(1);
    *err = om_cd_last_error();
    return rc ? rc : fk_sectors();
}

/* read_file's loop (cwxboot.c:65-93) over the public calls: one play of n sectors, take what is there, wait a V-blank
 * when nothing is. Returns sectors read, or the failing rc. */
static long stream_file(long latency, long every, int32_t n, int32_t *err)
{
    int32_t got = 0, c;
    int rc, stall = 0;
    fk_reset(latency, every);
    om_cd_spin_cap(10000);
    s_vbl = 0;
    if ((rc = om_cd_stream_start(2000, n)) != 0)
        return rc;
    while (got < n) {
        if ((c = om_cd_stream_count()) < 0) { *err = om_cd_last_error(); return c; }
        if (c == 0) {
            if (++stall > 600) return OM_CD_E_SECTORS;
            om_txt_wait_vblank();
            continue;
        }
        stall = 0;
        if (c > n - got) c = n - got;
        if ((rc = om_cd_take(buf, c)) != 0) { *err = om_cd_last_error(); return rc; }
        got += c;
    }
    *err = om_cd_last_error();
    return got;
}

int main(void)
{
    int32_t err = 0;
    long r;

    /* Mednafen-like: EHST rises with DataEnd. Back-to-back takes work -- why the emulator passed. */
    CHECK_INT(two_takes(0, &err), 0);
    CHECK_INT(err, 0);

    /* Hardware-like: EHST rises some time after DataEnd. The second take must still succeed. */
    CHECK_INT(two_takes(40, &err), 0);
    CHECK_INT(err, 0);
    CHECK_INT(fk_cmdbusy, 0);
    CHECK(om_cd_last_diag()->ehst_max >= 39 && om_cd_last_diag()->ehst_max <= 41);   /* the latency, measured */

    /* EHST never rises: bounded. The first take still returns its data; the second is refused by LIBCD's own gate
     * (step 4 GetdelSctData, CDC_ERR_CMDBUSY: code 401) instead of spinning forever. */
    CHECK_INT(two_takes(FK_NEVER, &err), OM_CD_E_CDC);
    CHECK_INT(err, 401);
    /* the launcher's error screen rows: HIRQ just before the refused call and at the refusal, EHST clear in both */
    CHECK(om_cd_last_diag()->pre[0] & CDC_HIRQ_CMOK);
    CHECK_INT(om_cd_last_diag()->pre[0] & CDC_HIRQ_EHST, 0);
    CHECK_INT(om_cd_last_diag()->post[0] & CDC_HIRQ_EHST, 0);
    CHECK_INT(om_cd_last_diag()->post[1], 0x0100);  /* CR1 = the last response (the model's GetSctNum) */

    /* om_cd_drop must not clear EHST itself: LIBCD clears it on issue and refuses the delete (CMDBUSY, code 1301)
     * when neither HIRQ nor its shadow holds it. */
    CHECK_INT(take_then_drop(40, &err), 1);
    CHECK_INT(err, 0);
    CHECK_INT(take_then_drop(0, &err), 1);
    CHECK_INT(err, 0);

    /* The Gex first file: 271 sectors in one play, EHST 40 ticks after each take. Sectors arrive every 2 ticks, so
     * the count is non-zero right after a take (on the console a sector lands every 6.7 ms while a take runs). */
    r = stream_file(40, 2, 271, &err);
    CHECK_INT(r, 271);
    CHECK_INT(err, 0);
    CHECK_INT(fk_cmdbusy, 0);
    r = stream_file(0, 2, 271, &err);             /* Mednafen-like */
    CHECK_INT(r, 271);
    r = stream_file(40, 300, 271, &err);          /* paced: a V-blank wait between most takes */
    CHECK_INT(r, 271);

    /* NiGHTS on hardware (2026-11-24): the IP read's play is accepted but no sector ever arrives -> -43 after 600
     * V-blanks. The error screen then needs what the drive was doing: the most sectors seen and a Get CD Status. */
    fk_reset(0, 0);
    om_cd_spin_cap(10000);
    CHECK_INT(om_cd_read(buf, 150, 16), OM_CD_E_SECTORS);
    CHECK_INT(om_cd_last_error(), 0);                       /* every LIBCD call answered: no CDC error rows */
    CHECK_INT(om_cd_last_diag()->cnt_max, 0);
    CHECK_INT(om_cd_last_diag()->cur_rc, 0);
    CHECK_INT(om_cd_last_diag()->cur_status, 0x03);         /* the play still "running" */
    CHECK_INT(om_cd_last_diag()->cur_fad, 150);
    fk_reset(0, 0);
    om_cd_spin_cap(10000);
    fk_add_sectors(5);                                      /* some sectors, then none */
    CHECK_INT(om_cd_read(buf, 150, 16), OM_CD_E_SECTORS);
    CHECK_INT(om_cd_last_diag()->cnt_max, 5);

    printf("test_cdtake: %d passed, %d failed\n", th_pass, th_fail);
    return th_fail || !th_pass;
}
