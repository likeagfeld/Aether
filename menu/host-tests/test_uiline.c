/* menu/host-tests/test_uiline.c -- the mockup-worded lines: details meta line, region column, breadcrumb,
 * game count, and finding the rc suffix the status strip keeps whole.
 * BSD-3-Clause. */
#include "th.h"
#include "msgfmt.h"
#include "uiline.h"

#define STATUS_W 300            /* menu/views/folders.c FOOT_W: the footer strip the toast replaces (x 10..310) */

int main(void)
{
    om_lib_entry e;
    om_game g;
    om_sysid ip;
    char out[160], msg[64];
    int w, i;

    memset(&e, 0, sizeof e); memset(&g, 0, sizeof g); memset(&ip, 0, sizeof ip);
    g.year = 1998; g.players = 1; g.region = 1u << 2;              /* U */
    e.disc = 1; e.disc_total = 4;
    om_meta_line(&e, &g, "RPG", NULL, out, sizeof out);
    CHECK_STR(out, "RPG" OM_SEP_DOT "1998" OM_SEP_DOT "1 player" OM_SEP_DOT "Disc 1/4" OM_SEP_DOT "USA");
    g.players = 2; g.region = 1u | (1u << 3); e.disc_total = 1;   /* J + E; one disc: no "Disc" part */
    om_meta_line(&e, &g, NULL, NULL, out, sizeof out);
    CHECK_STR(out, "1998" OM_SEP_DOT "2 players" OM_SEP_DOT "Japan" OM_SEP_DOT "Europe");   /* addendum 6: words */
    memcpy(ip.date, "19951124", 9);                                /* no GAMES.DB record: year from IP.BIN */
    ip.area_flags = 1;
    om_meta_line(&e, NULL, "", &ip, out, sizeof out);
    CHECK_STR(out, "1995" OM_SEP_DOT "Japan");
    memset(&ip, 0, sizeof ip);
    om_meta_line(&e, NULL, NULL, &ip, out, sizeof out);
    CHECK_STR(out, "");
    om_meta_line(&e, &g, "RPG", NULL, out, 6);                     /* small buffer: cut, terminated */
    CHECK(strlen(out) < 6);

    {   /* area codes: canonical J T U B K A E L order, from IP.BIN when known (it has K, A, L), else the bits */
        char codes[9];
        g.region = (1u << 2) | (1u << 4) | 1u;                     /* J, U, B */
        om_area_codes(&e, &g, NULL, codes); CHECK_STR(codes, "JUB");
        memcpy(ip.area, "UEJ", 4);                                 /* the disc's own order is not the order shown */
        om_area_codes(&e, &g, &ip, codes); CHECK_STR(codes, "JUE");
        memcpy(ip.area, "ka L", 5);                                /* lower case and junk tolerated */
        om_area_codes(&e, &g, &ip, codes); CHECK_STR(codes, "KAL");
        memset(&ip, 0, sizeof ip);
        g.region = 0; e.area = 0;
        om_area_codes(&e, &g, NULL, codes); CHECK_STR(codes, "");
        CHECK_STR(om_area_name('U'), "USA"); CHECK_STR(om_area_name('A'), "Asia PAL");
        CHECK_STR(om_area_name('L'), "Latin America"); CHECK_STR(om_area_name('x'), "");
    }

    om_crumb("/", out, sizeof out);
    CHECK_STR(out, "Games");
    om_crumb("/RPG", out, sizeof out);
    CHECK_STR(out, "Games" OM_SEP_CRUMB "RPG");
    om_crumb("/Shooters/Classic/", out, sizeof out);                /* whole, never abbreviated (addendum 3) */
    CHECK_STR(out, "Games" OM_SEP_CRUMB "Shooters" OM_SEP_CRUMB "Classic");
    CHECK(strstr(out, "…") == NULL);
    om_crumb("/RPG", out, 9);                                     /* small buffer: cut, terminated */
    CHECK(strlen(out) < 9);

    om_count_line(12, 13, out, sizeof out); CHECK_STR(out, "12 games");
    om_count_line(1, 1, out, sizeof out); CHECK_STR(out, "1 game");
    om_count_line(0, 3, out, sizeof out); CHECK_STR(out, "3 items");
    om_count_line(0, 0, out, sizeof out); CHECK_STR(out, "empty");

    /* status: the rc suffix is found so the view can keep it whole at the right of the strip */
    om_msg_rc(msg, sizeof msg, "WWWWWWWWWWWWWWWWWWWW: ", "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMM", -120);
    CHECK(om_split_rc(msg) != NULL);
    CHECK_STR(om_split_rc(msg), " rc -120");
    CHECK(om_uf_width(&om_face_small, msg) > STATUS_W);            /* too wide: the reason will marquee */
    CHECK(om_uf_width(&om_face_small, om_split_rc(msg)) < 60);     /* the rc alone always has room */
    CHECK(om_split_rc("SORT: GENRE") == NULL);
    CHECK_STR(om_split_rc("a rc 1 then rc 2 rc -9"), " rc -9");   /* the last one is om_msg_rc's */
    for (i = -9; i <= 0; i++) {
        om_msg_rc(msg, 41, "FAVS.TXT NOT SAVED: ", "XXXXXXXXXXXXXXXXXXXXXXXXXXX", i * 1000);   /* 40 columns */
        CHECK(om_split_rc(msg) != NULL);
    }
    (void)w;
    return th_done("test_uiline");
}
