/* menu/host-tests/test_sshoff.c -- the direct SMPC SSHOFF (menu/launcher/sshoff.c) against a fake SMPC
 * register bank and the TVSTAT simulation of test_smpcwait.c (1 us per poll, NTSC-like 63 us lines, 263 lines, 224
 * shown). Checks: issued only inside the window (>= 300 us after V-BLANK-IN, SMPC_Manual.txt:885-893), SF set before
 * COMREG = 03H, a pending INTBACK broken first, every wait bounded, OREG31 checked. */
#include "th.h"
#include "sshoff.h"
#include "smpcwait.h"

typedef struct {
    uint32_t t, last_vb_in;
    int prev_vb, vb_dead;
} sim;

static sim S;
static uint8_t R[0x80];
static int sf_stuck, sf_held_until_break, oreg_wrong, n_cmd, sf_was_set, ireg_break_before_cmd;
static uint32_t cmd_at;            /* sim time of the COMREG write */
static uint8_t cmd;

uint16_t om_sshoff_tvstat(void *ctx)
{
    uint32_t t = S.t++, in = t % (63u * 263u), line = in / 63u;
    int vb = !S.vb_dead && line >= 224u, hb = (in % 63u) >= 52u;
    (void)ctx;
    if (vb && !S.prev_vb) S.last_vb_in = t;
    S.prev_vb = vb;
    return (uint16_t)((vb ? 0x0008u : 0) | (hb ? 0x0004u : 0));
}

uint8_t om_sshoff_rd(uint32_t off) { return R[off]; }

void om_sshoff_wr(uint32_t off, uint8_t v)
{
    R[off] = v;
    if (off == 0x01u && v == 0x40u && !n_cmd) {        /* break: the SMPC ends the INTBACK (and its busy SF) */
        ireg_break_before_cmd = 1;
        R[0x61] &= (uint8_t)~0x20u;
        if (sf_held_until_break) R[0x63] = 0;
    }
    if (off == 0x1Fu) {                                  /* COMREG */
        n_cmd++;
        cmd = v;
        cmd_at = S.t;
        sf_was_set = R[0x63] & 1u;
        if (!sf_stuck) {
            R[0x5F] = oreg_wrong ? 0x10u : v;            /* OREG31: the command code (SMPC_Manual.txt:1119-1135) */
            R[0x63] = 0;
        }
    }
}

static void reset(uint32_t start)
{
    memset(&S, 0, sizeof S);
    memset(R, 0, sizeof R);
    S.t = start;
    sf_stuck = sf_held_until_break = oreg_wrong = n_cmd = sf_was_set = ireg_break_before_cmd = 0;
    cmd = 0;
    cmd_at = 0;
}

int main(void)
{
    uint32_t start;
    /* 1. from many phases: SSHOFF issued once, SF set first, inside the window, rc 0 */
    for (start = 0; start < 63u * 263u; start += 1499u) {
        reset(start);
        CHECK_INT(om_smpc_sshoff(), 0);
        CHECK_INT(n_cmd, 1);
        CHECK_INT(cmd, 0x03);
        CHECK_INT(sf_was_set, 1);
        CHECK(cmd_at - S.last_vb_in >= 300u);
        CHECK(cmd_at - S.last_vb_in < 63u * 39u);
    }
    /* 2. a command still in flight (SF stuck): COMREG never written, -63 */
    reset(0); R[0x63] = 1; sf_stuck = 1;
    CHECK_INT(om_smpc_sshoff(), OM_SSHOFF_E_BUSY);
    CHECK_INT(n_cmd, 0);
    /* 3. SF clears on its own but stays set after SSHOFF: -63, bounded */
    reset(0); sf_stuck = 1;
    CHECK_INT(om_smpc_sshoff(), OM_SSHOFF_E_BUSY);
    CHECK_INT(n_cmd, 1);
    /* 4. an INTBACK waiting for continue/break: broken before SSHOFF */
    reset(0); R[0x61] = 0x20;
    CHECK_INT(om_smpc_sshoff(), 0);
    CHECK_INT(ireg_break_before_cmd, 1);
    CHECK_INT(cmd, 0x03);
    /* 4b. the same with SF held at 1 until the break: the break is not stuck behind the SF wait */
    reset(0); R[0x61] = 0x20; R[0x63] = 1; sf_held_until_break = 1;
    CHECK_INT(om_smpc_sshoff(), 0);
    CHECK_INT(ireg_break_before_cmd, 1);
    CHECK_INT(n_cmd, 1);
    CHECK(cmd_at - S.last_vb_in >= 300u && cmd_at - S.last_vb_in < 63u * 39u);
    /* 5. OREG31 is not 03H: refused */
    reset(0); oreg_wrong = 1;
    CHECK_INT(om_smpc_sshoff(), OM_SSHOFF_E_OREG);
    /* 6. no V-BLANK at all (display off): nothing issued, a smpcwait code, bounded */
    reset(0); S.vb_dead = 1;
    CHECK_INT(om_smpc_sshoff(), OM_SMPC_E_VBLANK);
    CHECK_INT(n_cmd, 0);
    printf("test_sshoff: %d passed, %d failed\n", th_pass, th_fail);
    return th_fail || !th_pass;
}
