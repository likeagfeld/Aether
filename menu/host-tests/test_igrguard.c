/* menu/host-tests/test_igrguard.c -- Astal (hardware 2026-11-23, build 3c64908): A+B+C+Start at the title froze with
 * no in-game-reset colour. The game GFS_Loads its title overlays to 0x06002000 (TIT.BIN to 0x06008818), over the stub
 * at 0x06002800, and SYS_EXECDMP still pointed into it. SYS_EXECDMP now reaches the guard (menu/launcher/igrguard.c,
 * built here with IGR_HOST_TEST): it enters the stub only when the stub home 0x06002800-0x06002D48 still sums to the
 * seal written at arming time (om_igr_seal), and otherwise resets the console (SMPC SYSRES, retried).
 * Symptom guarded: "a game that loads over the stub freezes on A+B+C+Start instead of resetting". BSD-3-Clause. */
#include <setjmp.h>
#include "th.h"
#include "igr.h"

#define LONGS ((OM_IGR_STUB_END - OM_IGR_STUB) / 4u)
static uint32_t mem[LONGS];                    /* 0x06002800 .. 0x06002D48 as the game left it */
static struct { int entered, nsf, ncom, order_ok, reads_out; uint8_t last_com; } G;
static jmp_buf out;

uint32_t igr_g_rd32(uint32_t a)
{
    if (a < OM_IGR_STUB || a >= OM_IGR_STUB_END || (a & 3u)) {
        G.reads_out++;
        return 0;
    }
    return mem[(a - OM_IGR_STUB) / 4u];
}

void igr_g_wr8(uint32_t a, uint8_t v)
{
    if (a == 0x20100063u && v == 1u)
        G.nsf++;
    else if (a == 0x2010001Fu) {
        G.ncom++;
        G.last_com = v;
        G.order_ok = G.nsf == G.ncom;          /* SF = 1 before every COMREG write */
        if (G.ncom == 3)
            longjmp(out, 2);                   /* the reset is retried: three tries are enough to see */
    }
}

void igr_g_enter(void) { G.entered++; longjmp(out, 1); }

static int run(void)
{
    int r;
    memset(&G, 0, sizeof G);
    if ((r = setjmp(out)) == 0) {
        om_igr_guard();
        return 0;                              /* returned to the game: never allowed */
    }
    return r;
}

static void stub_image(void)                   /* a stand-in stub: any non-trivial bytes, then sealed */
{
    uint32_t i, x = 0x2F06E000u;
    for (i = 0; i < LONGS; i++)
        mem[i] = x = x * 1103515245u + 12345u;
    mem[LONGS - 1] = 0;                        /* the seal word is 0 in the linked image */
}

int main(void)
{
    uint32_t i, s;

    /* the layout the guard and launcher.ld share */
    CHECK_INT(OM_IGR_GUARD, 0x060003C0u);
    CHECK(OM_IGR_GUARD_END <= 0x06000400u);    /* the BIOS copyright text only (HWF-80) */
    CHECK(OM_IGR_SEAL != 0u && OM_IGR_SEAL != 0xFFFFFFFFu);

    /* the seal makes the stub home sum to OM_IGR_SEAL, and only the last long changes */
    stub_image();
    {
        uint32_t keep = mem[3];
        CHECK_INT(om_igr_seal(mem, LONGS), OM_IGR_SEAL);
        CHECK_INT(mem[3], keep);
    }
    for (s = 0, i = 0; i < LONGS; i++)
        s += mem[i];
    CHECK_INT(s, OM_IGR_SEAL);

    /* intact, sealed stub: the guard enters it and issues nothing */
    CHECK_INT(run(), 1);
    CHECK_INT(G.entered, 1);
    CHECK_INT(G.ncom, 0);
    CHECK_INT(G.reads_out, 0);

    /* Astal: an overlay loaded over the whole home -> console reset, the stub never entered */
    for (i = 0; i < LONGS; i++)
        mem[i] = 0x8B0B53E5u ^ (i * 0x9E3779B9u);
    CHECK_INT(run(), 2);
    CHECK_INT(G.entered, 0);
    CHECK_INT(G.last_com, 0x0D);                /* SMPC SYSRES */
    CHECK(G.order_ok);
    CHECK_INT(G.reads_out, 0);

    /* a zero-filled home (a game clearing its .bss there) is not a valid stub either */
    memset(mem, 0, sizeof mem);
    CHECK_INT(run(), 2);
    CHECK_INT(G.entered, 0);

    /* one long changed in the middle of an otherwise intact stub */
    stub_image();
    (void)om_igr_seal(mem, LONGS);
    mem[LONGS / 2] ^= 0x00010000u;
    CHECK_INT(run(), 2);
    CHECK_INT(G.entered, 0);

    /* only the seal word itself overwritten */
    stub_image();
    (void)om_igr_seal(mem, LONGS);
    mem[LONGS - 1] = 0;
    CHECK_INT(run(), 2);
    return th_done("test_igrguard");
}
