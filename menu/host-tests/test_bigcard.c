/* menu/host-tests/test_bigcard.c -- a large card through the REAL loader (card.c, satfs.c, sd.c, scan.c) over the fake
 * libsatiator. Hardware report 2026-11-23 (build c4aa847): a 699-name card root froze "LOADING YOUR LIBRARY" for over two
 * minutes. The root listing kept 482 of 698 names (OM_SCAN_NAMEBUF, 16 KB), so the root fingerprint never matched the
 * PC's LIBRARY.IDX and the console started a full card scan that draws no frames. BSD-3-Clause. */
#include "th.h"
#include "card.h"
#include "fakefs.h"
#include "fakesat.h"
#include "satfs.h"
#include "satiator.h"
#include "sd.h"
#include "tree.h"

#define MAXN 5200u
static uint8_t *blob, *data;
static uint32_t nfile, dlen;
static uint8_t gdb[300000], art[140000], lib[220000], vrt[16384];
static om_scan_work work;
static om_tree tree;
static char names[MAXN][64];
static const char *sorted[MAXN];
static om_lib_entry ent[MAXN];
static uint16_t order[MAXN];
static om_lib_folder fold[400];
static char pool[400000];
static uint8_t idx[400000];

static void add(const char *path, const uint8_t *d, uint32_t n)
{
    uint8_t *row = blob + 16 + 264 * nfile++;
    memset(row, 0, 264);
    strcpy((char *)row, path);
    om_put32(row + 256, dlen);
    om_put32(row + 260, n);
    memcpy(data + dlen, d, n);
    dlen += (n + 3u) & ~3u;
}

static uint32_t finish(void)
{
    uint32_t i, table_end = 16 + 264 * nfile;
    memcpy(blob, "OMFAKEFS", 8);
    om_put32(blob + 8, nfile);
    om_put32(blob + 12, 0);
    for (i = 0; i < nfile; i++) {
        uint8_t *row = blob + 16 + 264 * i;
        om_put32(row + 256, om_be32(row + 256) + table_end);
    }
    memmove(blob + table_end, data, dlen);
    return table_end + dlen;
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

/* An independent reference for SPEC v1 clarification 6 (tools/omsat/libidx.py card_fingerprint): qsort, then CRC32 of
 * the names joined by "\n". Not om_fingerprint, which is under test. */
static uint32_t ref_fp(uint32_t n)
{
    uint32_t i, crc = 0;
    for (i = 0; i < n; i++) sorted[i] = names[i];
    qsort(sorted, n, sizeof sorted[0], cmp_str);
    for (i = 0; i < n; i++) {
        if (i) crc = om_crc32(crc, "\n", 1);
        crc = om_crc32(crc, sorted[i], (uint32_t)strlen(sorted[i]));
    }
    return crc;
}

static uint32_t s_prog_calls;
static uint32_t s_prog_last_img;
static void prog(void *ctx, uint32_t dirs, uint32_t images) { (void)ctx; (void)dirs; s_prog_calls++; s_prog_last_img = images; }
static void st(void *ctx, const char *line) { (void)ctx; (void)line; }

/* n root game folders "Synthetic Game Title NNNN (USA)" (31 B: this card's root averaged 34.8 B a name, measured on the
 * G:\SATIATOR BACKUP mirror, 24,258 B over 698 names), one game.iso each, and a LIBRARY.IDX listing them with
 * fingerprint fp_delta + the true one. Returns the LIBRARY.IDX size. */
static uint32_t make_card(uint32_t n, uint32_t fp_delta, uint32_t *len, uint32_t *true_fp)
{
    static const uint8_t none[1];
    uint32_t i, ilen = 0, gn;
    om_libw w;
    char path[128];
    uint8_t *g = th_load(FIX("tiny.games.db"), &gn);
    nfile = dlen = 0;
    for (i = 0; i < n; i++) snprintf(names[i], sizeof names[i], "Synthetic Game Title %04u (USA)", (unsigned)i);
    *true_fp = ref_fp(n);
    om_libw_init(&w, ent, MAXN, order, fold, 400, pool, sizeof pool);
    CHECK_INT(om_libw_folder(&w, "", 0xFFFF), 0);
    for (i = 0; i < n; i++) {
        om_lib_entry e;
        memset(&e, 0, sizeof e);
        snprintf(path, sizeof path, "%s/game.iso", names[i]);
        e.path = path; e.title = ""; e.folder = 0; e.image_type = 2; e.disc = e.disc_total = 1;
        CHECK_INT(om_libw_entry(&w, &e), OM_OK);
    }
    CHECK_INT(om_libw_finish(&w, 1, *true_fp + fp_delta, 0, idx, sizeof idx, &ilen), OM_OK);
    add("aether/GAMES.DB", g, gn);
    add("aether/LIBRARY.IDX", idx, ilen);
    for (i = 0; i < n; i++) {
        snprintf(path, sizeof path, "%s/game.iso", names[i]);
        add(path, none, 0);
    }
    *len = finish();
    free(g);
    return ilen;
}

static int load(uint32_t n, uint32_t fp_delta, om_card *c, uint32_t lib_cap)
{
    uint32_t len, fp;
    om_card_bufs b = { gdb, sizeof gdb, art, sizeof art, lib, lib_cap, &work, vrt, sizeof vrt };
    make_card(n, fp_delta, &len, &fp);
    om_fakesat_mount(blob, len);
    CHECK_INT(s_mode(s_api), 0);
    return om_card_load(c, &b, st, NULL);
}

int main(void)
{
    om_card c;
    uint32_t len, fp, ilen;
    blob = malloc(1u << 20);
    data = malloc(1u << 20);

    /* Python's tools/omsat/libidx.py card_fingerprint over the 2,048 synthetic names (computed this session):
     * pins the reference to the PC side, so the card loads below compare against what cardbuild writes. */
    make_card(2048, 0, &len, &fp);
    CHECK_INT(fp, 0x6A925CCBu);

    /* 1. the reported card's shape: ~700 root game folders, an up-to-date LIBRARY.IDX -> no scan */
    CHECK_INT(load(700, 0, &c, sizeof lib), OM_OK);
    CHECK_INT(c.lib_source, OM_LIB_FROM_IDX);
    CHECK_INT(c.lib.count, 700);
    CHECK_INT(c.scan.entries, 0);

    /* 2. the brief's floor: 2,048 root folders (OM_MAX_ENTRIES) */
    CHECK_INT(load(2048, 0, &c, sizeof lib), OM_OK);
    CHECK_INT(c.lib_source, OM_LIB_FROM_IDX);
    CHECK_INT(c.lib.count, 2048);

    /* 3. mutation: the same 700-folder card with a stale LIBRARY.IDX. The console cannot hold this root for a scan, so
     * it must never produce a silently partial library: either a complete scan or the stale index kept, named. */
    CHECK_INT(load(700, 1, &c, sizeof lib), OM_OK);
    CHECK_INT(c.lib_source, OM_LIB_IDX_KEPT);
    CHECK_INT(c.scan.truncated, 0);
    CHECK_INT(c.lib.count, 700);
    {
        char sum[3][41];
        om_card_summary(&c, sum);
        CHECK_STR(sum[2], "STALE LIBRARY.IDX: REBUILD IT ON THE PC");
    }

    /* 4. a small stale card still rescans in full (the existing fallback, spec §5.6) */
    CHECK_INT(load(40, 1, &c, sizeof lib), OM_OK);
    CHECK_INT(c.lib_source, OM_LIB_SCAN_STALE);
    CHECK_INT(c.lib.count, 40);
    CHECK_INT(c.scan.truncated, 0);
    /* ... and reports progress after every folder it reads, so the Saturn can draw frames (main.c) */
    om_satfs_set_progress(prog);
    CHECK_INT(load(40, 1, &c, sizeof lib), OM_OK);
    CHECK(s_prog_calls >= 41);                               /* 40 game folders + the root */
    CHECK_INT(s_prog_last_img, 40);
    om_satfs_set_progress(NULL);

    /* 5. a LIBRARY.IDX larger than the menu's buffer is a named failure at once, never a scan that cannot fit either */
    ilen = make_card(40, 0, &len, &fp);
    CHECK_INT(load(40, 0, &c, ilen), OM_E_FULL);
    CHECK_INT(c.idx_rc, OM_E_FULL);
    CHECK_INT(c.scan.entries, 0);

    /* 6. the root fingerprint accumulator alone: 5,000 names in the card-scan work area equal the reference; a 2,000-name
     * arena is OM_E_FULL (mutation: the overflow is reported, never a fingerprint over part of the root) */
    {
        om_fpacc a;
        uint32_t i, r = OM_OK;
        for (i = 0; i < 5000; i++) snprintf(names[i], sizeof names[i], "Synthetic Game Title %04u (USA)", (unsigned)(4999 - i));
        om_fpacc_init(&a, &work, (uint32_t)sizeof work);
        CHECK(a.max >= 5000);
        for (i = 0; i < 5000; i++) r |= (uint32_t)om_fpacc_add(&a, names[i]);
        CHECK_INT(r, OM_OK);
        CHECK_INT(om_fpacc_add(&a, "vibemenu"), OM_OK);          /* hidden at the root: skipped, not stored */
        CHECK_INT(a.n, 5000);
        CHECK_INT(om_fpacc_finish(&a), ref_fp(5000));
        om_fpacc_init(&a, &work, 2000u * 48u);
        for (i = 0, r = OM_OK; i < 5000 && r == OM_OK; i++) r = (uint32_t)om_fpacc_add(&a, names[i]);
        CHECK_INT((int)r, OM_E_FULL);
        CHECK(a.overflow);
    }

    /* 7. the tree at the brief's scale: 300 folders (299 under the root) and 2,048 entries spread over them */
    {
        om_libw w;
        om_libidx l;
        uint32_t i, ilen2 = 0;
        static char fpath[300][16], epath[2048][48];
        uint16_t n;
        om_libw_init(&w, ent, MAXN, order, fold, 400, pool, sizeof pool);
        CHECK_INT(om_libw_folder(&w, "", 0xFFFF), 0);
        for (i = 1; i < 300; i++) {
            snprintf(fpath[i], sizeof fpath[i], "F%03u", (unsigned)i);
            CHECK(om_libw_folder(&w, fpath[i], 0) >= 0);
        }
        for (i = 0; i < 2048; i++) {
            om_lib_entry e;
            memset(&e, 0, sizeof e);
            snprintf(epath[i], sizeof epath[i], "%s/G%04u/game.iso", i % 300 ? fpath[i % 300] : "R", (unsigned)i);
            e.path = epath[i]; e.title = ""; e.folder = (uint16_t)(i % 300); e.image_type = 2; e.disc = e.disc_total = 1;
            CHECK_INT(om_libw_entry(&w, &e), OM_OK);
        }
        CHECK_INT(om_libw_finish(&w, 0, 0, 0, idx, sizeof idx, &ilen2), OM_OK);
        CHECK_INT(om_libidx_open(&l, idx, ilen2), OM_OK);
        CHECK_INT(l.count, 2048); CHECK_INT(l.nfolders, 300);
        CHECK_INT(om_tree_build(&tree, &l), OM_OK);
        om_tree_items(&tree, &n);
        CHECK_INT(n, 299 + 7);                                      /* 299 subfolders + entries 0, 300, ..., 1800 */
        tree.cur = 1;
        om_tree_items(&tree, &n);
        CHECK_INT(n, 7);                                            /* 1, 301, ..., 1801 */
    }
    free(blob); free(data);
    return th_done("test_bigcard");
}
