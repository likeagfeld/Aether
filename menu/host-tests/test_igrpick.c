/* menu/host-tests/test_igrpick.c -- om_igr_want (menu/src/loader.h/.c) decides whether to arm the
 * in-game reset for a launch: OM_CF_NOIGR (gamesdb.h) forces it off; otherwise an explicit per-game choice
 * (OM_PG_ON/OM_PG_OFF, pergame.h) wins; otherwise the global setting (settings.h s_ps.set.igr). Also om_igr_panel,
 * the per-game panel's Soft reset value text. BSD-3-Clause. */
#include "th.h"
#include "loader.h"
#include "pergame.h"
#include "gamesdb.h"

int main(void)
{
    /* --- om_igr_want: the plain 3-way, no compat restriction ------------------------------------------------- */
    CHECK_INT(om_igr_want(0, OM_PG_DEFAULT, 0), 0);              /* global Off, no per-game choice: Off */
    CHECK_INT(om_igr_want(1, OM_PG_DEFAULT, 0), 1);              /* global On, no per-game choice: On */
    CHECK_INT(om_igr_want(0, OM_PG_ON, 0), 1);                   /* per-game On overrides global Off */
    CHECK_INT(om_igr_want(1, OM_PG_OFF, 0), 0);                  /* per-game Off overrides global On */
    CHECK_INT(om_igr_want(1, OM_PG_ON, 0), 1);
    CHECK_INT(om_igr_want(0, OM_PG_OFF, 0), 0);

    /* --- OM_CF_NOIGR forces Off no matter what global/per-game say ------------------------------------------- */
    CHECK_INT(om_igr_want(1, OM_PG_ON, OM_CF_NOIGR), 0);         /* a user's per-game On loses to the compat flag */
    CHECK_INT(om_igr_want(1, OM_PG_DEFAULT, OM_CF_NOIGR), 0);
    CHECK_INT(om_igr_want(0, OM_PG_OFF, OM_CF_NOIGR), 0);
    CHECK_INT(om_igr_want(0, OM_PG_DEFAULT, OM_CF_NOIGR), 0);
    /* unrelated compat bits set alongside NOIGR: still forced off */
    CHECK_INT(om_igr_want(1, OM_PG_ON, (uint8_t)(OM_CF_3DPAD | OM_CF_NOCHEAT | OM_CF_NOIGR)), 0);

    /* --- unrelated compat bits with NOIGR clear: no effect ---------------------------------------------------- */
    CHECK_INT(om_igr_want(0, OM_PG_DEFAULT, (uint8_t)(OM_CF_3DPAD | OM_CF_NOCHEAT)), 0);
    CHECK_INT(om_igr_want(1, OM_PG_DEFAULT, (uint8_t)(OM_CF_3DPAD | OM_CF_NOCHEAT)), 1);
    CHECK_INT(om_igr_want(0, OM_PG_OFF, OM_CF_STDPAD), 0);
    CHECK_INT(om_igr_want(1, OM_PG_ON, OM_CF_STDPAD), 1);

    /* --- out-of-range per_game bytes (a hand-edited or corrupt PERGAME.INI) read as Default -------------------- */
    CHECK_INT(om_igr_want(1, 3, 0), 1);
    CHECK_INT(om_igr_want(0, 3, 0), 0);
    CHECK_INT(om_igr_want(1, 255, 0), 1);
    CHECK_INT(om_igr_want(0, 255, OM_CF_NOIGR), 0);

    /* --- om_igr_panel: the per-game panel's Soft reset value ---------------------------------------------------- */
    {
        char b[40];
        CHECK_STR(om_igr_panel(OM_PG_ON, 0, 0, b, sizeof b), "On");
        CHECK_STR(om_igr_panel(OM_PG_OFF, 1, 0, b, sizeof b), "Off");
        CHECK_STR(om_igr_panel(OM_PG_DEFAULT, 1, 0, b, sizeof b), "Default (On)");
        CHECK_STR(om_igr_panel(OM_PG_DEFAULT, 0, 0, b, sizeof b), "Default (Off)");
        /* Default resolves through the same NOIGR rule even though the row shows "Default" */
        CHECK_STR(om_igr_panel(OM_PG_DEFAULT, 1, OM_CF_NOIGR, b, sizeof b), "Default (Off)");
        CHECK_STR(om_igr_panel(OM_PG_DEFAULT, 1, 0, b, 8), "Default");          /* cut, always terminated */
        CHECK_STR(om_igr_panel(OM_PG_ON, 0, 0, b, 1), "");                      /* cap 1: only the terminator fits */
    }
    return th_done("test_igrpick");
}
