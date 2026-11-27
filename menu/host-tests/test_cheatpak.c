/* menu/host-tests/test_cheatpak.c -- contract/SPEC.md §5.15 CHEATS.PAK: the pure reader/encoder over
 * the fixture and the repo's own CHEATS.PAK, then om_cheats_load through the real menu/app/sd.c over fake_satiator.c
 * (2 KB per call, 4-aligned buffers), with damaged, truncated and interrupted-replace files. BSD-3-Clause. */
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "th.h"
#include "cheatpak.h"
#include "cheats.h"
#include "om_fmt.h"
#include "pergame.h"
#include "sd.h"
#include "satiator.h"

extern int fake_reads, fake_paths;
extern uint64_t fake_us;

static om_ch_file f;
static uint32_t stage32[OM_CP_STAGE / 4 + 1];
static uint8_t enc[OM_CP_MAX_REC];
static char root[] = "/tmp/omsat_cheatpak_XXXXXX";

static void put_file(const char *rel, const uint8_t *d, uint32_t n)
{
    char p[600];
    FILE *fp;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    fp = fopen(p, "wb");
    fwrite(d, 1, n, fp);
    fclose(fp);
}

static void rm_file(const char *rel)
{
    char p[600];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    unlink(p);
}

static int load(const char *key)
{
    return om_cheats_load(key, (uint8_t *)stage32, sizeof stage32, &f);
}

static void check_fixture_pure(const uint8_t *d, uint32_t n)
{
    om_cp_hdr h;
    const uint8_t *r1, *r2, *r3;
    uint32_t off, len, off2, len2, m;
    uint8_t rows8[4][8];
    static const uint8_t want0[8] = {0x06, 0x0D, 0xBC, 0xB0, 0xFF, 0xFF, 0x01, 0x00};
    static const uint8_t want2[8] = {0x06, 0x01, 0x23, 0x45, 0x00, 0xFF, 0x03, 0x00};
    CHECK_INT(om_cp_header(d, &h), OM_OK);
    CHECK_INT(h.count, 3);
    CHECK_INT(h.data_off, 64 + 3 * OM_CP_ROW);
    CHECK_INT(h.data_end, n);
    CHECK_INT(om_crc32(0, d + 64, 3 * OM_CP_ROW), h.table_crc);
    r1 = om_cp_scan(d + 64, h.count, "GS-9001");
    r2 = om_cp_scan(d + 64, h.count, "T-8113H-50");
    r3 = om_cp_scan(d + 64, h.count, "T-8113H");
    CHECK(r1 && r2 && r3 && r3 != r2);
    CHECK(om_cp_scan(d + 64, h.count, "T-8113") == NULL);           /* a prefix is not a key */
    CHECK(om_cp_scan(d + 64, h.count, "ABCDEFGHIJKLMNOPQ") == NULL); /* 17 bytes: no key is that long */
    CHECK_INT(om_cp_row(&h, r1, &off, &len), OM_OK);
    CHECK_INT(om_cp_row(&h, r2, &off2, &len2), OM_OK);
    CHECK_INT(off, off2);                                             /* GS-9001 and T-8113H-50 share one record */
    CHECK_INT(om_cp_decode(d + off, len, &f), OM_OK);
    {   /* 2026-11-25: the Launch Options count -- label rows not counted; its CRC checked as decode checks it */
        uint32_t on = 9, nn = 9, k, won = 0, wn = 0;
        for (k = 0; k < f.ncheats; k++)
            if (!(f.cheat[k].flags & OM_CH_HEADER)) { wn++; won += (f.cheat[k].flags & OM_CH_ON) != 0; }
        CHECK_INT(om_cp_count(d + off, len, &on, &nn), OM_OK);
        CHECK_INT(nn, wn); CHECK_INT(on, won); CHECK_INT(nn, 3);
        uint8_t *b = th_dup(d, n);
        b[off + 30] ^= 1; CHECK_INT(om_cp_count(b + off, len, &on, &nn), OM_E_CRC);
        CHECK_INT(om_cp_count(d + off, len - 1, &on, &nn), OM_E_FIELD);
    }
    CHECK_INT(f.master_addr, 0x06000914u); CHECK_INT(f.master_value, 0xC305u);
    CHECK_INT(f.flags, OM_CP_G_MASTER | OM_CP_G_CWX); CHECK_INT(f.location, 0x06002800u); CHECK_INT(f.cache4, 39);
    CHECK_INT(f.ncheats, 4); CHECK_INT(f.ncodes, 4);
    CHECK_STR(f.cheat[0].name, "PLAYER 1"); CHECK_INT(f.cheat[0].flags, OM_CH_HEADER); CHECK_INT(f.cheat[0].count, 0);
    CHECK_STR(f.cheat[1].name, "UNLIMITED LIVES"); CHECK_INT(f.cheat[1].flags, 0);
    CHECK_INT(f.cheat[1].first, 0); CHECK_INT(f.cheat[1].count, 1);
    CHECK_STR(f.cheat[2].name, "MAX MONEY"); CHECK_INT(f.cheat[2].flags, OM_CH_ON);
    CHECK_INT(f.cheat[2].first, 1); CHECK_INT(f.cheat[2].count, 2);
    CHECK_STR(f.cheat[3].name, "MY CODE"); CHECK_INT(f.cheat[3].flags, OM_CH_ON | OM_CH_USER);
    CHECK_INT(f.code[0].addr, 0x060DBCAAu); CHECK_INT(f.code[0].value, 3); CHECK_INT(f.code[0].cmd, 1);
    CHECK_INT(f.code[3].addr, 0x06012345u); CHECK_INT(f.code[3].value, 0xFF); CHECK_INT(f.code[3].cmd, 3);
    m = om_ch_enabled_codes(&f, rows8, 4);                            /* MAX MONEY's two codes, then MY CODE's */
    CHECK_INT(m, 3);
    CHECK_INT(memcmp(rows8[0], want0, 8), 0);
    CHECK_INT(memcmp(rows8[2], want2, 8), 0);
    CHECK_INT(om_ch_enabled_codes(&f, rows8, 1), 1);                  /* cap respected */
    CHECK_INT(om_cp_encode(&f, enc, sizeof enc, &m), OM_OK);
    CHECK_INT(m, len);
    CHECK_INT(memcmp(enc, d + off, len), 0);                          /* encode(decode(record)) is the record */
    CHECK_INT(om_cp_encode(&f, enc, len - 4, &m), OM_E_FULL);
    f.cheat[0].flags |= OM_CH_ON;                                     /* a header row may not be on */
    CHECK_INT(om_cp_encode(&f, enc, sizeof enc, &m), OM_E_FIELD);
    f.cheat[0].flags = OM_CH_HEADER;
    f.cheat[1].name[0] = 0;                                           /* nor a cheat have no name */
    CHECK_INT(om_cp_encode(&f, enc, sizeof enc, &m), OM_E_FIELD);
    CHECK_INT(om_cp_row(&h, r3, &off, &len), OM_OK);
    CHECK_INT(om_cp_decode(d + off, len, &f), OM_OK);
    CHECK_INT(f.flags, 0); CHECK_INT(f.ncheats, 1); CHECK_STR(f.cheat[0].name, "INVINCIBLE");
    CHECK_INT(f.code[0].cmd, 0x0D);
}

static void check_pure_rejects(const uint8_t *d, uint32_t n)
{
    om_cp_hdr h;
    uint8_t *b = th_dup(d, n);
    uint32_t off = om_be32(d + 64 + 16), len = om_be32(d + 64 + 20);
    b[0] = 'X'; CHECK_INT(om_cp_header(b, &h), OM_E_MAGIC); b[0] = d[0];
    b[9] = 2; CHECK_INT(om_cp_header(b, &h), OM_E_VERSION); b[9] = d[9];
    b[0x17] ^= 0x18; CHECK_INT(om_cp_header(b, &h), OM_E_TABLESIZE); b[0x17] = d[0x17];
    b[0x30] = 1; CHECK_INT(om_cp_header(b, &h), OM_E_RANGE); b[0x30] = 0;
    b[off + 30] ^= 1; CHECK_INT(om_cp_decode(b + off, len, &f), OM_E_CRC); b[off + 30] = d[off + 30];
    CHECK_INT(om_cp_decode(b + off, len - 4, &f), OM_E_CRC);          /* a record cut short */
    CHECK_INT(om_cp_decode(b + off, len - 1, &f), OM_E_FIELD);        /* not a multiple of 4 */
    CHECK_INT(om_cp_header(b, &h), OM_OK);
    om_put32(b + 64 + 16, off + 2);                                   /* unaligned record offset */
    CHECK_INT(om_cp_row(&h, b + 64, &off, &len), OM_E_RANGE);
    om_put32(b + 64 + 16, h.data_end);                                /* past the data */
    CHECK_INT(om_cp_row(&h, b + 64, &off, &len), OM_E_RANGE);
    free(b);
}

/* Every record of the repo's own CHEATS.PAK decodes in C and re-encodes to the same bytes: the C reader and
 * tools/omsat/cheatpak.py agree on the whole shipped file, not only on the fixture. */
static void check_repo_pak(void)
{
    uint32_t n, i, off, len, m, cheats = 0, bad = 0;
    uint8_t *d = th_load("../../tools/data/cheats/CHEATS.PAK", &n);
    om_cp_hdr h;
    CHECK(n <= OM_CP_MAX_FILE);
    CHECK_INT(om_cp_header(d, &h), OM_OK);
    CHECK_INT(h.data_end, n);
    CHECK_INT(om_crc32(0, d + 64, h.count * OM_CP_ROW), h.table_crc);
    CHECK(h.count >= 100);                                          /* 111 keys in the shipped 2016 set */
    for (i = 0; i < h.count; i++) {
        if (om_cp_row(&h, d + 64 + i * OM_CP_ROW, &off, &len) || om_cp_decode(d + off, len, &f)
            || om_cp_encode(&f, enc, sizeof enc, &m) || m != len || memcmp(enc, d + off, len)) {
            bad++;
            continue;
        }
        cheats += f.ncheats;
    }
    CHECK_INT(bad, 0);
    printf("repo CHEATS.PAK: %u B, %u games, %u cheat rows, all decoded and re-encoded byte-exact\n", n, h.count,
           cheats);
    free(d);
}

int main(void)
{
    char p[600];
    uint32_t n, rn, off = 0, len = 0;
    uint8_t *d = th_load(FIX("tiny.cheats.pak"), &n);
    uint8_t *real = th_load("../../tools/data/cheats/CHEATS.PAK", &rn), *b;
    int r;

    check_fixture_pure(d, n);
    check_pure_rejects(d, n);
    check_repo_pak();

    CHECK_INT(om_ch_want(1, OM_PG_DEFAULT), 1);
    CHECK_INT(om_ch_want(0, OM_PG_DEFAULT), 0);
    CHECK_INT(om_ch_want(0, OM_PG_ON), 1);
    CHECK_INT(om_ch_want(1, OM_PG_OFF), 0);

    CHECK(mkdtemp(root) != NULL);
    setenv("FAKE_SD_ROOT", root, 1);
    snprintf(p, sizeof p, "%s/aether", root);
    mkdir(p, 0755);

    CHECK_INT(load("GS-9001"), OM_SD_NOFILE);                         /* no file at all */
    put_file("aether/CHEATS.PAK", d, n);
    CHECK_INT(load("GS-9001"), 1);
    CHECK_STR(f.cheat[2].name, "MAX MONEY"); CHECK_INT(f.ncodes, 4);
    CHECK_INT(load("T-8113H"), 1);
    CHECK_STR(f.cheat[0].name, "INVINCIBLE");
    CHECK_INT(load("T-99999G"), 0);                                   /* a game with no cheats */
    CHECK_INT(om_cheats_load("GS-9001", (uint8_t *)stage32 + 2, sizeof stage32 - 4, &f), OM_E_FIELD);
    CHECK_INT(om_cheats_load("GS-9001", (uint8_t *)stage32, OM_CP_STAGE - 4, &f), OM_E_FIELD);

    /* damage: one byte of GS-9001's shared record -- that record is refused, the other game still loads */
    om_cp_row(&(om_cp_hdr){0, 64 + 3 * OM_CP_ROW, n, 0}, om_cp_scan(d + 64, 3, "GS-9001"), &off, &len);
    b = th_dup(d, n);
    b[off + len - 9] ^= 0x40;
    put_file("aether/CHEATS.PAK", b, n);
    CHECK_INT(load("GS-9001"), OM_E_CRC);
    CHECK_INT(load("T-8113H-50"), OM_E_CRC);
    CHECK_INT(load("T-8113H"), 1);
    /* damage in the index: every lookup is refused, even one whose row is intact */
    memcpy(b, d, n);
    b[64 + 2 * OM_CP_ROW + 3] ^= 1;
    put_file("aether/CHEATS.PAK", b, n);
    CHECK_INT(load("GS-9001"), OM_E_CRC);
    CHECK_INT(load("T-99999G"), OM_E_CRC);
    /* a bad header */
    memcpy(b, d, n);
    b[3] = 'X';
    put_file("aether/CHEATS.PAK", b, n);
    CHECK_INT(load("GS-9001"), OM_E_MAGIC);
    /* truncated: inside the index, inside a record, before the header ends */
    put_file("aether/CHEATS.PAK", d, 100);
    CHECK_INT(load("GS-9001"), OM_E_SHORT);
    put_file("aether/CHEATS.PAK", d, off + len / 2);
    CHECK_INT(load("GS-9001"), OM_E_SHORT);
    CHECK_INT(load("T-99999G"), 0);                                   /* the index is whole: "no cheats" is true */
    put_file("aether/CHEATS.PAK", d, 40);
    CHECK(load("GS-9001") < 0);
    put_file("aether/CHEATS.PAK", d, 0);
    CHECK(load("GS-9001") < 0);
    free(b);
    b = 0;

    /* an interrupted replace (sdwrite.h): CHEATS.PAK gone, CHEATS.PAK.NEW is the latest file */
    rm_file("aether/CHEATS.PAK");
    put_file("aether/CHEATS.PAK.NEW", d, n);
    CHECK_INT(load("T-8113H"), 1);
    b = th_dup(d, n);
    b[3] = 'X';
    put_file("aether/CHEATS.PAK", b, n);                            /* both: CHEATS.PAK wins, even a bad one */
    CHECK_INT(load("T-8113H"), OM_E_MAGIC);
    free(b);
    put_file("aether/CHEATS.PAK", real, rn);
    rm_file("aether/CHEATS.PAK.NEW");

    /* the repo's file on the card: one lookup's cost, on fake_satiator's virtual clock (reads at the diag page's
     * 296 KB/s, other commands 1 ms each: a model, not a hardware measurement) */
    {
        char last[17] = {0};
        memcpy(last, real + 64 + (om_be32(real + 0x0C) - 1) * OM_CP_ROW, 16);   /* the last row: a full scan */
        fake_reads = fake_paths = 0;
        fake_us = 0;
        r = load(last);
    }
    printf("repo CHEATS.PAK lookup: rc %d, %d reads, %d path walks, %llu us (fake clock)\n", r, fake_reads, fake_paths,
           (unsigned long long)fake_us);
    CHECK_INT(r, 1);
    CHECK(fake_paths == 1);
    CHECK(fake_reads <= 3 + (int)((rn - 64) / OM_CP_PIECE) + 2);
    free(real);
    free(d);
    return th_done("test_cheatpak");
}
