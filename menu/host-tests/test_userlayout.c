/* menu/host-tests/test_userlayout.c -- 2026-11-20 hardware report: Region and Year sort in Games/ disagreed with what
 * the list shows. The values below are the user's own card (build/release/card, made by omsat.cardbuild from E:\ --
 * the same 36 retail discs as G:\Saturn Games plus the XBAND set): per entry the LIBRARY.IDX area bits (from the
 * disc's IP.BIN), the GAMES.DB region bits, and the IP.BIN area string and date the details card reads once the game
 * is selected. Region: the flag the list shows must be the region the sort groups by, before AND after the IP.BIN
 * is loaded (it used to flip to the IP string's first canonical letter: Golden Axe "UTJ" and every XBAND disc
 * "JTUBKAEL" showed Japan while sorted under USA; Grandia's patched IP "U" showed USA while sorted under Japan).
 * Year: the year the details card shows must be the year the sort uses (GAMES.DB had year 0 everywhere, so the
 * year sort was title order; cardbuild now writes the IP.BIN year into the card's GAMES.DB). BSD-3-Clause. */
#include "th.h"
#include "layout.h"
#include "libsort.h"
#include "marquee.h"
#include "settings.h"
#include "uiline.h"

typedef struct { const char *title; uint8_t area, region; const char *ip_area, *ip_date; } ucase;

static const ucase k_card[] = {                     /* build/release/card/LIBRARY.IDX + GAMES.DB, G:\ IP.BIN headers */
    { "Body Special 264 ~Girls in Motion Puzzle Vol. 2~", 0x01, 0x01, "J", "19960621" },
    { "Fist", 0x01, 0x01, "J", "19961001" },
    { "Formula Karts Special Edition", 0x08, 0x08, "E", "19971007" },
    { "Frank Thomas Big Hurt Baseball", 0x04, 0x04, "U", "19960519" },
    { "Frankenstein - Through the Eyes of the Monster", 0x08, 0x08, "E", "19970611" },
    { "From TV Animation Slam Dunk - I Love Basketball", 0x01, 0x01, "J", "19950713" },
    { "Funky Head Boxers", 0x03, 0x01, "JT", "19961130" },
    { "Galaxy Fight", 0x04, 0x04, "U", "19960314" },
    { "Ghen War", 0x06, 0x04, "UT", "19951026" },
    { "Golden Axe The Duel", 0x07, 0x04, "UTJ", "19960201" },
    { "Grand Slam", 0x04, 0x04, "U", "19970604" },
    { "Grandia", 0x04, 0x01, "U", "19971108" },
    { "Gunbird", 0x01, 0x01, "J", "19951010" },
    { "GT 24", 0x01, 0x01, "J", "19980407" },
    { "Die Hard Arcade", 0x1F, 0x04, "JTUBKAEL", "19961101" },
    { "Touge King the Spirits", 0x1F, 0x01, "JTUBKAEL", "19951010" },
};
#define N (sizeof k_card / sizeof k_card[0])

static om_sortctx c;

static void setup(uint32_t i, om_lib_entry *e, om_game *g, om_sysid *ip, int db_year)
{
    const ucase *u = &k_card[i];
    memset(e, 0, sizeof *e); memset(g, 0, sizeof *g); memset(ip, 0, sizeof *ip);
    e->area = u->area; e->disc = e->disc_total = 1; e->path = u->title; e->title = "";
    g->region = u->region; g->title_en = g->title_rom = u->title;
    if (db_year)                                                  /* a card built by the fixed cardbuild */
        g->year = (uint16_t)((u->ip_date[0] - '0') * 1000 + (u->ip_date[1] - '0') * 100 + (u->ip_date[2] - '0') * 10 +
                             (u->ip_date[3] - '0'));
    strcpy(ip->area, u->ip_area); strcpy(ip->date, u->ip_date);
    ip->area_flags = om_area_flags(u->ip_area);
}

int main(void)
{
    uint16_t idx[N], tmp[N];
    uint32_t i;
    memset(&c, 0, sizeof c);
    for (i = 0; i < N; i++) {
        om_lib_entry e;
        om_game g;
        om_sysid ip;
        char before, after, out[200], yr[8];
        setup(i, &e, &g, &ip, 1);
        before = om_primary_area(&e, &g, NULL);                  /* the flag while the row was never selected */
        after = om_primary_area(&e, &g, &ip);                    /* ... and once its IP.BIN is loaded */
        c.key[i].title = k_card[i].title;
        c.key[i].region = om_region_bits(&e, &g);                /* what om_sortctx_build keys the region sort by */
        c.key[i].year = om_game_year(&g);
        c.key[i].genre = 0xFF;
        CHECK_INT(before, after);
        CHECK_INT(after, "JTUEB?"[om_region_primary(c.key[i].region)]);
        om_meta_line(&e, &g, NULL, &ip, out, sizeof out);         /* the details card's year is the sort's year */
        yr[0] = k_card[i].ip_date[0]; yr[1] = k_card[i].ip_date[1]; yr[2] = k_card[i].ip_date[2];
        yr[3] = k_card[i].ip_date[3]; yr[4] = 0;
        CHECK(!strncmp(out, yr, 4));
        CHECK_INT(c.key[i].year, atoi(yr));
    }
    {   /* the flags the user saw, named: Golden Axe and the XBAND Die Hard are USA; Touge (T-14401G) and Grandia
         * (T-4507G, an English patch of the Japanese disc) are Japan; Frankenstein is Europe */
        om_lib_entry e; om_game g; om_sysid ip;
        setup(9, &e, &g, &ip, 1);  CHECK_INT(om_primary_area(&e, &g, &ip), 'U');
        setup(14, &e, &g, &ip, 1); CHECK_INT(om_primary_area(&e, &g, &ip), 'U');
        setup(15, &e, &g, &ip, 1); CHECK_INT(om_primary_area(&e, &g, &ip), 'J');
        setup(11, &e, &g, &ip, 1); CHECK_INT(om_primary_area(&e, &g, &ip), 'J');
        setup(4, &e, &g, &ip, 1);  CHECK_INT(om_primary_area(&e, &g, &ip), 'E');
        /* no GAMES.DB record and no area bits: the IP.BIN's area string still names the flag */
        e.area = 0;
        CHECK_INT(om_primary_area(&e, NULL, &ip), 'E');
        CHECK_INT(om_primary_area(&e, NULL, NULL), 0);
        /* a GAMES.DB record with no year but a date (YYYYMMDD) sorts by the date's year, as the meta line shows */
        g.year = 0; g.date = 19970611u;
        CHECK_INT(om_game_year(&g), 1997);
    }
    c.n = N;
    for (i = 0; i < N; i++) idx[i] = (uint16_t)i;
    om_sort_entries(&c, OM_SORT_YEAR, idx, N, tmp);
    for (i = 1; i < N; i++)                                       /* never "1996, 1997, back to 1996" */
        CHECK(c.key[idx[i - 1]].year <= c.key[idx[i]].year);
    CHECK_STR(c.key[idx[0]].title, "From TV Animation Slam Dunk - I Love Basketball");   /* 1995, "F" < "G" < "T" */
    CHECK_STR(c.key[idx[N - 1]].title, "GT 24");                                       /* 1998 */
    for (i = 0; i < N; i++) idx[i] = (uint16_t)i;
    om_sort_entries(&c, OM_SORT_REGION, idx, N, tmp);
    for (i = 1; i < N; i++)                                       /* groups J, U, E: each row's flag, in group order */
        CHECK(om_region_primary(c.key[idx[i - 1]].region) <= om_region_primary(c.key[idx[i]].region));
    CHECK_STR(c.key[idx[0]].title, "Body Special 264 ~Girls in Motion Puzzle Vol. 2~");
    CHECK_STR(c.key[idx[N - 1]].title, "Frankenstein - Through the Eyes of the Monster");
    {   /* a card built before the fix (GAMES.DB year 0): the details card still shows the IP.BIN year, the sort cannot
         * know it -- documented, the remedy is re-running cardbuild */
        om_lib_entry e; om_game g; om_sysid ip;
        char out[200];
        setup(1, &e, &g, &ip, 0);
        CHECK_INT(om_game_year(&g), 0);
        om_meta_line(&e, &g, NULL, &ip, out, sizeof out);
        CHECK(!strncmp(out, "1996", 4));
    }
    printf("test_userlayout: %d passed, %d failed\n", th_pass, th_fail);
    return th_fail || !th_pass;
}
