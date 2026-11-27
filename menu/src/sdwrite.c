/* menu/src/sdwrite.c -- see sdwrite.h (the rules, the replace steps and the recovery rule live there). Runs once per
 * save, never per frame (tools/sbuild.py COLD_OS). BSD-3-Clause. */
#include <string.h>
#include "om_fmt.h"
#include "satiator.h"
#include "sdwrite.h"
#include "yield.h"

static uint16_t (*s_now)(void);
static uint32_t (*s_us)(uint16_t);

void om_sdw_set_timer(uint16_t (*now)(void), uint32_t (*to_us)(uint16_t ticks)) { s_now = now; s_us = to_us; }

static const char *stage(uint8_t *dst, const char *path, int dotnew)
{   /* the name as libsatiator needs it: in a 4-aligned buffer (it is walked as uint32_t*) */
    uint32_t n = (uint32_t)strlen(path);
    if (n > 250) n = 250;
    memcpy(dst, path, n);
    if (dotnew) { memcpy(dst + n, ".NEW", 4); n += 4; }
    dst[n] = 0;
    return (const char *)dst;
}

static int put(int fd, uint32_t off, const uint8_t *b, uint32_t w, om_sdw_gap_fn gap, om_sdw_stats *st)
{   /* one chunk: write + sync, retried once after a gap from its own offset */
    uint16_t t0 = s_now ? s_now() : 0;
    int k, r;
    for (k = 0;; k++) {
        r = s_write(fd, b, (int)w);
        if (r == (int)w && (r = s_sync(fd)) >= 0)
            break;
        r = r < 0 ? OM_SDW_ERR(r) : OM_E_SHORT;
        if (k)
            return r;
        st->retries++;
        if (gap) gap();
        if ((r = s_seek(fd, (int)off, C_SEEK_SET)) < 0)
            return OM_SDW_ERR(r);
    }
    st->chunks++;
    if (s_now) {
        uint32_t us = s_us((uint16_t)(s_now() - t0));
        st->sum_us += us;
        if (us > st->max_us) st->max_us = us;
    }
    return 0;
}

static int check(const char *path, int dotnew, uint8_t *scr, uint32_t len, uint32_t crc, om_sdw_stats *st)
{   /* the read-back: same length, same CRC-32 */
    uint32_t got = 0, c = 0;
    int r, fd;
    om_yield();                  /* the launch's eye (yield.h): a chance before each path command and each read */
    if ((fd = s_open(stage(scr, path, dotnew), FA_READ | FA_OPEN_EXISTING)) < 0)
        return OM_SDW_ERR(fd);
    for (;;) {
        om_yield();
        if ((r = s_read(fd, scr, S_MAXBUF)) <= 0 || got > len) break;
        c = om_crc32(c, scr, (uint32_t)r);
        got += (uint32_t)r;
    }
    s_close(fd);
    if (r < 0)
        return OM_SDW_ERR(r);
    if (got != len || c != crc) {
        st->verify_fail++;
        return OM_E_CRC;
    }
    return 0;
}

static int write_src(const char *path, int dotnew, om_sdw_src_fn src, void *ctx, int verify, uint8_t *scr,
                     om_sdw_gap_fn gap, om_sdw_stats *st)
{
    uint32_t fill = 0, off = 0, crc = 0, w;
    int32_t n;
    int fd, rc = 0, c, end = 0;
    om_yield();
    if ((fd = s_open(stage(scr, path, dotnew), FA_WRITE | FA_CREATE_ALWAYS)) < 0)
        return OM_SDW_ERR(fd);
    om_yield_wr++;                            /* open for write: the launch's pacer reads nothing (yield.h) */
    while (!end) {
        if ((n = src(ctx, scr + fill, OM_SDW_CHUNK - fill)) < 0) { rc = (int)n; break; }
        fill += (uint32_t)n;
        if (n > 0 && fill < OM_SDW_CHUNK)
            continue;
        end = n == 0 && OM_SDW_CHUNK - fill >= OM_SDW_PIECE_MAX;
        w = end ? fill : fill & ~3u;          /* a source out of room: the 4-multiple goes out, 0-3 B carried */
        if (!w)
            continue;
        if (off && gap) gap();
        if ((rc = put(fd, off, scr, w, gap, st)) != 0)
            break;
        crc = om_crc32(crc, scr, w);
        off += w;
        fill -= w;
        memmove(scr, scr + w, fill);
    }
    c = s_close(fd);
    om_yield_wr--;
    if (!rc && c < 0)
        rc = OM_SDW_ERR(c);
    if (!rc && verify)
        rc = check(path, dotnew, scr, off, crc, st);
    return rc;
}

typedef struct { const uint8_t *p; uint32_t left; } buf_src;

static int32_t from_buf(void *ctx, uint8_t *dst, uint32_t cap)
{
    buf_src *b = (buf_src *)ctx;
    uint32_t n = b->left < cap ? b->left : cap;
    memcpy(dst, b->p, n);
    b->p += n;
    b->left -= n;
    return (int32_t)n;
}

static int done(om_sdw_stats *st, int rc) { st->last_rc = (uint32_t)rc; return rc; }

/* a path stage() would cut is refused, never written or renamed under a different name */
#define TOO_LONG(path) (strlen(path) > 250u)

int om_sdw_write_src(const char *path, om_sdw_src_fn src, void *ctx, int verify, uint8_t *scratch2k,
                     om_sdw_gap_fn gap, om_sdw_stats *st)
{
    om_sdw_stats d;
    if (!st) { memset(&d, 0, sizeof d); st = &d; }
    if (TOO_LONG(path)) return done(st, OM_E_FIELD);
    return done(st, write_src(path, 0, src, ctx, verify, scratch2k, gap, st));
}

int om_sdw_write_file(const char *path, const uint8_t *data, uint32_t len, int verify, uint8_t *scratch2k,
                      om_sdw_gap_fn gap, om_sdw_stats *st)
{
    buf_src b;
    b.p = data;
    b.left = len;
    return om_sdw_write_src(path, from_buf, &b, verify, scratch2k, gap, st);
}

int om_sdw_write_at(const char *path, uint32_t off, const uint8_t *data, uint32_t len, uint8_t *scratch2k,
                    om_sdw_gap_fn gap, om_sdw_stats *st)
{
    om_sdw_stats d;
    uint32_t x, w;
    int fd, rc = 0, c;
    if (!st) { memset(&d, 0, sizeof d); st = &d; }
    if (TOO_LONG(path)) return done(st, OM_E_FIELD);
    om_yield();
    if (off) {
        if ((rc = s_stat(stage(scratch2k, path, 0), (s_stat_t *)scratch2k, 12)) < 0) return done(st, OM_SDW_ERR(rc));
        if (((s_stat_t *)scratch2k)->size != off) return done(st, OM_E_RANGE);
        om_yield();
        if ((fd = s_open(stage(scratch2k, path, 0), FA_WRITE | FA_OPEN_EXISTING)) < 0) return done(st, OM_SDW_ERR(fd));
        if ((rc = s_seek(fd, (int)off, C_SEEK_SET)) < 0) { s_close(fd); return done(st, OM_SDW_ERR(rc)); }
        rc = 0;
    } else if ((fd = s_open(stage(scratch2k, path, 0), FA_WRITE | FA_CREATE_ALWAYS)) < 0)
        return done(st, OM_SDW_ERR(fd));
    om_yield_wr++;
    for (x = 0; x < len; x += w) {
        w = len - x > OM_SDW_CHUNK ? OM_SDW_CHUNK : len - x;    /* a multiple of 4 but the last (Kai v6.455) */
        memcpy(scratch2k, data + x, w);
        if (x && gap) gap();
        if ((rc = put(fd, off + x, scratch2k, w, gap, st)) != 0) break;
    }
    c = s_close(fd);
    om_yield_wr--;
    if (!rc && c < 0) rc = OM_SDW_ERR(c);
    return done(st, rc);
}

/* a name under a folder that does not exist is FR_NO_PATH on real FatFs (the Card Manager's finding),
 * FR_NO_FILE otherwise -- both mean "absent" */
#define ABSENT(rc) ((rc) == -FR_NO_FILE || (rc) == -FR_NO_PATH)

int om_sdw_replace(const char *path, const uint8_t *data, uint32_t len, uint8_t *scratch2k, om_sdw_gap_fn gap,
                   om_sdw_stats *st)
{
    om_sdw_stats d;
    buf_src b;
    int rc, fd, exists;
    if (!st) { memset(&d, 0, sizeof d); st = &d; }
    if (TOO_LONG(path)) return done(st, OM_E_FIELD);
    /* 0: an interrupted replace finished first -- only when path is absent (fix round 1, review I1: so it never depends
     * on rename-onto-existing being refused), and any failure but "absent" stops the call before anything is written
     * (steps 1-2 would otherwise empty path and truncate the only good copy). s_rename copies both names
     * into its own aligned buffer. */
    om_yield();
    rc = s_stat(stage(scratch2k, path, 0), (s_stat_t *)scratch2k, 12);
    exists = rc >= 0;
    if (ABSENT(rc)) {
        om_yield();
        rc = s_rename(stage(scratch2k, path, 1), path);
        if (ABSENT(rc)) rc = 0;                         /* no interrupted replace either: a first save */
        else if (rc >= 0) exists = 1;                   /* the interrupted replace's line is path now */
    }
    if (rc < 0)
        return done(st, OM_SDW_ERR(rc));
    /* 1: path exists for the whole write, so "path absent" can only mean step 3 ran. Made only when step 0 found it
     * absent (the save swap's speed, 2026-11-24: each open walks the card from its root) */
    if (!exists) {
        om_yield();
        if ((fd = s_open(stage(scratch2k, path, 0), FA_WRITE | FA_OPEN_ALWAYS)) < 0)
            return done(st, OM_SDW_ERR(fd));
        if ((rc = s_close(fd)) < 0)
            return done(st, OM_SDW_ERR(rc));
    }
    /* 2 */
    b.p = data;
    b.left = len;
    if ((rc = write_src(path, 1, from_buf, &b, 1, scratch2k, gap, st)) != 0) {
        s_unlink(stage(scratch2k, path, 1));
        return done(st, rc);
    }
    /* 3, 4 */
    if (gap) gap();
    rc = s_unlink(stage(scratch2k, path, 0));
    if (rc < 0 && !ABSENT(rc))
        return done(st, OM_SDW_ERR(rc));
    if (gap) gap();
    rc = s_rename(stage(scratch2k, path, 1), path);
    return done(st, rc < 0 ? OM_SDW_ERR(rc) : 0);
}

static int read1(const char *path, int dotnew, uint8_t *buf, uint32_t cap, uint32_t *len, int head)
{
    uint32_t size, got = 0;
    int fd, r;
    om_yield();                  /* the reads only (the start-up's ACTIVE.TXT): a frame between path commands */
    if ((r = s_stat(stage(buf, path, dotnew), (s_stat_t *)buf, 12)) < 0)
        return ABSENT(r) ? OM_SDW_NOFILE : OM_SDW_ERR(r);
    size = ((s_stat_t *)buf)->size;
    if (head && size + 4 > cap) size = (cap - 4) & ~3u;   /* the first cap - 4 bytes (whole words) */
    if (size + 4 > cap)
        return OM_E_FULL;
    om_yield();
    if ((fd = s_open(stage(buf, path, dotnew), FA_READ | FA_OPEN_EXISTING)) < 0)
        return ABSENT(fd) ? OM_SDW_NOFILE : OM_SDW_ERR(fd);
    for (r = 0; got < size; got += (uint32_t)r) {
        uint32_t n = size - got > S_MAXBUF ? S_MAXBUF : size - got;
        om_yield();
        if ((r = s_read(fd, buf + got, (int)n)) <= 0) {
            r = r < 0 ? OM_SDW_ERR(r) : OM_E_SHORT;
            break;
        }
    }
    s_close(fd);
    if (got < size)
        return r;
    *len = size;
    return 0;
}

int om_sdw_read_file(const char *path, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    int rc;
    if (((uintptr_t)buf & 3) || cap < 260 || TOO_LONG(path))
        return OM_E_FIELD;
    if ((rc = read1(path, 0, buf, cap, len, 0)) == OM_SDW_NOFILE)
        rc = read1(path, 1, buf, cap, len, 0);
    return rc;
}

int om_sdw_read_head(const char *path, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    int rc;
    if (((uintptr_t)buf & 3) || cap < 260 || TOO_LONG(path))
        return OM_E_FIELD;
    if ((rc = read1(path, 0, buf, cap, len, 1)) == OM_SDW_NOFILE)
        rc = read1(path, 1, buf, cap, len, 1);
    return rc;
}
