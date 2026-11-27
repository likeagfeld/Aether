#include "th.h"
#include "gamesdb.h"

int main(void)
{
    uint32_t n;
    uint8_t *f = th_load(FIX("tiny.games.db"), &n), *m;
    om_gamesdb db;
    om_game g;
    char canon[17];
    CHECK_INT(om_gamesdb_open(&db, f, n), OM_OK);
    CHECK_INT(db.count, 4);
    CHECK_INT(om_gamesdb_find(&db, "GS-9001", &g), 1);
    CHECK_STR(g.title_en, "Daytona USA");
    CHECK_STR(g.title_rom, "Daytona USA");
    CHECK_STR(g.maker, "SEGA");
    CHECK_INT(g.region, 15); CHECK_INT(g.players, 1); CHECK_INT(g.periph, 19);
    CHECK_INT(g.year, 1995); CHECK_INT(g.date, 19950401u); CHECK_INT(g.disc_count, 1);
    CHECK_STR(om_gamesdb_genre(&db, g.genre), "Racing");
    CHECK_INT(om_gamesdb_find(&db, "GS-7103", &g), 1);
    CHECK_STR(om_gamesdb_genre(&db, g.genre), "Fighting 3D");
    CHECK_STR(g.maker, "");
    CHECK_INT(om_gamesdb_find(&db, "T-4507G", &g), 1);
    CHECK_INT(g.disc_count, 2);
    CHECK_INT(om_gamesdb_find(&db, "XX-0000", &g), 0);
    CHECK(om_gamesdb_genre(&db, 0xFF) == NULL);
    CHECK(om_gamesdb_genre(&db, 4) == NULL);
    CHECK_INT(om_gamesdb_alias(&db, "610-6342", canon), 1);
    CHECK_STR(canon, "GS-7103");
    CHECK_INT(om_gamesdb_alias(&db, "GS-7103", canon), 0);
    om_gamesdb_at(&db, 0, &g);
    CHECK_STR(g.key, "GS-7103");                                         /* table is sorted by key */
    {   /* compat bytes 0x2E-0x3F. make_fixtures.py puts the compat row on GS-9001 (table index 1,
         * sorted after GS-7103): hint=OM_LH_JHL(1), flags=3, note="JHL loader required", region mask 0x04 (U only). */
        om_compat c;
        CHECK_INT(om_gdb_compat(&db, 1, 0x04, &c), 1);
        CHECK_INT(c.hint, OM_LH_JHL);
        CHECK_INT(c.flags, 3);
        CHECK_STR(c.note, "JHL loader required");
        CHECK_INT(om_gdb_compat(&db, 1, 0x01, &c), 0);                   /* region mask U only, a J disc */
        CHECK_INT(om_gdb_compat(&db, 3, 0x1F, &c), 0);                   /* T-8113H: no compat row at all */
    }
    m = th_dup(f, n); m[64 + 0x11] = 9;                                  /* genre id 9 >= 4 genres */
    om_put32(m + 0x24, om_crc32(0, m + 64, 64 * 4));
    CHECK_INT(om_gamesdb_open(&db, m, n), OM_E_FIELD); free(m);
    m = th_dup(f, n); om_put32(m + 64 + 0x1C, 0x7FFFFFFFu);              /* title offset beyond pool */
    om_put32(m + 0x24, om_crc32(0, m + 64, 64 * 4));
    CHECK_INT(om_gamesdb_open(&db, m, n), OM_E_FIELD); free(m);
    /* brief Step 5: compat note offset (record 1 = GS-9001, 0x30) beyond pool. */
    m = th_dup(f, n); om_put32(m + 64 + 64 + 0x30, 0x7FFFFFFFu);
    om_put32(m + 0x24, om_crc32(0, m + 64, 64 * 4));
    CHECK_INT(om_gamesdb_open(&db, m, n), OM_E_FIELD); free(m);
    m = th_dup(f, n); m[n - 1] = 'x';                                    /* pool (last section) not NUL-terminated */
    CHECK_INT(om_gamesdb_open(&db, m, n), OM_E_FIELD); free(m);
    free(f);
    {   /* fix round 1 regression: count x 64 overflow (coordinator-reported). 64u * 67108866u wraps mod
         * 2^32 to 128, matching this crafted, CRC-valid 2-record (128-byte) table -- `h.table_size !=
         * 64u * h.count` used to pass, after which the per-record loop (`db->table + 64 * i` for i up to
         * 67,108,865) read far out of bounds past record 1 (the 2 records here are genuinely valid and
         * ascending, with a valid 1-byte string pool and a 0-entry genre table, so the crafted buffer
         * clears every earlier check and reaches that loop). The fixed guard (`table_size % 64u != 0 ||
         * table_size / 64u != h.count`) rejects this immediately after om_header_read, with no crash. */
        uint8_t ov[195] = { 0 };
        om_gamesdb db2;
        memcpy(ov, "OMSATGDB", 8);
        om_put16(ov + 0x08, 1);
        om_put16(ov + 0x0A, 64);
        om_put32(ov + 0x0C, 67108866u);  /* wraps: 64u * 67108866u == 128 (mod 2^32) */
        om_put32(ov + 0x10, 67);         /* table_off */
        om_put32(ov + 0x14, 128);        /* table_size: 2 real records (64 each), != the wrapped count */
        om_put32(ov + 0x18, 0);          /* data_off (generic header field, unused by GAMES.DB) */
        om_put32(ov + 0x1C, 0);          /* data_size */
        om_put32(ov + 0x20, 20261117u);  /* build_date */
        om_put32(ov + 0x28, 64);  om_put32(ov + 0x2C, 2);     /* section 0 (genre): off 64, size 2 -> ngenre 0 */
        om_put32(ov + 0x30, 66);  om_put32(ov + 0x34, 0);     /* section 1 (alias): off 66, size 0 */
        om_put32(ov + 0x38, 66);  om_put32(ov + 0x3C, 1);     /* section 2 (pool): off 66, size 1 ("\0") */
        /* offsets 64-65: ngenre = 0. offset 66: the pool's single NUL byte. */
        memset(ov + 67, 0x01, 16); ov[67 + 0x11] = 0xFF;      /* record 0: key = 16x 0x01, genre = unknown */
        memset(ov + 131, 0x02, 16); ov[131 + 0x11] = 0xFF;    /* record 1: key = 16x 0x02 (ascending), genre = unknown */
        om_put32(ov + 0x24, om_crc32(0, ov + 67, 128));
        CHECK_INT(om_gamesdb_open(&db2, ov, sizeof ov), OM_E_TABLESIZE);
    }
    return th_done("test_gamesdb");
}
