/* menu/host-tests/test_browse.c -- virtual roots over tiny.library.idx + tiny.games.db.
 * Entries: 0 Daytona USA (Racing, JTUE, 1995, folder Racing), 1/2 Grandia d1/d2 (RPG, J, folder RPG),
 * 3 unknown.iso (at the root, no DB match, no region; its LIBRARY.IDX override MY HOMEBREW names no row, 2026-11-27). 2026-11-23 (Satiator Rings' model): a card
 * folder lists its folders first, by name (om_title_cmp: "Racing" < "RPG", not raw bytes), then its files. */
#include "th.h"
#include "browse.h"
#include "settings.h"
#include "title.h"

static om_tree t;
static om_sortctx s;
static om_browse b;

static void lab(char *out) { om_browse_label(&b, om_browse_selected(&b), out, 40); }

int main(void)
{
    uint32_t nl, ng;
    uint8_t *fl = th_load(FIX("tiny.library.idx"), &nl), *fg = th_load(FIX("tiny.games.db"), &ng);
    om_libidx l;
    om_gamesdb db;
    om_plist favs, recent;
    const om_item *it;
    uint16_t n;
    char buf[64];
    CHECK_INT(om_libidx_open(&l, fl, nl), OM_OK);
    CHECK_INT(om_gamesdb_open(&db, fg, ng), OM_OK);
    CHECK_INT(om_tree_build(&t, &l), OM_OK);
    om_sortctx_build(&s, &l, &db);
    om_plist_init(&favs, OM_FAVS_MAX); om_plist_toggle(&favs, 3); om_plist_toggle(&favs, 0);
    om_plist_init(&recent, OM_RECENT_MAX); om_plist_push_front(&recent, 3); om_plist_push_front(&recent, 1);
    om_browse_init(&b, &t, &l, &s, &db, &favs, &recent);
    it = om_browse_items(&b, &n);
    CHECK_INT(n, 8);                               /* 5 virtual roots + Racing (Daytona) + RPG (Grandia) + game 3 */
    CHECK_INT(it[0].kind, OM_IT_VROOT); CHECK_INT(it[4].idx, OM_VR_REGION);
    CHECK_INT(it[5].kind, OM_IT_FOLDER); CHECK_INT(it[5].idx, 1);      /* Racing: "Daytona USA" sorts first */
    CHECK_INT(it[6].kind, OM_IT_FOLDER); CHECK_INT(it[6].idx, 2);      /* RPG: "Grandia" */
    CHECK_INT(it[7].kind, OM_IT_GAME); CHECK_INT(it[7].idx, 3);
    lab(buf); CHECK_STR(buf, "Favorites/");                       /* addendum 7: American spelling */
    om_browse_path(&b, buf, sizeof buf); CHECK_STR(buf, "/");
    CHECK_INT(om_browse_back(&b), 0);                              /* B at the root does nothing (spec §7.4) */
    CHECK_INT(om_browse_enter(&b), 1);                             /* Favourites: sorted A-Z -> Daytona, unknown.iso */
    it = om_browse_items(&b, &n);
    CHECK_INT(n, 2); CHECK_INT(it[0].idx, 0); CHECK_INT(it[1].idx, 3);
    om_browse_path(&b, buf, sizeof buf); CHECK_STR(buf, "/Favorites");
    CHECK_INT(om_browse_enter(&b), 2);                             /* A on a game = launch */
    CHECK_INT(om_browse_back(&b), 1);
    om_browse_move(&b, 1);                                         /* Recently Played: MRU order, not sorted */
    CHECK_INT(om_browse_enter(&b), 1);
    it = om_browse_items(&b, &n);
    CHECK_INT(n, 2); CHECK_INT(it[0].idx, 1); CHECK_INT(it[1].idx, 3);
    om_browse_back(&b);
    om_browse_move(&b, 1);                                         /* A-Z: buckets D, G, U (the shown names: */
    om_browse_enter(&b);                                           /* single-game folders Daytona USA, Grandia; the
                                                                    * file unknown.iso, not its override) */
    it = om_browse_items(&b, &n);
    CHECK_INT(n, 3); CHECK_INT(it[0].kind, OM_IT_VSUB); CHECK_INT(it[0].idx, 'D'); CHECK_INT(it[2].idx, 'U');
    om_browse_move(&b, 1);
    lab(buf); CHECK_STR(buf, "G/");
    om_browse_enter(&b);
    it = om_browse_items(&b, &n);
    CHECK_INT(n, 2); CHECK_INT(it[0].idx, 1); CHECK_INT(it[1].idx, 2);
    om_browse_path(&b, buf, sizeof buf); CHECK_STR(buf, "/A-Z/G");
    {   /* 2026-11-25 (the return from a tool): the place saved as handoff.h's om_retblk saves it, replayed from a
         * fresh browser -- the same path, cursor and game, and B climbs the same way back */
        static om_browse r;
        om_loc path[OM_MAX_DEPTH + 3];
        uint32_t d = b.depth, k;
        om_browse_move(&b, 1);                                     /* Grandia disc 2 */
        for (k = 0; k < d; k++) path[k] = b.stack[k];
        path[d] = b.loc;
        om_browse_init(&r, &t, &l, &s, &db, &favs, &recent);
        CHECK_INT(om_browse_replay(&r, path, d, b.cur, b.top), 1);
        om_browse_path(&r, buf, sizeof buf); CHECK_STR(buf, "/A-Z/G");
        CHECK_INT(r.depth, 2);
        CHECK_INT(om_browse_selected(&r)->kind, OM_IT_GAME);
        CHECK_INT(om_browse_selected(&r)->idx, 2);
        CHECK_INT(om_browse_back(&r), 1); CHECK_INT(r.cur, 1);    /* back on G */
        CHECK_INT(om_browse_back(&r), 1); CHECK_INT(r.cur, 2);    /* back on A-Z */
        CHECK_INT(om_browse_replay(&r, path, d, 9, 0), 0);         /* a cursor past the list: the root, top */
        CHECK_INT(r.depth, 0); CHECK_INT(r.cur, 0);
        path[2].arg = 'Q';                                         /* a bucket that is gone */
        CHECK_INT(om_browse_replay(&r, path, d, 0, 0), 0);
        CHECK_INT(r.depth, 0); CHECK_INT(r.cur, 0);
        path[0].arg = 1;                                           /* not from the card root */
        CHECK_INT(om_browse_replay(&r, path, d, 0, 0), 0);
        CHECK_INT(om_browse_replay(&r, path, OM_MAX_DEPTH + 2, 0, 0), 0);   /* deeper than the stack */
        path[0].arg = 0;
        path[1].kind = 7;                                          /* not a location kind */
        CHECK_INT(om_browse_replay(&r, path, d, 0, 0), 0);
        CHECK_INT(om_browse_replay(&r, path, 0, 6, 0), 1);         /* the root itself, on the RPG folder */
        CHECK_INT(om_browse_selected(&r)->kind, OM_IT_FOLDER); CHECK_INT(om_browse_selected(&r)->idx, 2);
        om_browse_move(&b, -1);
    }
    om_browse_back(&b);
    CHECK_INT(b.cur, 1);                                           /* bucket list remembers G */
    om_browse_back(&b);
    CHECK_INT(b.cur, 2);                                           /* root remembers A-Z */
    om_browse_move(&b, 1);                                         /* Genre: Racing, RPG (name order), Unknown */
    om_browse_enter(&b);
    it = om_browse_items(&b, &n);
    CHECK_INT(n, 3);
    lab(buf); CHECK_STR(buf, "Racing/");
    om_browse_move(&b, 2); lab(buf); CHECK_STR(buf, "Unknown/");
    om_browse_enter(&b);
    it = om_browse_items(&b, &n);
    CHECK_INT(n, 1); CHECK_INT(it[0].idx, 3);
    om_browse_back(&b); om_browse_back(&b);
    om_browse_move(&b, 1);                                         /* Region: Japan, Asia, N. America, Europe, Unknown */
    om_browse_enter(&b);
    it = om_browse_items(&b, &n);
    CHECK_INT(n, 5);
    lab(buf); CHECK_STR(buf, "Japan/");
    om_browse_enter(&b);                                           /* Japan: Daytona (JTUE) + both Grandia discs */
    it = om_browse_items(&b, &n);
    CHECK_INT(n, 3);
    om_browse_back(&b); om_browse_back(&b);
    om_browse_set(&b, OM_SORT_AZ, OM_FILTER_ANY, 1u << 2);         /* U only: game 3 has no region: hidden; the
                                                                     * folder rows Racing and RPG are never filtered
                                                                     * (2026-11-23, Satiator Rings' model) */
    it = om_browse_items(&b, &n);
    CHECK_INT(n, 7);
    CHECK_INT(b.cur, 4);                                           /* "Region" still selected */
    om_browse_move(&b, -4);
    om_browse_enter(&b);                                           /* Favourites under the U filter: Daytona only */
    it = om_browse_items(&b, &n);
    CHECK_INT(n, 1); CHECK_INT(it[0].idx, 0);
    om_browse_back(&b);
    om_browse_set(&b, OM_SORT_AZ, 2, 0);                           /* genre RPG only: A-Z has just G */
    om_browse_move(&b, 2);
    om_browse_enter(&b);
    it = om_browse_items(&b, &n);
    CHECK_INT(n, 1); CHECK_INT(it[0].idx, 'G');
    om_browse_back(&b);
    om_browse_set(&b, OM_SORT_AZ, OM_FILTER_ANY, 0);
    om_browse_move(&b, 10);                                        /* to the last item, game 3, then into folder RPG */
    CHECK_INT(b.cur, 7);
    CHECK_INT(OM_ROWS, 8);                                         /* the view shows 8 rows of 14 px */
    CHECK_INT(b.top, 0);                                           /* 8 items fill the 8 rows exactly: no scroll */
    om_browse_page(&b, -1); CHECK_INT(b.cur, 0); CHECK_INT(b.top, 0);
    om_browse_page(&b, 1); CHECK_INT(b.cur, 7); CHECK_INT(b.top, 0);
    om_browse_step(&b, 1, 0); CHECK_INT(b.cur, 7);                 /* a held Down stops at the last item */
    om_browse_step(&b, 1, 1); CHECK_INT(b.cur, 0); CHECK_INT(b.top, 0);   /* a fresh press wraps to the top */
    om_browse_step(&b, -1, 0); CHECK_INT(b.cur, 0);                /* a held Up stops there */
    om_browse_step(&b, -1, 1); CHECK_INT(b.cur, 7);                /* and a fresh one wraps to the bottom */
    om_browse_step(&b, -1, 1); CHECK_INT(b.cur, 6);                /* inside the list: one row, as move */
    om_browse_step(&b, 1, 1); CHECK_INT(b.cur, 7);
    om_browse_move(&b, -1);                                      /* RPG is now one position closer to the end
                                                                     * than Racing (A-Z: Racing/Daytona < RPG/Grandia) */
    lab(buf); CHECK_STR(buf, "RPG/");
    CHECK_INT(om_browse_enter(&b), 1);
    om_browse_path(&b, buf, sizeof buf); CHECK_STR(buf, "/RPG");
    it = om_browse_items(&b, &n);
    CHECK_INT(n, 2); CHECK_INT(it[0].kind, OM_IT_GAME);
    om_plist_toggle(&favs, 3);                                     /* favourites now [0] */
    om_browse_back(&b); om_browse_move(&b, -10); om_browse_enter(&b);
    it = om_browse_items(&b, &n);
    CHECK_INT(n, 1);
    om_plist_toggle(&favs, 3);                                     /* added while the list is shown (X in the view) */
    om_browse_refresh(&b);
    it = om_browse_items(&b, &n);
    CHECK_INT(n, 2);
    CHECK_INT(om_browse_selected(&b)->idx, 0);                     /* selection stays on the same game */
    om_browse_move(&b, 1);
    CHECK_INT(om_browse_selected(&b)->idx, 3);
    om_plist_toggle(&favs, 3);                                     /* remove the selected game */
    om_browse_refresh(&b);
    it = om_browse_items(&b, &n);
    CHECK_INT(n, 1); CHECK_INT(b.cur, 0);                          /* cursor clamped, never past the end */
    free(fl); free(fg);

    {   /* 2026-11-23, Satiator Rings' browse model on the user's card (G:\SATIATOR BACKUP\!_LEISURE SUIT LARRY):
         * LARRY 1 (one .cue, no subfolder) is a single-game folder: an entry of its parent, named and listed as the
         * folder, A acts on its game. LARRY 6 (game.cue + OLD\) opens as a list: OLD (itself a single-game folder,
         * lsl6.cue) first with the folders, then game.cue, named by its exact file name. Built as the scanners lay
         * the card out (contract clarification 7). */
        om_lib_entry entbuf[8], e;
        uint16_t order[8];
        om_lib_folder fold[8];
        char pool[512];
        uint8_t out2[4096];
        uint32_t outlen;
        om_libw w;
        om_tree t2;
        om_libidx l2;
        om_sortctx s2;
        om_browse b2;
        om_plist favs2, recent2;
        const om_item *it2;
        uint16_t n2;
        char buf2[64];
        om_lib_entry e2;

#define MKE(p, f) do { memset(&e, 0, sizeof e); e.path = (p); e.folder = (f); e.disc = e.disc_total = 1; e.title = "";     e.image_type = 1; CHECK_INT(om_libw_entry(&w, &e), OM_OK); } while (0)

        om_libw_init(&w, entbuf, 8, order, fold, 8, pool, sizeof pool);
        CHECK_INT(om_libw_folder(&w, "", 0xFFFF), 0);
        CHECK_INT(om_libw_folder(&w, "!_LEISURE SUIT LARRY", 0), 1);
        CHECK_INT(om_libw_folder(&w, "!_LEISURE SUIT LARRY/LARRY 6", 1), 2);
        CHECK_INT(om_libw_folder(&w, "Zeta", 1), 3);                                    /* an empty folder */
        MKE("!_LEISURE SUIT LARRY/LARRY 1/Leisure Suit Larry 1 Saturn.cue", 1);         /* LARRY 1 */
        MKE("!_LEISURE SUIT LARRY/LARRY 6/OLD/lsl6.cue", 2);                            /* OLD */
        MKE("!_LEISURE SUIT LARRY/LARRY 6/game.cue", 2);
        MKE("!_LEISURE SUIT LARRY/aaa loose.iso", 1);                                   /* a loose file */
#undef MKE
        CHECK_INT(om_libw_finish(&w, 1, 0, 20261123u, out2, sizeof out2, &outlen), OM_OK);
        CHECK_INT(om_libidx_open(&l2, out2, outlen), OM_OK);
        CHECK_INT(om_tree_build(&t2, &l2), OM_OK);
        om_sortctx_build(&s2, &l2, NULL);
        om_plist_init(&favs2, OM_FAVS_MAX);
        om_plist_init(&recent2, OM_RECENT_MAX);
        om_browse_init(&b2, &t2, &l2, &s2, NULL, &favs2, &recent2);
        om_browse_move(&b2, 5); CHECK_INT(om_browse_enter(&b2), 1);    /* into !_LEISURE SUIT LARRY */
        it2 = om_browse_items(&b2, &n2);
        /* folders first, by name (LARRY 1 is a folder entry acting as its game), then the files */
        CHECK_INT(n2, 4);
        CHECK_INT(it2[0].kind, OM_IT_GAME);   om_libidx_entry(&l2, it2[0].idx, &e2);
        CHECK_STR(e2.path, "!_LEISURE SUIT LARRY/LARRY 1/Leisure Suit Larry 1 Saturn.cue");
        om_title(&l2, &e2, NULL, buf2, sizeof buf2); CHECK_STR(buf2, "LARRY 1");
        CHECK_INT(it2[1].kind, OM_IT_FOLDER); CHECK_INT(it2[1].idx, 2);  /* LARRY 6 */
        om_browse_label(&b2, &it2[1], buf2, sizeof buf2); CHECK_STR(buf2, "LARRY 6/");
        CHECK_INT(it2[2].kind, OM_IT_FOLDER); CHECK_INT(it2[2].idx, 3);  /* Zeta */
        CHECK_INT(it2[3].kind, OM_IT_GAME);   om_libidx_entry(&l2, it2[3].idx, &e2);
        om_title(&l2, &e2, NULL, buf2, sizeof buf2); CHECK_STR(buf2, "aaa loose.iso");   /* the exact file name */
        CHECK_INT(om_browse_enter(&b2), 2);                             /* A on LARRY 1: its game (the panel) */
        om_browse_move(&b2, 1); CHECK_INT(om_browse_enter(&b2), 1);     /* LARRY 6: a list */
        it2 = om_browse_items(&b2, &n2);
        CHECK_INT(n2, 2);
        CHECK_INT(it2[0].kind, OM_IT_GAME); om_libidx_entry(&l2, it2[0].idx, &e2);
        om_title(&l2, &e2, NULL, buf2, sizeof buf2); CHECK_STR(buf2, "OLD");             /* a folder entry, first */
        CHECK_INT(it2[1].kind, OM_IT_GAME); om_libidx_entry(&l2, it2[1].idx, &e2);
        om_title(&l2, &e2, NULL, buf2, sizeof buf2); CHECK_STR(buf2, "game.cue");
        CHECK_INT(om_browse_enter(&b2), 2);                             /* OLD acts as its game */
        om_browse_path(&b2, buf2, sizeof buf2); CHECK_STR(buf2, "/!_LEISURE SUIT LARRY/LARRY 6");
        CHECK_INT(om_sort_is_folder(&s2, it2[0].idx), 1); CHECK_INT(om_sort_is_folder(&s2, it2[1].idx), 0);
    }
    return th_done("test_browse");
}
