#include "th.h"
#include "title.h"

/* 2026-11-23, Satiator Rings' model: a game in a single-game folder (one image, no subfolder: an entry of its parent
 * folder row) is named by that folder; any other game by its exact file name, extension included; a title the user
 * set (LIBRARY.IDX override, or GAMES.DB 0x35 bit0 from Card Manager's USEREDIT.INI) wins. GAMES.DB's own title never
 * names a game (spec §5.6). */
static char t[64];

static const char *name_of(const om_libidx *l, const char *path, uint16_t folder, uint8_t disc, uint8_t total,
                           const om_game *g)
{
    om_lib_entry e;
    memset(&e, 0, sizeof e);
    e.path = path; e.folder = folder; e.disc = disc; e.disc_total = total; e.title = "";
    om_title(l, &e, g, t, sizeof t);
    return t;
}

int main(void)
{
    uint32_t n, nd, len;
    uint8_t *li = th_load(FIX("tiny.library.idx"), &n), *gd = th_load(FIX("tiny.games.db"), &nd), *gu;
    om_libidx l;
    om_gamesdb db, dbu;
    om_lib_entry e;
    om_game g, gf;
    om_sysid ip;
    const char *s;
    char d[3][23], rl[6];
    uint32_t rec;
    CHECK_INT(om_libidx_open(&l, li, n), OM_OK);
    CHECK_INT(om_gamesdb_open(&db, gd, nd), OM_OK);
    om_libidx_entry(&l, 0, &e);                          /* Racing/Daytona USA/daytona.cue, folder 1 "Racing" */
    CHECK_INT(om_gamesdb_find(&db, e.key, &g), 1);
    CHECK_INT(g.user, 0);
    om_title(&l, &e, &g, t, sizeof t);  CHECK_STR(t, "Daytona USA");        /* its single-game folder's name */
    om_details(&e, &g, om_gamesdb_genre(&db, g.genre), NULL, d);
    CHECK_STR(d[0], "Racing"); CHECK_STR(d[1], "Region JTUE  1P"); CHECK_STR(d[2], "Disc 1/1  1995-04-01");

    /* a single-game folder named differently from GAMES.DB's title: the folder's name */
    CHECK_STR(name_of(&l, "Racing/DAYTONA (USA) (RE)/daytona.cue", 1, 1, 1, &g), "DAYTONA (USA) (RE)");
    CHECK_STR(name_of(&l, "LARRY 1/Leisure Suit Larry 1 Saturn.cue", 0, 1, 1, NULL), "LARRY 1");   /* at the root */
    /* a file in a folder row (beside other games or a subfolder): its exact file name, extension included */
    CHECK_STR(name_of(&l, "Racing/Sega Rally Championship (Europe).cue", 1, 1, 1, &g), "Sega Rally Championship (Europe).cue");
    CHECK_STR(name_of(&l, "game.cue", 0, 1, 1, &g), "game.cue");
    CHECK_STR(name_of(NULL, "x/y/daytona.cue", 0, 1, 1, NULL), "daytona.cue");   /* no folder table: the file name */
    /* exact names: no disc indication added to a file or folder name */
    CHECK_STR(name_of(&l, "RPG/grandia_d1.iso", 2, 1, 2, &g), "grandia_d1.iso");

    /* GAMES.DB 0x35 bit0 (the user's USEREDIT.INI title): title_en is shown, as the user typed it */
    gu = th_dup(gd, nd);
    CHECK_INT(om_gamesdb_rec(&db, "GS-9001", &rec), 1);
    gu[64 + 64 * rec + 0x35] = 0x01;
    om_put32(gu + 0x24, om_crc32(0, gu + 64, 64 * db.count));
    CHECK_INT(om_gamesdb_open(&dbu, gu, nd), OM_OK);
    CHECK_INT(om_gamesdb_find(&dbu, "GS-9001", &gf), 1);
    CHECK_INT(gf.user, OM_GU_TITLE);
    CHECK_STR(name_of(&l, "Racing/DAYTONA (USA) (RE)/daytona.cue", 1, 1, 1, &gf), "Daytona USA");
    s = om_entry_name(&l, &e, &gf, &len);
    CHECK_INT(len, 11); CHECK(s && !strncmp(s, "Daytona USA", 11) && !s[11]);
    s = om_entry_name(&l, &e, &g, &len);                 /* a folder name: a span of the path, ending at its '/' */
    CHECK_INT(len, 11); CHECK(s == e.path + 7 && s[len] == '/');
    s = om_entry_name(&l, &e, NULL, &len);
    CHECK_INT(len, 11);
    /* a user title on a multi-disc game keeps its disc indication (the discs share it) */
    CHECK_STR(name_of(&l, "RPG/x/disc2.cue", 2, 2, 2, &gf), "Daytona USA (Disc 2)");

    om_libidx_entry(&l, 3, &e);
    om_title(&l, &e, NULL, t, sizeof t);  CHECK_STR(t, "MY HOMEBREW");       /* LIBRARY.IDX override wins */
    om_title(&l, &e, &gf, t, sizeof t);  CHECK_STR(t, "MY HOMEBREW");        /* even over a GAMES.DB user title */
    /* 2026-11-27 (the user's rule: games are listed by their exact file name): the list's rows and the A-Z order use
     * om_entry_raw / om_title_raw -- no override, no user title -- and only the info line keeps om_title */
    s = om_entry_raw(&l, &e, &len);
    CHECK(strncmp(s, "MY HOMEBREW", len) != 0);
    om_title_raw(&l, &e, t, sizeof t);
    printf("raw name of the override entry: %s (path %s)\n", t, e.path);
    CHECK(strcmp(t, "MY HOMEBREW") != 0 && strstr(e.path, t) != NULL);
    {
        om_lib_entry r;
        om_libidx_entry(&l, 0, &r);                      /* a single-game folder with a GAMES.DB user title */
        om_title_raw(&l, &r, t, sizeof t);  CHECK_STR(t, "Daytona USA"); /* the folder's name, not the user title */
        CHECK_STR(name_of(&l, "Racing/DAYTONA (USA) (RE)/daytona.cue", 1, 1, 1, &gf), "Daytona USA");   /* om_title */
        r.path = "Racing/DAYTONA (USA) (RE)/daytona.cue"; r.folder = 1;
        om_title_raw(&l, &r, t, sizeof t);  CHECK_STR(t, "DAYTONA (USA) (RE)");
    }
    om_details(&e, NULL, NULL, NULL, d);
    CHECK_STR(d[0], "Unknown genre"); CHECK_STR(d[1], "Region ?"); CHECK_STR(d[2], "Disc 1/1");
    memset(&ip, 0, sizeof ip);
    strcpy(ip.date, "19961001"); ip.area_flags = 1;
    e.flags = 0; e.title = ""; e.area = 0; e.path = "Caf\xc3\xa9.iso";
    om_title(&l, &e, NULL, t, sizeof t);  CHECK_STR(t, "Caf??.iso");         /* ASCII-only font (spec §5.3) */
    om_details(&e, NULL, NULL, &ip, d);
    CHECK_STR(d[1], "Region J"); CHECK_STR(d[2], "Disc 1/1  1996-10-01");
    e.path = "unknown.iso";
    om_title(&l, &e, NULL, t, 6);  CHECK_STR(t, "unkno");                    /* truncation honours cap */
    om_region_letters(31, rl); CHECK_STR(rl, "JTUEB");
    free(li); free(gd); free(gu);
    return th_done("test_title");
}
