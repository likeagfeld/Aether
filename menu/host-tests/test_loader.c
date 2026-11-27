/* menu/host-tests/test_loader.c -- manual > forced global > database > CWX default; the GAMES.DB hint
 * lookup the menu feeds it (record index for om_gdb_compat, the disc's area bits). BSD-3-Clause. */
#include "th.h"
#include "loader.h"
#include "settings.h"
#include "gamesdb.h"

int main(void)
{
    om_loader_choice c;
    c = om_loader_pick(OM_LDR_AUTO, OM_LDR_AUTO, OM_LH_NONE, 1);
    CHECK_INT(c.loader, OM_LDR_CWX); CHECK_INT(c.why, OM_LWHY_DEFAULT);           /* requirement 1: CWX default */
    CHECK_STR(om_loader_why(&c), "CWX (auto)");
    c = om_loader_pick(OM_LDR_AUTO, OM_LDR_AUTO, OM_LH_JHL, 1);
    CHECK_INT(c.loader, OM_LDR_JHL); CHECK_INT(c.why, OM_LWHY_DB);                /* Kai's JHL-only list */
    CHECK_STR(om_loader_why(&c), "JHL (database)");
    c = om_loader_pick(OM_LDR_CWX, OM_LDR_AUTO, OM_LH_JHL, 1);
    CHECK_INT(c.loader, OM_LDR_CWX); CHECK_INT(c.why, OM_LWHY_USER);              /* the user overrides the DB */
    CHECK_STR(om_loader_why(&c), "CWX (your choice)");
    c = om_loader_pick(OM_LDR_AUTO, OM_LDR_JHL, OM_LH_NONE, 1);
    CHECK_INT(c.loader, OM_LDR_JHL); CHECK_INT(c.why, OM_LWHY_GLOBAL);
    CHECK_STR(om_loader_why(&c), "JHL (settings)");
    c = om_loader_pick(OM_LDR_JHL, OM_LDR_CWX, OM_LH_CWX, 1);
    CHECK_INT(c.loader, OM_LDR_JHL); CHECK_INT(c.why, OM_LWHY_USER);
    c = om_loader_pick(OM_LDR_AUTO, OM_LDR_AUTO, OM_LH_CWX, 0);
    CHECK_INT(c.loader, OM_LDR_JHL); CHECK_INT(c.why, OM_LWHY_NOCWX);             /* CWX not built: say so */
    CHECK_STR(om_loader_why(&c), "JHL (CWX not available)");
    c = om_loader_pick(OM_LDR_CWX, OM_LDR_AUTO, OM_LH_NONE, 0);
    CHECK_INT(c.loader, OM_LDR_JHL); CHECK_INT(c.why, OM_LWHY_NOCWX);
    c = om_loader_pick(OM_LDR_AUTO, OM_LDR_AUTO, OM_LH_BAD, 1);
    CHECK_INT(c.loader, OM_LDR_CWX); CHECK_INT(c.why, OM_LWHY_DB);                /* both fail: try the default */
    CHECK_STR(om_loader_why(&c), "CWX (database: may fail)");
    c = om_loader_pick(9, 7, 5, 1);                                               /* out-of-range bytes: Auto */
    CHECK_INT(c.loader, OM_LDR_CWX); CHECK_INT(c.why, OM_LWHY_DEFAULT);
    CHECK_INT(OM_CWX_AVAILABLE, 1);
    {   /* the database half: tiny.games.db's GS-9001 carries hint JHL with region mask U only (test_gamesdb.c) */
        uint32_t n, rec = 99;
        uint8_t *f = th_load(FIX("tiny.games.db"), &n);
        om_gamesdb db;
        CHECK_INT(om_gamesdb_open(&db, f, n), OM_OK);
        CHECK_INT(om_gamesdb_rec(&db, "GS-9001", &rec), 1);
        CHECK_INT(rec, 1);                                                        /* sorted after GS-7103 */
        CHECK_INT(om_gamesdb_rec(&db, "XX-0000", &rec), 0);
        CHECK_INT(om_loader_db_hint(&db, 1, "GS-9001", 0x04), OM_LH_JHL);         /* a U disc */
        CHECK_INT(om_loader_db_hint(&db, 1, "GS-9001", 0x01), OM_LH_NONE);        /* a J disc: the mask excludes it */
        CHECK_INT(om_loader_db_hint(&db, 1, "GS-7103", 0x1F), OM_LH_NONE);        /* no compat row */
        CHECK_INT(om_loader_db_hint(&db, 1, "XX-0000", 0x1F), OM_LH_NONE);        /* not in the database */
        CHECK_INT(om_loader_db_hint(&db, 0, "GS-9001", 0x04), OM_LH_NONE);        /* GAMES.DB failed to load */
        CHECK_INT(om_loader_db_hint(&db, 1, "", 0x04), OM_LH_NONE);               /* an unmatched entry */
        c = om_loader_pick(OM_LDR_AUTO, OM_LDR_AUTO, om_loader_db_hint(&db, 1, "GS-9001", 0x04), OM_CWX_AVAILABLE);
        CHECK_INT(c.loader, OM_LDR_JHL);                                          /* end to end: Auto -> JHL */
        free(f);
    }
    {   /* the per-game panel's Loader value. A per-game choice shows as itself; Auto shows what the launch
         * would pick (the same om_loader_pick, with no per-game choice) and, unless it is the plain default, why */
        char b[40];
        om_loader_choice a;
        a = om_loader_pick(OM_LDR_AUTO, OM_LDR_AUTO, OM_LH_NONE, OM_CWX_AVAILABLE);
        CHECK_STR(om_loader_panel(OM_LDR_AUTO, &a, b, sizeof b), "Auto (CWX)");
        CHECK_STR(om_loader_panel(OM_LDR_CWX, &a, b, sizeof b), "CWX");
        CHECK_STR(om_loader_panel(OM_LDR_JHL, &a, b, sizeof b), "JHL");
        a = om_loader_pick(OM_LDR_AUTO, OM_LDR_AUTO, OM_LH_JHL, OM_CWX_AVAILABLE);
        CHECK_STR(om_loader_panel(OM_LDR_AUTO, &a, b, sizeof b), "Auto (JHL, database)");
        a = om_loader_pick(OM_LDR_AUTO, OM_LDR_JHL, OM_LH_NONE, OM_CWX_AVAILABLE);
        CHECK_STR(om_loader_panel(OM_LDR_AUTO, &a, b, sizeof b), "Auto (JHL, settings)");
        a = om_loader_pick(OM_LDR_AUTO, OM_LDR_AUTO, OM_LH_BAD, OM_CWX_AVAILABLE);
        CHECK_STR(om_loader_panel(OM_LDR_AUTO, &a, b, sizeof b), "Auto (CWX, database: may fail)");
        a = om_loader_pick(OM_LDR_AUTO, OM_LDR_AUTO, OM_LH_NONE, 0);
        CHECK_STR(om_loader_panel(OM_LDR_AUTO, &a, b, sizeof b), "Auto (JHL, CWX not available)");
        CHECK_STR(om_loader_panel(OM_LDR_AUTO, &a, b, 8), "Auto (J");               /* cut, always terminated */
        /* a per-game CWX without the CWX boot is what the launch does instead (om_loader_pick -> JHL) */
        CHECK_STR(om_loader_panel(OM_LDR_CWX, &a, b, sizeof b), "JHL (CWX not available)");
        CHECK_STR(om_loader_panel(OM_LDR_JHL, &a, b, sizeof b), "JHL");
    }
    return th_done("test_loader");
}
