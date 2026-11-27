/* menu/host-tests/test_slaveoff.c -- the slave-SH-2 stop sequence (slaveoff.h) against a fake SGL whose
 * slSlaveOffWait fails while the persistent INTBACK is on, the way LIBSGL's _slRequestQueue does (HWF-78). */
#include "th.h"
#include "slaveoff.h"

typedef struct {
    int intback_on;          /* SGL's per-frame INTBACK (slGetStatus at boot, HWF-40) */
    int frames_since_cancel; /* the in-flight INTBACK needs OM_SLV_SETTLE frames to clear */
    int cancel_ng;           /* the next n slIntBackCancel calls fail (semaphore held) */
    int off_ng;              /* the next n slSlaveOffWait calls fail regardless */
    int direct_rc;           /* what the direct SSHOFF returns */
    int slave_on;
    int n_cancel, n_frame, n_off, n_direct;
    char trace[256];
    int tn;
} fake;

static void tr(fake *f, char c) { if (f->tn < 255) f->trace[f->tn++] = c; f->trace[f->tn] = 0; }

static int f_cancel(void *ctx)
{
    fake *f = ctx;
    f->n_cancel++;
    tr(f, 'C');
    if (f->cancel_ng > 0) { f->cancel_ng--; return -1; }
    if (f->intback_on) { f->intback_on = 0; f->frames_since_cancel = 0; }
    return 0;
}

static void f_frame(void *ctx)
{
    fake *f = ctx;
    f->n_frame++;
    tr(f, 'F');
    f->frames_since_cancel++;
}

static int f_off(void *ctx)
{
    fake *f = ctx;
    f->n_off++;
    tr(f, 'O');
    if (f->off_ng > 0) { f->off_ng--; return -1; }
    if (f->intback_on || f->frames_since_cancel < OM_SLV_SETTLE) return -1;   /* _slRequestQueue: WAIT -> NG */
    f->slave_on = 0;
    return 0;
}

static int f_direct(void *ctx)
{
    fake *f = ctx;
    f->n_direct++;
    tr(f, 'D');
    if (f->direct_rc == 0) f->slave_on = 0;
    return f->direct_rc;
}

static fake mk(void)
{
    fake f;
    memset(&f, 0, sizeof f);
    f.intback_on = 1;
    f.frames_since_cancel = 99;
    f.slave_on = 1;
    return f;
}

static int run(fake *f, om_slvoff_res *r)
{
    om_slvoff_ops o = { f_cancel, f_frame, f_off, f_direct, 0 };
    o.ctx = f;
    memset(r, 0x5A, sizeof *r);
    return om_slave_off_seq(&o, r);
}

int main(void)
{
    fake f;
    om_slvoff_res r;

    /* the menu's real state: INTBACK on; cancel, two frames, then SSHOFF through SGL */
    f = mk();
    CHECK_INT(run(&f, &r), OM_SLV_OK_SGL);
    CHECK_INT(r.result, OM_SLV_OK_SGL);
    CHECK_STR(f.trace, "CFFO");
    CHECK_INT(f.slave_on, 0);
    CHECK_INT(r.cancel_rc, 0); CHECK_INT(r.cancel_tries, 1);
    CHECK_INT(r.off_rc, 0); CHECK_INT(r.off_tries, 1);
    CHECK_INT(r.direct_rc, 1);
    CHECK_INT(f.n_direct, 0);

    /* the cancel's semaphore held twice: retried a frame apart, then the settle frames */
    f = mk(); f.cancel_ng = 2;
    CHECK_INT(run(&f, &r), OM_SLV_OK_SGL);
    CHECK_STR(f.trace, "CFCFCFFO");
    CHECK_INT(r.cancel_tries, 3);
    CHECK_INT(f.slave_on, 0);

    /* the cancel never succeeds: SGL's SSHOFF keeps failing (INTBACK still on), the direct SSHOFF does it */
    f = mk(); f.cancel_ng = 100;
    CHECK_INT(run(&f, &r), OM_SLV_OK_DIRECT);
    CHECK_INT(f.n_cancel, OM_SLV_CANCEL_TRIES);
    CHECK_INT(r.cancel_tries, OM_SLV_CANCEL_TRIES);
    CHECK(r.cancel_rc != 0);
    CHECK_INT(f.n_off, OM_SLV_OFF_TRIES);
    CHECK(r.off_rc != 0);
    CHECK_INT(f.n_direct, 1);
    CHECK_INT(r.direct_rc, 0);
    CHECK_INT(f.slave_on, 0);

    /* slSlaveOffWait fails twice for another reason, then succeeds */
    f = mk(); f.off_ng = 2;
    CHECK_INT(run(&f, &r), OM_SLV_OK_SGL);
    CHECK_STR(f.trace, "CFFOFOFO");
    CHECK_INT(r.off_tries, 3);
    CHECK_INT(f.n_direct, 0);

    /* slSlaveOffWait never succeeds: the direct path */
    f = mk(); f.off_ng = 100;
    CHECK_INT(run(&f, &r), OM_SLV_OK_DIRECT);
    CHECK_INT(f.n_off, OM_SLV_OFF_TRIES);
    CHECK_INT(f.n_direct, 1);

    /* everything fails: reported, bounded, and the caller carries on */
    f = mk(); f.cancel_ng = 100; f.off_ng = 100; f.direct_rc = -63;
    CHECK_INT(run(&f, &r), OM_SLV_E_FAILED);
    CHECK_INT(r.result, OM_SLV_E_FAILED);
    CHECK_INT(r.direct_rc, -63);
    CHECK_INT(f.slave_on, 1);
    CHECK(f.n_cancel + f.n_frame + f.n_off + f.n_direct <= 2 * OM_SLV_CANCEL_TRIES + OM_SLV_SETTLE + 2 * OM_SLV_OFF_TRIES + 1);
    CHECK_INT(f.n_direct, 1);

    /* no NULL op is ever called: a missing direct path is a failure, not a crash */
    f = mk(); f.cancel_ng = 100;
    {
        om_slvoff_ops o = { f_cancel, f_frame, f_off, 0, 0 };
        o.ctx = &f;
        CHECK_INT(om_slave_off_seq(&o, &r), OM_SLV_E_FAILED);
        CHECK_INT(f.n_direct, 0);
    }

    printf("test_slaveoff: %d passed, %d failed\n", th_pass, th_fail);
    return th_fail || !th_pass;
}
