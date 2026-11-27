#include "th.h"
#include "scan.h"
#include "cases.inc"

static int drop_box;   /* mutation switch: hide BOX.TGA from one listing */
static const char *fail_path;      /* hostile test: make one subfolder's listing fail mid-scan */

static int f_list(void *ctx, const char *path, om_listing *l)
{
    uint32_t i, k;
    (void)ctx;
    om_listing_reset(l);
    for (i = 0; i < ST_NDIRS; i++)
        if (!strcmp(ST_DIRS[i].path, path)) {
            for (k = 0; k < ST_DIRS[i].nd; k++) om_listing_add(l, ST_DIRS[i].dirs[k], 1);
            for (k = 0; k < ST_DIRS[i].nf; k++)
                if (!(drop_box && !strcmp(ST_DIRS[i].files[k], "BOX.TGA"))) om_listing_add(l, ST_DIRS[i].files[k], 0);
            return 0;
        }
    return -1;
}

/* 2026-11-23 (clarification 7, cue dedupe): the fixture's FILE names, NUL-separated, as om_satfs_cue_files gives them */
static int no_cues;    /* mutation switch: a filesystem that reports no FILE names */
static int f_cue_files(void *ctx, const char *path, char *out, uint32_t cap)
{
    uint32_t i, k, o = 0;
    (void)ctx;
    if (no_cues) return 0;
    for (i = 0; i < ST_NCUES; i++)
        if (!strcmp(ST_CUES[i].path, path)) {
            for (k = 0; k < ST_CUES[i].n; k++) {
                uint32_t n = (uint32_t)strlen(ST_CUES[i].names[k]) + 1;
                if (o + n > cap) break;
                memcpy(out + o, ST_CUES[i].names[k], n);
                o += n;
            }
            return (int)k;
        }
    return 0;
}

/* the user's LARRY 6 (G:\SATIATOR BACKUP\!_LEISURE SUIT LARRY): game.cue + GAME.iso (the cue's own data) + OLD\ */
static int f_list_larry(void *ctx, const char *path, om_listing *l)
{
    (void)ctx;
    om_listing_reset(l);
    if (!strcmp(path, "")) { om_listing_add(l, "LARRY 6", 1); om_listing_add(l, "Two", 1); return 0; }
    if (!strcmp(path, "LARRY 6")) {
        om_listing_add(l, "OLD", 1); om_listing_add(l, "game.cue", 0); om_listing_add(l, "GAME.iso", 0);
        om_listing_add(l, "BOX.TGA", 0); return 0;
    }
    if (!strcmp(path, "LARRY 6/OLD")) { om_listing_add(l, "lsl6.cue", 0); om_listing_add(l, "lsl6.bin", 0); return 0; }
    if (!strcmp(path, "Two")) { om_listing_add(l, "x.cue", 0); om_listing_add(l, "x.iso", 0); om_listing_add(l, "X.Iso", 0); return 0; }
    return -1;
}

static int f_cue_larry(void *ctx, const char *path, char *out, uint32_t cap)
{
    static const char g[] = "game.iso", x[] = "x.iso\0track2.bin";
    (void)ctx;
    if (no_cues || cap < sizeof x) return 0;
    if (!strcmp(path, "LARRY 6/game.cue")) { memcpy(out, g, sizeof g); return 1; }
    if (!strcmp(path, "Two/x.cue")) { memcpy(out, x, sizeof x); return 2; }
    return 0;
}

static int f_list_fail_mid(void *ctx, const char *path, om_listing *l)
{
    if (fail_path && !strcmp(path, fail_path)) return OM_E_FIELD;
    return f_list(ctx, path, l);
}

static void put_field(uint8_t *b, int off, int n, const char *s)
{
    int i;
    for (i = 0; i < n && s[i]; i++) b[off + i] = (uint8_t)s[i];
}

static int f_sysid(void *ctx, const char *path, om_sysid *s)
{
    uint32_t i;
    uint8_t b[256];
    (void)ctx;
    for (i = 0; i < ST_NSYS; i++)
        if (!strcmp(ST_SYS[i].path, path)) {                 /* same bytes as omsat.iphdr.make_system_id */
            memset(b, ' ', 0xD0); memset(b + 0xD0, 0, 0x30);
            put_field(b, 0, 16, "SEGA SEGASATURN ");
            put_field(b, 0x20, 10, ST_SYS[i].product); put_field(b, 0x38, 8, ST_SYS[i].device);
            put_field(b, 0x40, 10, ST_SYS[i].area); put_field(b, 0x60, 112, ST_SYS[i].title);
            return om_sysid_parse(b, 256, s);
        }
    return -1;
}

/* --- hostile fixtures, independent of the ST_DIRS golden tree --- */

static int f_sysid_never(void *ctx, const char *path, om_sysid *s) { (void)ctx; (void)path; (void)s; return -1; }

static char longname[300];     /* a single name far over the spec §2 255-byte path limit */

static int f_list_long_file(void *ctx, const char *path, om_listing *l)
{
    (void)ctx;
    om_listing_reset(l);
    if (!strcmp(path, "")) { om_listing_add(l, "A.iso", 0); om_listing_add(l, longname, 0); return 0; }
    return -1;
}

static int f_list_long_dir(void *ctx, const char *path, om_listing *l)
{
    (void)ctx;
    om_listing_reset(l);
    if (!strcmp(path, "")) { om_listing_add(l, longname, 1); return 0; }
    return -1;
}

/* A linear chain 9 folders deep (root + L1..L8). Per the established fact "game-dir classification
 * happens before the depth/folder-cap gates, matching the Python scanner" (and tools/omsat/cardscan.py's
 * own scan(): it calls src.listdir(sub) on every candidate subdirectory, including one past the depth cap,
 * purely to classify it), L9's own directory IS listed once -- what must never happen is L9 being entered
 * as a folder row (added to the folder table / recursed into), which is what CPATH stops one entry short
 * of: L9 has an empty listing (CPATH[9]), so this also proves L9's own children, if it had any, are never
 * discovered. */
static const char *const CPATH[10] = {
    "", "L1", "L1/L2", "L1/L2/L3", "L1/L2/L3/L4", "L1/L2/L3/L4/L5", "L1/L2/L3/L4/L5/L6",
    "L1/L2/L3/L4/L5/L6/L7", "L1/L2/L3/L4/L5/L6/L7/L8", "L1/L2/L3/L4/L5/L6/L7/L8/L9"
};
static const char *const CNAME[9] = { "L1", "L2", "L3", "L4", "L5", "L6", "L7", "L8", "L9" };

static int f_list_chain(void *ctx, const char *path, om_listing *l)
{
    uint32_t i;
    (void)ctx;
    om_listing_reset(l);
    for (i = 0; i < 10; i++)
        if (!strcmp(CPATH[i], path)) { if (i < 9) om_listing_add(l, CNAME[i], 1); return 0; }
    return -1;              /* any path outside this chain is a test-construction error */
}

static int f_list_fail_root(void *ctx, const char *path, om_listing *l) { (void)ctx; (void)path; (void)l; return OM_E_FIELD; }

static om_scan_work w;
static uint8_t out[65536];

int main(void)
{
    uint32_t n, gn, olen;
    uint8_t *gold = th_load(FIX("scan_tree.library.idx"), &n), *g = th_load(FIX("tiny.games.db"), &gn);
    om_gamesdb db;
    om_scan_fs fs = { NULL, f_list, f_sysid, NULL, f_cue_files };
    om_scan_report rep;
    CHECK_INT(om_gamesdb_open(&db, g, gn), OM_OK);
    CHECK_INT(om_scan(&fs, &db, &w, ST_BUILD_DATE, out, sizeof out, &olen, &rep), OM_OK);
    CHECK_INT(olen, n);
    CHECK(olen == n && memcmp(out, gold, n) == 0);           /* byte-identical to tools/omsat/cardscan.py */
    /* contract/fixtures/scan_tree.json's SCAN_TREE has a depth-9 chain under "Deep" (measured against the
     * committed golden: header count=9, nfolders=13 via tools/omsat.binfmt.unpack_header, and independently
     * reproduced by running tools/omsat/cardscan.py's scan() directly on scan_tree.json this session). Deep,
     * L2..L8 (depth 1..8) are entered as folder rows (8 folders); L2/.../L9 (depth 9) is not,
     * which is exactly the 1 folder this report must count as skipped. 2026-11-23 (Satiator Rings' model): RPG/Grandia
     * (two images) is a folder row too: 14. */
    CHECK_INT(rep.entries, 9); CHECK_INT(rep.matched, 4); CHECK_INT(rep.folders, 14);
    CHECK_INT(rep.unreadable, 0); CHECK_INT(rep.list_errors, 0); CHECK_INT(rep.skipped_folders, 1);
    printf("test_scan: %u entries (%u matched) in %u folders (%u skipped)\n",
           (unsigned)rep.entries, (unsigned)rep.matched, (unsigned)rep.folders, (unsigned)rep.skipped_folders);
    no_cues = 1;                                             /* mutation: without the cue's FILE names, Daytona.ISO */
    CHECK_INT(om_scan(&fs, &db, &w, ST_BUILD_DATE, out, sizeof out, &olen, &rep), OM_OK);   /* is a 2nd image */
    CHECK(olen != n || memcmp(out, gold, n) != 0);
    no_cues = 0;
    {   /* LARRY 6: one entry for game.cue (GAME.iso is its data), OLD a game folder; Two: x.cue + its x.iso and X.Iso
         * (any case) is one image, a game folder of one */
        om_scan_fs lf = { NULL, f_list_larry, f_sysid_never, NULL, f_cue_larry };
        om_libidx li;
        om_lib_entry e;
        CHECK_INT(om_scan(&lf, NULL, &w, ST_BUILD_DATE, out, sizeof out, &olen, &rep), OM_OK);
        CHECK_INT(rep.entries, 3); CHECK_INT(rep.folders, 2);
        CHECK_INT(om_libidx_open(&li, out, olen), OM_OK);
        om_libidx_entry(&li, 0, &e); CHECK_STR(e.path, "Two/x.cue"); CHECK_INT(e.folder, 0);
        om_libidx_entry(&li, 1, &e); CHECK_STR(e.path, "LARRY 6/OLD/lsl6.cue"); CHECK_INT(e.folder, 1);
        om_libidx_entry(&li, 2, &e); CHECK_STR(e.path, "LARRY 6/game.cue"); CHECK_INT(e.folder, 1);
        no_cues = 1;                                         /* mutation: the old rule lists GAME.iso, x.iso, X.Iso */
        CHECK_INT(om_scan(&lf, NULL, &w, ST_BUILD_DATE, out, sizeof out, &olen, &rep), OM_OK);
        CHECK_INT(rep.entries, 6);
        no_cues = 0;
    }
    drop_box = 1;                                            /* mutation: the box flag must reach the output */
    CHECK_INT(om_scan(&fs, &db, &w, ST_BUILD_DATE, out, sizeof out, &olen, &rep), OM_OK);
    CHECK(memcmp(out, gold, n) != 0);
    drop_box = 0;
    CHECK_INT(om_scan(&fs, &db, &w, ST_BUILD_DATE, out, 100, &olen, &rep), OM_E_FULL);
    CHECK_INT(om_scan(&fs, NULL, &w, ST_BUILD_DATE, out, sizeof out, &olen, &rep), OM_OK);
    CHECK_INT(rep.matched, 0);                               /* no GAMES.DB: still lists (spec §5.6) */

    /* hostile: a name over 255 bytes (spec §2) must be skipped and counted, never read out of bounds */
    memset(longname, 'A', sizeof longname - 5);
    memcpy(longname + sizeof longname - 5, ".iso", 5);
    {
        om_scan_fs lf = { NULL, f_list_long_file, f_sysid_never, NULL, NULL };
        CHECK_INT(om_scan(&lf, NULL, &w, ST_BUILD_DATE, out, sizeof out, &olen, &rep), OM_OK);
        CHECK_INT(rep.entries, 1);                            /* only "A.iso" survives */
        CHECK(rep.truncated >= 1);
    }
    {
        om_scan_fs lf = { NULL, f_list_long_dir, f_sysid_never, NULL, NULL };
        CHECK_INT(om_scan(&lf, NULL, &w, ST_BUILD_DATE, out, sizeof out, &olen, &rep), OM_OK);
        CHECK_INT(rep.folders, 1);                            /* root only: the over-long subdir is skipped */
        CHECK(rep.truncated >= 1);
    }

    /* hostile: a deep chain, isolated from the golden fixture's own depth-9 case */
    {
        om_scan_fs cf = { NULL, f_list_chain, f_sysid_never, NULL, NULL };
        CHECK_INT(om_scan(&cf, NULL, &w, ST_BUILD_DATE, out, sizeof out, &olen, &rep), OM_OK);
        CHECK_INT(rep.folders, 9);                            /* root + L1..L8 (depth 1..8) */
        CHECK_INT(rep.skipped_folders, 1);                    /* L9 (depth 9) is never entered */
        CHECK_INT(rep.entries, 0); CHECK_INT(rep.list_errors, 0);
    }

    /* hostile: a callback that fails on the very first (root) call -- the error code must propagate */
    {
        om_scan_fs rf = { NULL, f_list_fail_root, f_sysid_never, NULL, NULL };
        CHECK_INT(om_scan(&rf, NULL, &w, ST_BUILD_DATE, out, sizeof out, &olen, &rep), OM_E_FIELD);
    }

    /* hostile: a callback that fails mid-scan, on a non-root folder -- the scan must not abort, the failed
     * folder is counted in list_errors, and the rest of the card is still produced as a valid, well-formed
     * image (unlike the root case above, which has nothing to fall back on and must propagate). */
    {
        om_scan_fs mf = { NULL, f_list_fail_mid, f_sysid, NULL, NULL };
        fail_path = "Racing";
        CHECK_INT(om_scan(&mf, &db, &w, ST_BUILD_DATE, out, sizeof out, &olen, &rep), OM_OK);
        CHECK_INT(rep.list_errors, 1);
        CHECK(olen > 0 && olen <= sizeof out);
        fail_path = NULL;
    }

    free(gold); free(g);
    return th_done("test_scan");
}
