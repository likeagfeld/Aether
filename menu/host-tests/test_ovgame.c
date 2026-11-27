/* menu/host-tests/test_ovgame.c -- the per-game panel edits only on an explicit toggle (Kai's cursor bug,
 * research_rings_vs_kai §3.5), Reset to defaults asks twice, the list wraps on a fresh press and stops on a held one,
 * Manage saves and Cheats hand off. BSD-3-Clause. */
#include <string.h>
#include "th.h"
#include "overlay.h"
#include "pergame.h"
#include "settings.h"

int main(void)
{
    om_overlay o;
    om_pergame g;
    int f, i;
    om_pergame_default(&g, "MK-81045@U");
    om_overlay_open_game(&o, &g);
    CHECK_INT(o.kind, OM_OV_GAME); CHECK_INT(o.row, OM_OGR_LAUNCH);       /* highlighted by default (user decision) */
    CHECK_INT(om_overlay_key_game(&o, OM_K_RIGHT, &g), 0);                /* Launch: Left/Right do nothing */
    CHECK_INT(om_overlay_key_game(&o, OM_K_LEFT, &g), 0);
    /* hardware report on 3c64908 (A launched every game at once): the A that opened the panel, still held, or seen
     * again by the read-ahead's pad peeks while held, never launches -- A counts only after it was released */
    om_overlay_game_held(&o, OM_K_A);
    CHECK_INT(om_overlay_key_game(&o, OM_K_A, &g), 0);
    CHECK_INT(o.kind, OM_OV_GAME); CHECK_INT(o.row, OM_OGR_LAUNCH);
    om_overlay_game_held(&o, 0);                                          /* released: armed */
    om_overlay_game_held(&o, OM_K_A);                                     /* a new press stays armed */
    f = om_overlay_key_game(&o, OM_K_A, &g);
    CHECK_INT(f, OM_OV_LAUNCH);                                           /* a double-tap of A: start it now */
    CHECK_INT(o.kind, OM_OV_GAME);                                        /* the caller decides whether to close it */
    CHECK_INT(g.id[0] != 0, 1);                                           /* never dirties, never forgets */
    f = om_overlay_key_game(&o, OM_K_B, &g);
    CHECK_INT(f, OM_OV_CLOSED);                                           /* B: back, nothing written */
    om_overlay_open_game(&o, &g);
    for (i = 0; i < 4; i++) om_overlay_key_game(&o, OM_K_DOWN, &g);
    CHECK_INT(o.row, OM_OGR_SAVEMGR);                                     /* Manage saves, after Slot */
    om_overlay_game_held(&o, 0);
    CHECK_INT(om_overlay_key_game(&o, OM_K_RIGHT, &g), 0);                /* Left/Right: nothing */
    CHECK_INT(om_overlay_key_game(&o, OM_K_LEFT, &g), 0);
    CHECK_INT(om_overlay_key_game(&o, OM_K_A, &g), OM_OV_SAVEMGR);        /* A: this game's saves */
    CHECK_INT(o.dirty, 0); CHECK_INT(o.kind, OM_OV_GAME);
    om_overlay_key_game(&o, OM_K_DOWN, &g);
    CHECK_INT(o.row, OM_OGR_CHEATS);
    CHECK_INT(g.cheats, OM_PG_DEFAULT);                                   /* moving the cursor changes nothing */
    /* A on Cheats opens the cheat list (LAUNCHER.BIN's editor), never cycles; Left/Right still do */
    om_overlay_game_held(&o, 0);
    CHECK_INT(om_overlay_key_game(&o, OM_K_A, &g), OM_OV_CHEATS);
    CHECK_INT(g.cheats, OM_PG_DEFAULT);
    CHECK_INT(o.dirty, 0);
    /* 2026-11-25 (user): On or Off, never Default again -- the shown value (the global one with no choice: defs, here
     * cheats off) flips into the game's own choice; only Reset to defaults clears it */
    CHECK_INT(o.defs, 0);
    CHECK_INT(om_overlay_key_game(&o, OM_K_RIGHT, &g), 0);
    CHECK_INT(g.cheats, OM_PG_ON);
    CHECK_INT(om_overlay_key_game(&o, OM_K_LEFT, &g), 0);
    CHECK_INT(g.cheats, OM_PG_OFF);
    CHECK_INT(om_overlay_key_game(&o, OM_K_LEFT, &g), 0);
    CHECK_INT(g.cheats, OM_PG_ON);
    g.cheats = OM_PG_DEFAULT;
    o.defs = OM_OVD_CHEATS;                                               /* the global says on: the first flip is Off */
    CHECK_INT(om_overlay_key_game(&o, OM_K_RIGHT, &g), 0);
    CHECK_INT(g.cheats, OM_PG_OFF);
    CHECK_INT(om_overlay_key_game(&o, OM_K_A, &g), OM_OV_CHEATS);        /* A still opens the list */
    CHECK_INT(g.cheats, OM_PG_OFF);
    g.cheats = OM_PG_DEFAULT;
    o.dirty = 0;
    f = om_overlay_key_game(&o, OM_K_START, &g);
    CHECK_INT(f, OM_OV_CLOSED);                                           /* nothing changed: no write */
    om_overlay_open_game(&o, &g);
    om_overlay_game_held(&o, 0);
    om_overlay_key_game(&o, OM_K_DOWN, &g);                               /* off Launch, onto Loader */
    CHECK_INT(om_overlay_key_game(&o, OM_K_RIGHT, &g), 0);
    CHECK_INT(g.loader, OM_LDR_CWX);                                      /* auto -> cwx -> jhl -> auto */
    om_overlay_key_game(&o, OM_K_DOWN, &g);
    om_overlay_key_game(&o, OM_K_RIGHT, &g);
    CHECK_INT(g.saves, OM_PG_ON);                                         /* Off (no global) -> On */
    om_overlay_key_game(&o, OM_K_RIGHT, &g);
    CHECK_INT(g.saves, OM_PG_OFF);                                        /* On <-> Off, never Default */
    om_overlay_key_game(&o, OM_K_A, &g);
    CHECK_INT(g.saves, OM_PG_ON);
    g.saves = OM_PG_DEFAULT;
    o.defs = OM_OVD_SAVES;                                                /* the global says on */
    om_overlay_key_game(&o, OM_K_LEFT, &g);
    CHECK_INT(g.saves, OM_PG_OFF);
    om_overlay_key_game(&o, OM_K_RIGHT, &g);
    CHECK_INT(g.saves, OM_PG_ON);
    om_overlay_key_game(&o, OM_K_DOWN, &g);
    om_overlay_key_game(&o, OM_K_LEFT, &g);
    CHECK_INT(g.slot, OM_PG_SLOT_ASK);                                    /* 0 <- ask <- 9 ... */
    f = om_overlay_key_game(&o, OM_K_B, &g);
    CHECK_INT(f, OM_OV_CLOSED | OM_OV_PGSAVE);
    om_overlay_open_game(&o, &g);
    for (i = 0; i < 7; i++) om_overlay_key_game(&o, OM_K_DOWN, &g);
    CHECK_INT(o.row, OM_OGR_PAD);                                         /* right after IGR */
    CHECK_INT(om_overlay_key_game(&o, OM_K_RIGHT, &g), 0);                /* a static note: nothing to cycle */
    CHECK_INT(om_overlay_key_game(&o, OM_K_LEFT, &g), 0);
    f = om_overlay_key_game(&o, OM_K_A, &g);
    CHECK_INT(f, 0);                                                      /* never arms Forget, never dirties */
    f = om_overlay_key_game(&o, OM_K_B, &g);
    CHECK_INT(f, OM_OV_CLOSED);                                           /* no PGSAVE: nothing changed */
    om_overlay_open_game(&o, &g);
    om_overlay_game_held(&o, 0);
    for (i = 0; i < 20; i++) om_overlay_key_game(&o, OM_K_DOWN | OM_K_REP, &g);   /* held: stops at the end */
    CHECK_INT(o.row, OM_OGR_RESET); CHECK_INT(OM_OGR_RESET, OM_OGR_COUNT - 1);    /* the last row */
    CHECK_INT(OM_OGR_COUNT, OM_OV_VIS); CHECK_INT(o.top, 0);              /* every row fits: no scrolling */
    om_overlay_key_game(&o, OM_K_DOWN, &g);                               /* a fresh press wraps to Launch */
    CHECK_INT(o.row, OM_OGR_LAUNCH); CHECK_INT(o.top, 0);
    om_overlay_key_game(&o, OM_K_UP | OM_K_REP, &g);                      /* held Up at the top: stays */
    CHECK_INT(o.row, OM_OGR_LAUNCH);
    om_overlay_key_game(&o, OM_K_UP, &g);                                 /* fresh Up wraps to the bottom */
    CHECK_INT(o.row, OM_OGR_RESET);
    CHECK_INT(om_overlay_key_game(&o, OM_K_A, &g), 0);                    /* first A: "Press A again" */
    CHECK_INT(o.rcur, 1);
    om_overlay_key_game(&o, OM_K_UP, &g);
    CHECK_INT(o.rcur, 0);                                                 /* moving away disarms it */
    om_overlay_key_game(&o, OM_K_DOWN, &g);
    om_overlay_key_game(&o, OM_K_A, &g);
    f = om_overlay_key_game(&o, OM_K_A, &g);
    CHECK_INT(f, OM_OV_CLOSED | OM_OV_PGSAVE); CHECK_INT(g.id[0], 0);     /* id cleared = delete the section */
    {   /* a game with no product code (no game id): the panel still opens, with Launch alone -- nothing it could
         * remember the other rows by */
        om_pergame n;
        om_pergame_default(&n, "");
        om_overlay_open_game(&o, &n);
        CHECK_INT(o.kind, OM_OV_GAME); CHECK_INT(o.row, OM_OGR_LAUNCH);
        om_overlay_game_held(&o, 0);
        CHECK_INT(om_overlay_key_game(&o, OM_K_DOWN, &n), 0);
        CHECK_INT(o.row, OM_OGR_LAUNCH);                                  /* the cursor stays on Launch */
        CHECK_INT(om_overlay_key_game(&o, OM_K_UP, &n), 0);
        CHECK_INT(o.row, OM_OGR_LAUNCH);                                  /* wrapping a one-row list: Launch */
        CHECK_INT(om_overlay_key_game(&o, OM_K_A, &n), OM_OV_LAUNCH);
        CHECK_INT(om_overlay_key_game(&o, OM_K_B, &n), OM_OV_CLOSED);     /* nothing to save */
    }
    return th_done("test_ovgame");
}
