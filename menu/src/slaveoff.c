/* menu/src/slaveoff.c -- see slaveoff.h. BSD-3-Clause. */
#include "slaveoff.h"

int om_slave_off_seq(const om_slvoff_ops *ops, om_slvoff_res *r)
{
    int i;
    r->cancel_rc = -1;
    r->cancel_tries = 0;
    r->off_rc = -1;
    r->off_tries = 0;
    r->direct_rc = 1;
    for (i = 0; i < OM_SLV_CANCEL_TRIES; i++) {                 /* 1. the INTBACK off (the semaphore may be held) */
        if (i)
            ops->frame(ops->ctx);
        r->cancel_tries++;
        if ((r->cancel_rc = ops->cancel(ops->ctx)) == 0)
            break;
    }
    for (i = 0; i < OM_SLV_SETTLE; i++)                          /* 2. the INTBACK in flight ends */
        ops->frame(ops->ctx);
    for (i = 0; i < OM_SLV_OFF_TRIES; i++) {                    /* 3. SSHOFF through SGL, rc checked */
        if (i)
            ops->frame(ops->ctx);
        r->off_tries++;
        if ((r->off_rc = ops->offwait(ops->ctx)) == 0)
            return r->result = OM_SLV_OK_SGL;
    }
    if (!ops->direct)                                           /* 4. the direct SSHOFF */
        return r->result = OM_SLV_E_FAILED;
    r->direct_rc = ops->direct(ops->ctx);
    return r->result = r->direct_rc == 0 ? OM_SLV_OK_DIRECT : OM_SLV_E_FAILED;
}
