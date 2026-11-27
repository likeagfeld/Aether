/* menu/host-tests/test_fakefs.c -- the OMFAKEFS reader, then the fake libsatiator driving the REAL card loader
 * (menu/app/card.c, satfs.c, sd.c) to a LIBRARY.IDX whose fingerprint matches -- what the Saturn fake build does.
 * BSD-3-Clause. */
#include "th.h"
#include "card.h"
#include "fakefs.h"
#include "fakesat.h"
#include "satiator.h"
#include "sd.h"

static uint8_t blob[200000], data[150000];
static uint32_t nfile, dlen;
static uint8_t gdb[300000], art[140000], lib[220000], vrt[16384];
static om_scan_work work;

static void add(const char *path, const uint8_t *d, uint32_t n)
{
    uint8_t *row = blob + 16 + 264 * nfile++;
    memset(row, 0, 264);
    strcpy((char *)row, path);
    om_put32(row + 256, dlen);              /* data-relative here; finish() rebases it */
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
    memcpy(blob + table_end, data, dlen);
    return table_end + dlen;
}

static void st(void *ctx, const char *line) { (void)ctx; (void)line; }

int main(void)
{
    uint32_t ng, nl, len, off, size;
    uint8_t *g = th_load(FIX("tiny.games.db"), &ng), *l = th_load(FIX("tiny.library.idx"), &nl);
    static const uint8_t none[1];
    om_fakefs f;
    char name[256];
    int dir;
    om_card c;
    om_card_bufs b = { gdb, sizeof gdb, art, sizeof art, lib, sizeof lib, &work, vrt, sizeof vrt };
    static uint32_t buf32[64];
    static uint32_t stbuf[76];                               /* 4-aligned s_stat_t buffer, satfs.c's own pattern */
    int fd;
    add("aether/GAMES.DB", g, ng);
    add("aether/LIBRARY.IDX", l, nl);
    add("Racing/Daytona USA/daytona.cue", none, 0);
    add("RPG/Grandia/disc1.cue", none, 0);
    add("RPG/Grandia/disc2.cue", none, 0);
    add("unknown.iso", none, 0);
    len = finish();
    CHECK_INT(om_fakefs_open(&f, blob, len), OM_OK);
    CHECK_INT(f.count, 6);
    CHECK_INT(om_fakefs_find(&f, "/aether/GAMES.DB", &off, &size), 1); CHECK_INT(size, ng);
    CHECK_INT(memcmp(blob + off, g, ng), 0);
    CHECK_INT(om_fakefs_find(&f, "RPG", &off, &size), 2);
    CHECK_INT(om_fakefs_find(&f, "RP", &off, &size), 0);     /* a prefix of a name is not a directory */
    CHECK_INT(om_fakefs_child(&f, "", 0, name, &dir), 1); CHECK_STR(name, "aether"); CHECK_INT(dir, 1);
    CHECK_INT(om_fakefs_child(&f, "/", 1, name, &dir), 1); CHECK_STR(name, "Racing");
    CHECK_INT(om_fakefs_child(&f, "", 2, name, &dir), 1); CHECK_STR(name, "RPG");      /* deduplicated */
    CHECK_INT(om_fakefs_child(&f, "", 3, name, &dir), 1); CHECK_STR(name, "unknown.iso"); CHECK_INT(dir, 0);
    CHECK_INT(om_fakefs_child(&f, "", 4, name, &dir), 0);
    CHECK_INT(om_fakefs_child(&f, "RPG/Grandia", 1, name, &dir), 1); CHECK_STR(name, "disc2.cue");
    blob[0] = 'X';
    CHECK_INT(om_fakefs_open(&f, blob, len), OM_E_MAGIC);
    blob[0] = 'O';
    /* review-9.md: om_fakefs_open's bounds checks were correct but untested. count x OM_FAKEFS_ENT (264)
     * overflow -- a claimed count too large for the blob -- is rejected before any i * 264 addition runs. */
    {
        uint32_t saved_count = om_be32(blob + 8);
        om_put32(blob + 8, 0xFFFFFFFFu);
        CHECK_INT(om_fakefs_open(&f, blob, len), OM_E_RANGE);
        om_put32(blob + 8, saved_count);
    }
    /* A path with no NUL anywhere in its 256-byte field (the row's last byte, e[255], forced non-zero). */
    {
        uint8_t *last = blob + 16 + 0 * 264 + 255;
        uint8_t saved_last = *last;
        *last = 'X';
        CHECK_INT(om_fakefs_open(&f, blob, len), OM_E_FIELD);
        *last = saved_last;
    }
    /* offset+size beyond the blob -- same OM_E_FIELD branch (om_in_range fails). */
    {
        uint8_t *row = blob + 16 + 0 * 264;
        uint32_t saved_size = om_be32(row + 260);
        om_put32(row + 260, len);                /* off (already < len) + len certainly runs past the blob */
        CHECK_INT(om_fakefs_open(&f, blob, len), OM_E_FIELD);
        om_put32(row + 260, saved_size);
    }
    CHECK_INT(om_fakefs_open(&f, blob, len), OM_OK);         /* every corrupted field was restored */
    om_fakesat_mount(blob, len);
    CHECK_INT(s_mode(s_api), 0);
    CHECK_INT(om_card_load(&c, &b, st, NULL), OM_OK);
    CHECK_INT(c.lib_source, OM_LIB_FROM_IDX);                /* fingerprint of {Racing, RPG, unknown.iso} matched */
    CHECK_INT(c.db_rc, OM_OK);
    CHECK_INT(c.art_rc, OM_SD_NOFILE);                       /* no ART.PAK on the fake card: fallback path */
    memcpy(buf32, "hello fake", 10);
    fd = s_open(om_sd_path("/aether/FAVS.TXT"), FA_WRITE | FA_CREATE_ALWAYS);
    CHECK(fd >= 0);
    CHECK_INT(s_write(fd, buf32, 10), 10);
    s_close(fd);
    CHECK_INT(om_sd_size("/aether/FAVS.TXT", &size), 0); CHECK_INT(size, 10);
    memset(buf32, 0, sizeof buf32);
    CHECK_INT(om_sd_read_all("/aether/FAVS.TXT", buf32, sizeof buf32, &size), 0);
    CHECK_INT(memcmp(buf32, "hello fake", 10), 0);
    /* review-9.md: HWF-22's S_MAXBUF (2048) cap, exercised directly against s_read/s_write (om_sd_* already
     * chunks to <= 2 KB before ever reaching these, so that path alone never proves the guard exists). */
    fd = s_open(om_sd_path("/aether/FAVS.TXT"), FA_READ | FA_OPEN_EXISTING);
    CHECK(fd >= 0);
    CHECK_INT(s_read(fd, buf32, S_MAXBUF + 1), -FR_INVALID_PARAMETER);
    s_close(fd);
    fd = s_open(om_sd_path("/aether/TOOLONG.TXT"), FA_WRITE | FA_CREATE_ALWAYS);
    CHECK(fd >= 0);
    CHECK_INT(s_write(fd, buf32, S_MAXBUF + 1), -FR_INVALID_PARAMETER);
    s_close(fd);
    /* review-9.md fix round 1: s_stat's statsize < 9 guard (matches satiator.c:291-292). Before the fix,
     * fill_stat's `statsize - 9` went negative here and became a huge size_t memcpy length. */
    CHECK_INT(s_stat(NULL, (s_stat_t *)stbuf, 8), -FR_INVALID_PARAMETER);
    CHECK_INT(s_reset_to_satiator(), -1);                    /* never jumps into the BIOS in the fake build */
    free(g); free(l);
    return th_done("test_fakefs");
}
