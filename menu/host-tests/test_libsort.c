/* menu/host-tests/test_libsort.c -- sort modes and filters (spec §7.6). Unit keys are built by hand; the fixture
 * check proves key extraction from tiny.library.idx + tiny.games.db. BSD-3-Clause. */
#include "th.h"
#include "gamesdb.h"
#include "libsort.h"
#include "settings.h"

static om_sortctx c;

static char nm[64];
static const char *kname(uint16_t e) { om_sortkey_name(&c, e, nm, sizeof nm); return nm; }

static void key(uint16_t e, const char *t, uint16_t y, uint8_t g, uint8_t r)
{
    c.key[e].title = t; c.key[e].year = y; c.key[e].genre = g; c.key[e].region = r;
}

int main(void)
{
    uint16_t idx[8], tmp[8];
    uint32_t nl, ng, i;
    uint8_t *fl = th_load(FIX("tiny.library.idx"), &nl), *fg = th_load(FIX("tiny.games.db"), &ng);
    om_libidx l;
    om_gamesdb db;
    memset(&c, 0, sizeof c);
    key(0, "zeta", 1996, 1, 0x04);       /* genre 1, U */
    key(1, "Alpha", 0, 0, 0x01);         /* genre 0, J, year unknown */
    key(2, "beta", 1994, 0xFF, 0x00);    /* unknown genre, no region */
    key(3, "alpha", 1995, 1, 0x08);      /* genre 1, E */
    key(4, "Beta", 1994, 0, 0x06);       /* genre 0, T+U -> first bit T */
    c.n = 5; c.ngenre = 2;
    c.genre_rank[0] = 1; c.genre_rank[1] = 0; c.genre_rank[0xFF] = 255;    /* genre 1's name sorts first */
    for (i = 0; i < 5; i++) idx[i] = (uint16_t)i;
    om_sort_entries(&c, OM_SORT_AZ, idx, 5, tmp);
    CHECK_INT(idx[0], 1); CHECK_INT(idx[1], 3); CHECK_INT(idx[2], 4); CHECK_INT(idx[3], 2); CHECK_INT(idx[4], 0);
    om_sort_entries(&c, OM_SORT_GENRE, idx, 5, tmp);    /* genre 1 (3 alpha, 0 zeta), genre 0 (1 Alpha, 4 Beta), unknown */
    CHECK_INT(idx[0], 3); CHECK_INT(idx[1], 0); CHECK_INT(idx[2], 1); CHECK_INT(idx[3], 4); CHECK_INT(idx[4], 2);
    om_sort_entries(&c, OM_SORT_REGION, idx, 5, tmp);   /* J:1  T:4  U:0  E:3  none:2 */
    CHECK_INT(idx[0], 1); CHECK_INT(idx[1], 4); CHECK_INT(idx[2], 0); CHECK_INT(idx[3], 3); CHECK_INT(idx[4], 2);
    om_sort_entries(&c, OM_SORT_YEAR, idx, 5, tmp);     /* 1994 Beta, 1994 beta (title tie -> "Beta" < "beta"), 1995, 1996, unknown */
    CHECK_INT(idx[0], 4); CHECK_INT(idx[1], 2); CHECK_INT(idx[2], 3); CHECK_INT(idx[3], 0); CHECK_INT(idx[4], 1);

    /* fix: the primary bit of a multi-region game is picked in uiline.c's canonical area order (J,T,U,B,E --
     * Brazil before Europe), not plain bit order (J,T,U,E,B), so a game whose only flag the UI would show is
     * "Brazil" (region E|B, no J/T/U) sorts with the other Brazil-primary games, not with "Europe" ones. The
     * group RANK stays J,T,U,E,B (k_region[]/the Region vroot's bucket order) -- only which single bit
     * represents a multi-region game changes, see entry 6 below move from the Europe group (old) to the
     * Brazil group (new) while pure-Europe entry 5 and pure-Brazil entry 7 are unaffected. */
    key(5, "EuroOnly", 1990, 0, 0x08);          /* E only */
    key(6, "BrazEuro", 1990, 0, 0x18);          /* E+B -> primary Brazil now (was Europe) */
    key(7, "BrazOnly", 1990, 0, 0x10);          /* B only */
    c.n = 8;
    for (i = 0; i < 8; i++) idx[i] = (uint16_t)i;
    om_sort_entries(&c, OM_SORT_REGION, idx, 8, tmp);   /* J:1  T:4  U:0  E:{3,5}  B:{6,7}  none:2 */
    CHECK_INT(idx[0], 1); CHECK_INT(idx[1], 4); CHECK_INT(idx[2], 0);
    CHECK_INT(idx[3], 3); CHECK_INT(idx[4], 5);                        /* Europe: "alpha" < "EuroOnly" */
    CHECK_INT(idx[5], 6); CHECK_INT(idx[6], 7);                        /* Brazil: "BrazEuro" < "BrazOnly" */
    CHECK_INT(idx[7], 2);
    c.n = 5;

    CHECK_INT(om_entry_passes(&c, 0, OM_FILTER_ANY, 0), 1);
    CHECK_INT(om_entry_passes(&c, 0, 1, 0), 1);
    CHECK_INT(om_entry_passes(&c, 0, 0, 0), 0);
    CHECK_INT(om_entry_passes(&c, 2, 0xFF, 0), 1);       /* the "Unknown" genre bucket */
    CHECK_INT(om_entry_passes(&c, 4, OM_FILTER_ANY, 0x04), 1);   /* T+U passes a U filter */
    CHECK_INT(om_entry_passes(&c, 4, OM_FILTER_ANY, 0x01), 0);
    CHECK_INT(om_entry_passes(&c, 3, 1, 0x0C), 1);       /* combinable: genre AND (U OR E) */
    CHECK_INT(om_entry_passes(&c, 0, 0, 0x0C), 0);
    CHECK_INT(om_title_cmp("abc", "ABD") < 0, 1);
    CHECK_INT(om_title_cmp("ABC", "abc") < 0, 1);        /* case-insensitive tie -> bytes */
    CHECK_INT(om_title_letter("daytona"), 'D');
    CHECK_INT(om_title_letter("3D Lemmings"), '#');
    CHECK_INT(om_title_letter(""), '#');
    CHECK_INT(om_libidx_open(&l, fl, nl), OM_OK);
    CHECK_INT(om_gamesdb_open(&db, fg, ng), OM_OK);
    om_sortctx_build(&c, &l, &db);
    CHECK_INT(c.n, 4);
    CHECK_STR(kname(0), "Daytona USA"); CHECK_INT(c.key[0].year, 1995);   /* its single-game folder's name */
    CHECK_INT(c.key[0].genre, 0); CHECK_INT(c.key[0].region, 0x0F);
    CHECK_STR(kname(1), "Grandia"); CHECK_INT(c.key[1].genre, 2); CHECK_INT(c.key[1].region, 0x01);
    CHECK_STR(kname(3), "unknown.iso"); CHECK_INT(c.key[3].genre, 0xFF);   /* its file name, not its override */ CHECK_INT(c.key[3].region, 0);
    CHECK_INT(c.ngenre, 4);           /* names: Racing, First Person Shooter, RPG, Fighting 3D */
    CHECK_INT(c.genre_by_rank[0], 3); CHECK_INT(c.genre_by_rank[1], 1);
    CHECK_INT(c.genre_by_rank[2], 0); CHECK_INT(c.genre_by_rank[3], 2);
    CHECK_INT(c.genre_rank[0xFF], 255);
    {   /* fix: a GAMES.DB hit whose own region is 0 falls back to the LIBRARY.IDX area bits, the same
         * fallback uiline.c's om_area_codes uses for the flag (`g->region ? g->region : e->area`) -- T-4507G
         * (Grandia, table index 2: table is sorted by key, "GS-7103" < "GS-9001" < "T-4507G" < "T-8113H") is
         * zeroed here and library entry 1 (Grandia disc 1) given area U, so the fixed key must show U, not
         * "no region" as entry 1 did above with the unmodified fixture (region 0x01, J, from GAMES.DB). */
        uint8_t *gm = th_dup(fg, ng), *lm = th_dup(fl, nl);
        om_gamesdb db2;
        om_libidx l2;
        gm[64 + 64 * 2 + 0x12] = 0;
        om_put32(gm + 0x24, om_crc32(0, gm + 64, 64 * 4));
        lm[64 + 32 * 1 + 0x12] = 0x04;
        om_put32(lm + 0x24, om_crc32(0, lm + 64, 32 * 4));
        CHECK_INT(om_gamesdb_open(&db2, gm, ng), OM_OK);
        CHECK_INT(om_libidx_open(&l2, lm, nl), OM_OK);
        om_sortctx_build(&c, &l2, &db2);
        CHECK_INT(c.key[1].region, 0x04);
        free(gm); free(lm);
    }
    om_sortctx_build(&c, &l, NULL);   /* no GAMES.DB: folder / file names and override titles, no genres */
    CHECK_STR(kname(0), "Daytona USA"); CHECK_INT(c.key[0].genre, 0xFF); CHECK_INT(c.key[0].region, 0x0F);
    CHECK_INT(c.ngenre, 0);
    {   /* 2026-11-23 user decision: A-Z sorts by the name the list shows (2026-11-27: the image's folder or file name
         * only -- a user title in GAMES.DB 0x35 bit0 or LIBRARY.IDX shows in the info line), never by GAMES.DB's title (Satiator Rings' model: a single-game
         * folder's game by the folder's name, a file by its exact name). Keys are GS-9001 "Daytona USA",
         * T-8113H "Alien Trilogy" and GS-7103 "Virtua Fighter 2"; by DB title the order would be 2, 1, 0. */
        om_lib_entry entbuf[4], e;
        uint16_t order[4];
        om_lib_folder fold[4];
        char pool[512];
        uint8_t out2[2048], *gm = th_dup(fg, ng);
        uint32_t outlen, rec;
        om_libw w;
        om_libidx l2;
        om_gamesdb db2;
        om_libw_init(&w, entbuf, 4, order, fold, 4, pool, sizeof pool);
        CHECK_INT(om_libw_folder(&w, "", 0xFFFF), 0);
        CHECK_INT(om_libw_folder(&w, "Games", 0), 1);
#define MKE(p, k) do { memset(&e, 0, sizeof e); e.path = (p); strcpy(e.key, (k)); e.folder = 1; e.disc = 1;     e.disc_total = 1; e.title = ""; e.image_type = 2; e.area = 0x04; CHECK_INT(om_libw_entry(&w, &e), OM_OK); } while (0)
        MKE("Games/Zeta Racing/zz_daytona.iso", "GS-9001");       /* "zz_daytona" */
        MKE("Games/alien.iso", "T-8113H");                        /* "alien" */
        MKE("Games/Echo/vf2.iso", "GS-7103");                     /* "vf2" */
#undef MKE
        CHECK_INT(om_libw_finish(&w, 1, 0, 20261123u, out2, sizeof out2, &outlen), OM_OK);
        CHECK_INT(om_libidx_open(&l2, out2, outlen), OM_OK);
        om_libidx_entry(&l2, 0, &e);
        CHECK_STR(e.path, "Games/Echo/vf2.iso");                  /* the writer sorts by path: re-derive the order */
        om_sortctx_build(&c, &l2, &db);
        CHECK_STR(kname(0), "Echo"); CHECK_STR(kname(1), "Zeta Racing"); CHECK_STR(kname(2), "alien.iso");
        for (i = 0; i < 3; i++) idx[i] = (uint16_t)i;
        om_sort_entries(&c, OM_SORT_AZ, idx, 3, tmp);             /* alien.iso, Echo, Zeta Racing */
        CHECK_INT(idx[0], 2); CHECK_INT(idx[1], 0); CHECK_INT(idx[2], 1);
        CHECK_INT(om_title_letter(c.key[1].title), 'Z');          /* the A-Z view's letter: Z, not D */
        CHECK_INT(om_title_letter(c.key[2].title), 'A');
        CHECK_INT(c.key[1].region, 0x0F);                         /* GAMES.DB's JTUE for GS-9001, as before */
        CHECK_INT(om_entry_passes(&c, 1, OM_FILTER_ANY, 0x04), 1);
        CHECK_INT(om_gamesdb_rec(&db, "GS-9001", &rec), 1);       /* GS-9001 carries a user title: "Daytona USA" */
        gm[64 + 64 * rec + 0x35] = 0x01;
        om_put32(gm + 0x24, om_crc32(0, gm + 64, 64 * db.count));
        CHECK_INT(om_gamesdb_open(&db2, gm, ng), OM_OK);
        om_sortctx_build(&c, &l2, &db2);
        CHECK_STR(kname(1), "Zeta Racing");                       /* 2026-11-27: a user title names no row either */
        for (i = 0; i < 3; i++) idx[i] = (uint16_t)i;
        om_sort_entries(&c, OM_SORT_AZ, idx, 3, tmp);             /* alien.iso, Echo, Zeta Racing, as before */
        CHECK_INT(idx[0], 2); CHECK_INT(idx[1], 0); CHECK_INT(idx[2], 1);
        om_sortctx_build(&c, &l2, NULL);                          /* no GAMES.DB at all: still the shown names */
        CHECK_STR(kname(1), "Zeta Racing");
        free(gm);
    }
    free(fl); free(fg);
    return th_done("test_libsort");
}
