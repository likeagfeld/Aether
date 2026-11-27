#include "th.h"
#include "libidx.h"
#include "cases.inc"

static om_lib_entry ent[64];
static uint16_t order[64];
static om_lib_folder fold[16];
static char pool[4096];
static uint8_t out[8192];

int main(void)
{
    uint32_t n, i, olen;
    uint8_t *f = th_load(FIX("tiny.library.idx"), &n), *m;
    om_libidx l;
    om_lib_entry e;
    om_lib_folder fo;
    om_libw w;
    uint16_t scratch[16];
    uint32_t pool_off;
    CHECK_INT(om_libidx_open(&l, f, n), OM_OK);
    CHECK_INT(l.count, 4);
    CHECK_INT(l.nfolders, 3);
    CHECK_INT(l.fingerprint, 0xa4c2dc0eu);
    om_libidx_entry(&l, 0, &e);
    CHECK_STR(e.path, "Racing/Daytona USA/daytona.cue");
    CHECK_STR(e.key, "GS-9001");
    CHECK_INT(e.flags & OM_LIB_BOX, OM_LIB_BOX); CHECK_INT(e.folder, 1); CHECK_INT(e.area, 15); CHECK_INT(e.image_type, 1);
    om_libidx_entry(&l, 2, &e);
    CHECK_INT(e.disc, 2); CHECK_INT(e.disc_total, 2);
    CHECK_INT(om_libidx_games(&l), 3);                         /* disc 2 of 2 is not another game */
    om_libidx_entry(&l, 3, &e);
    CHECK_STR(e.title, "MY HOMEBREW"); CHECK_STR(e.key, ""); CHECK_INT(e.image_type, 2);
    om_libidx_folder(&l, 0, &fo);
    CHECK_STR(fo.path, ""); CHECK_INT(fo.parent, 0xFFFF);
    om_libidx_folder(&l, 2, &fo);
    CHECK_STR(fo.path, "RPG"); CHECK_INT(fo.parent, 0);
    /* the writer reproduces the Python fixture byte for byte (unsorted: fixture order) */
    om_libw_init(&w, ent, 64, order, fold, 16, pool, sizeof pool);
    for (i = 0; i < l.nfolders; i++) { om_libidx_folder(&l, i, &fo); CHECK_INT(om_libw_folder(&w, fo.path, fo.parent), (int)i); }
    for (i = 0; i < l.count; i++) { om_libidx_entry(&l, i, &e); CHECK_INT(om_libw_entry(&w, &e), OM_OK); }
    CHECK_INT(om_libw_finish(&w, 0, l.fingerprint, 20261117u, out, sizeof out, &olen), OM_OK);
    CHECK_INT(olen, n);
    CHECK(memcmp(out, f, n) == 0);
    CHECK_INT(om_libw_finish(&w, 0, l.fingerprint, 20261117u, out, n - 1, &olen), OM_E_FULL);
    /* fingerprint vectors: contract/fixtures/fingerprint_cases.json (= Python card_fingerprint) */
    for (i = 0; i < FPC_N; i++)
        CHECK_INT(om_fingerprint(FPC[i].names, FPC[i].n, scratch), FPC[i].expect);
    CHECK_INT(om_is_hidden_name("AUTOBOOT.CUE", 1), 1);
    CHECK_INT(om_is_hidden_name("openmenu", 0), 0);
    CHECK_INT(om_is_hidden_name("..", 0), 1);
    /* Kai's /SAVES save tree, at root only, like satiator-saves */
    CHECK_INT(om_is_hidden_name("SAVES", 1), 1);
    CHECK_INT(om_is_hidden_name("saves", 1), 1);
    CHECK_INT(om_is_hidden_name("satiator-saves", 1), 1);
    CHECK_INT(om_is_hidden_name("SAVES", 0), 0);
    CHECK_INT(om_is_hidden_name("Saved Games", 1), 0);
    /* the hidden set hides the current folder name and keeps hiding the old ones, at root only (the Aether
     * rename: both "aether" and "vibemenu" hidden, so moving the folder leaves the fingerprint as it was) */
    CHECK_INT(om_is_hidden_name("aether", 1), 1);
    CHECK_INT(om_is_hidden_name("AETHER", 1), 1);
    CHECK_INT(om_is_hidden_name("aether", 0), 0);
    CHECK_INT(om_is_hidden_name("vibemenu", 1), 1);
    CHECK_INT(om_is_hidden_name("VibeMenu", 1), 1);               /* case-insensitive (FAT) */
    CHECK_INT(om_is_hidden_name("openmenu", 1), 1);
    CHECK_INT(om_is_hidden_name("OpenMenu", 1), 1);
    CHECK_INT(om_is_hidden_name("vibemenu", 0), 0);                /* clarification 7: root only */
    CHECK_INT(om_is_hidden_name("region.txt", 1), 1);              /* the official menu's own root files */
    CHECK_INT(om_is_hidden_name("Region.TXT", 1), 1);
    CHECK_INT(om_is_hidden_name("ar_backup.bin", 1), 1);
    CHECK_INT(om_is_hidden_name("region.txt", 0), 0);
    /* mutations, each with the table CRC repaired so the field check itself is exercised */
    m = th_dup(f, n); m[64 + 0x18 + 1] = 7;                               /* entry 0 folder id 7 >= 3 */
    om_put32(m + 0x24, om_crc32(0, m + 64, 32 * 4));
    CHECK_INT(om_libidx_open(&l, m, n), OM_E_FIELD); free(m);
    m = th_dup(f, n); m[64 + 0x13] = 3;                                   /* image type 3 */
    om_put32(m + 0x24, om_crc32(0, m + 64, 32 * 4));
    CHECK_INT(om_libidx_open(&l, m, n), OM_E_FIELD); free(m);
    m = th_dup(f, n); m[5] ^= 1;
    CHECK_INT(om_libidx_open(&l, m, n), OM_E_MAGIC); free(m);

    /* --- hostile-input RED tests (lesson from the Tasks 3+4 review) --- */

    /* 1. wrapped count: count*32 overflows u32 to a tiny, CRC-valid table_size (0x08000000 * 32 wraps
     * to 0). A crafted 77-byte file: 64-byte header, 0-byte table, 8-byte folder section (one root row),
     * 1-byte pool, 4-byte fingerprint. The division-based guard in om_libidx_open must reject this before
     * any loop walks l->count (134,217,728) entries. */
    {
        uint8_t w1[77];
        memset(w1, 0, sizeof w1);
        memcpy(w1, "OMSATLIB", 8);
        om_put16(w1 + 8, 1);
        om_put16(w1 + 10, 64);
        om_put32(w1 + 0x0C, 0x08000000u);          /* count: wraps *32 to 0 */
        om_put32(w1 + 0x10, 64);                   /* table_off */
        om_put32(w1 + 0x14, 0);                    /* table_size: wrapped value */
        om_put32(w1 + 0x18, 72);                   /* data_off (pool) */
        om_put32(w1 + 0x1C, 1);                     /* data_size (pool) */
        om_put32(w1 + 0x20, 0);                     /* build_date */
        om_put32(w1 + 0x24, om_crc32(0, w1 + 64, 0)); /* table_crc over 0 bytes */
        om_put32(w1 + 0x28, 64); om_put32(w1 + 0x2C, 8);   /* folder section: 1 row */
        om_put32(w1 + 0x30, 72); om_put32(w1 + 0x34, 1);   /* pool section: 1 byte */
        om_put32(w1 + 0x38, 73); om_put32(w1 + 0x3C, 4);   /* fingerprint section */
        om_put32(w1 + 64, 0);                       /* folder 0: path offset 0 */
        om_put16(w1 + 64 + 4, 0xFFFF);              /* folder 0: parent */
        om_put16(w1 + 64 + 6, 0);
        w1[72] = 0;                                  /* pool: single NUL */
        om_put32(w1 + 73, 0);                        /* fingerprint */
        CHECK_INT(om_libidx_open(&l, w1, sizeof w1), OM_E_TABLESIZE);
    }
    /* 2. out-of-range path pool offset: entry 0's path offset set to exactly pool_size (one past the
     * last valid offset), CRC repaired so the field check itself is exercised. */
    m = th_dup(f, n);
    {
        om_libidx l0;
        CHECK_INT(om_libidx_open(&l0, f, n), OM_OK);
        om_put32(m + 64 + 0x14, l0.pool_size);
        pool_off = (uint32_t)((const uint8_t *)l0.pool - f);
    }
    om_put32(m + 0x24, om_crc32(0, m + 64, 32 * 4));
    CHECK_INT(om_libidx_open(&l, m, n), OM_E_FIELD); free(m);
    /* 3. unterminated string in the pool: the pool's last byte (required 0 by clarification 3's
     * NUL-terminated-pool invariant) is overwritten with a non-NUL byte. No table mutation, so the
     * table CRC stays valid; only the pool-terminator check is exercised. */
    m = th_dup(f, n);
    {
        om_libidx l0;
        CHECK_INT(om_libidx_open(&l0, f, n), OM_OK);
        m[pool_off + l0.pool_size - 1] = 1;
    }
    CHECK_INT(om_libidx_open(&l, m, n), OM_E_FIELD); free(m);
    free(f);
    /* 4. path over 255 bytes: the writer must refuse a path longer than spec §2's 255-byte limit, both
     * for a folder path and for an entry path. A 255-byte path is the boundary and must still succeed. */
    {
        static om_lib_entry ent2[2];
        static uint16_t order2[2];
        static om_lib_folder fold2[2];
        static char pool2[1024];
        char longpath[257];
        om_libw w2;
        om_lib_entry e2;
        memset(longpath, 'A', 255); longpath[255] = 0;      /* 255 bytes: boundary, must succeed */
        om_libw_init(&w2, ent2, 2, order2, fold2, 2, pool2, sizeof pool2);
        CHECK_INT(om_libw_folder(&w2, longpath, 0xFFFF), 0);
        memset(longpath, 'B', 256); longpath[256] = 0;      /* 256 bytes: over the limit, must fail */
        CHECK_INT(om_libw_folder(&w2, longpath, 0), OM_E_FULL);
        memset(&e2, 0, sizeof e2);
        e2.key[0] = 0; e2.path = longpath; e2.title = "";
        CHECK_INT(om_libw_entry(&w2, &e2), OM_E_FULL);
    }
    return th_done("test_libidx");
}
