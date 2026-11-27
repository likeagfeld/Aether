/* menu/host-tests/test_reveal.c -- start on the last-played game, inside its card folder.
 * Fixture facts (test_browse.c's header): entry 0 Daytona in folder Racing (1); entries 1/2 Grandia d1/d2 in RPG (2),
 * region J only; entry 3 MY HOMEBREW at the root; root items = 5 virtual roots + Racing + RPG + game 3. BSD-3-Clause. */
#include "th.h"
#include "browse.h"
#include "settings.h"

static om_tree t;
static om_sortctx s;
static om_browse b;

int main(void)
{
    uint32_t nl, ng;
    uint8_t *fl = th_load(FIX("tiny.library.idx"), &nl), *fg = th_load(FIX("tiny.games.db"), &ng);
    om_libidx l;
    om_gamesdb db;
    om_plist favs, recent;
    const om_item *it;
    char buf[64];
    CHECK_INT(om_libidx_open(&l, fl, nl), OM_OK);
    CHECK_INT(om_gamesdb_open(&db, fg, ng), OM_OK);
    CHECK_INT(om_tree_build(&t, &l), OM_OK);
    om_sortctx_build(&s, &l, &db);
    om_plist_init(&favs, OM_FAVS_MAX);
    om_plist_init(&recent, OM_RECENT_MAX);
    om_browse_init(&b, &t, &l, &s, &db, &favs, &recent);
    CHECK_INT(om_browse_reveal(&b, 1), 1);                      /* Grandia disc 1 -> /RPG, on it */
    om_browse_path(&b, buf, sizeof buf); CHECK_STR(buf, "/RPG");
    it = om_browse_selected(&b); CHECK(it != NULL);
    CHECK_INT(it->kind, OM_IT_GAME); CHECK_INT(it->idx, 1);
    CHECK_INT(om_browse_back(&b), 1);                           /* B: the root, cursor on the RPG folder */
    it = om_browse_selected(&b); CHECK_INT(it->kind, OM_IT_FOLDER); CHECK_INT(it->idx, 2);
    CHECK_INT(om_browse_reveal(&b, 2), 1);                      /* disc 2 of the same game: the same folder */
    it = om_browse_selected(&b); CHECK_INT(it->kind, OM_IT_GAME); CHECK_INT(it->idx, 2);
    CHECK_INT(om_browse_reveal(&b, 0), 1);                      /* Daytona -> /Racing (from inside /RPG: re-rooted) */
    om_browse_path(&b, buf, sizeof buf); CHECK_STR(buf, "/Racing");
    CHECK_INT(b.depth, 1);
    CHECK_INT(om_browse_reveal(&b, 3), 1);                      /* a game at the root */
    om_browse_path(&b, buf, sizeof buf); CHECK_STR(buf, "/");
    it = om_browse_selected(&b); CHECK_INT(it->idx, 3); CHECK_INT(b.cur, 7);
    om_browse_set(&b, OM_SORT_AZ, OM_FILTER_ANY, 1u << 2);      /* region U: Grandia (J) is filtered out */
    CHECK_INT(om_browse_reveal(&b, 1), 0);
    om_browse_path(&b, buf, sizeof buf); CHECK_STR(buf, "/");   /* back at the root, no message */
    CHECK_INT(b.depth, 0);
    CHECK_INT(om_browse_reveal(&b, 999), 0);                    /* not in LIBRARY.IDX */
    CHECK_INT(b.depth, 0);
    CHECK_INT(b.cur, 0);                                        /* the top */
    return th_done("test_reveal");
}
