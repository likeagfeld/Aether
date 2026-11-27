/* menu/host-tests/test_bootanim.c -- BOOTANIM.BIN (contract/SPEC.md §5.10): the reader, the byte-op decoder
 * (every op and both word paths against a naive reference, then the shipped asset (every frame's CRC-32 in the index was computed by tools/omsat/bootanim.py from the indexed
 * PNG it came from, and `bootanim.py check --frames` compares the pixels themselves), the 30 fps chain, a corrupt
 * stream, the ISO 9660 lookup, the pacer's event timing and the underrun fallback against a simulated 2x and 1x CD.
 * BSD-3-Clause. */
#include "th.h"
#include "bootanim.h"
#include "bootstream.h"
#include "isodir.h"
#include "om_fmt.h"

#define ASSET "../app/assets/BOOTANIM.BIN"

static uint8_t s_slots[2 * OM_BA_MAX_PIX];

static int decode_chain(const om_ba *a, const uint8_t *f, int evens_only, uint32_t *bad)
{
    om_ba_dec d;
    uint32_t i, off, len, crc;
    int rc;
    *bad = 0;
    om_ba_dec_init(&d, a, s_slots);
    for (i = 0; i < a->count; i++) {
        if (evens_only && (i & 1u)) continue;
        om_ba_entry(a, i, &off, &len, &crc);
        if ((rc = om_ba_decode(&d, a, i, f + off, len)) != OM_OK) return rc;
        if (om_crc32(0, d.pix, a->w * a->h) != crc) (*bad)++;
    }
    return OM_OK;
}

static void test_asset(void)
{
    uint32_t n, bad, i, off, len, crc, peak = 0, total = 0;
    uint8_t *f = th_load(ASSET, &n), *m;
    om_ba a;
    om_ba_dec d;
    CHECK_INT(om_ba_open(&a, f, om_ba_head_bytes(f, n), n), OM_OK);
    CHECK_INT(a.count, 215);
    CHECK_INT(a.w, 176);
    CHECK_INT(a.h, 127);
    CHECK_INT(a.chant_x100, 13710);
    CHECK_INT(om_ba_chant_frame(&a), 137);
    CHECK_INT(om_ba_color(&a, 0), 0);                              /* index 0 is black */
    CHECK_INT(decode_chain(&a, f, 0, &bad), OM_OK);                /* 60 fps: all 215 frames */
    CHECK_INT(bad, 0);
    CHECK_INT(decode_chain(&a, f, 1, &bad), OM_OK);                /* 30 fps fallback: the even chain alone */
    CHECK_INT(bad, 0);
    for (i = 0; i < a.count; i++) {
        om_ba_entry(&a, i, &off, &len, &crc);
        total += len;
        if (len > peak) peak = len;
    }
    CHECK(total / a.count * 60u < 250u * 1024u);                   /* average well inside a 2x CD (300 KB/s) */
    printf("test_bootanim: %u frames, %u B of frames, average %u KB/s, per-frame peak %u B = %u KB/s\n", a.count, total,
           total / a.count * 60u / 1024u, peak, peak * 60u / 1024u);
    om_ba_dec_init(&d, &a, s_slots);                        /* out of chain order: refused, not decoded */
    om_ba_entry(&a, 3, &off, &len, &crc);
    CHECK_INT(om_ba_decode(&d, &a, 3, f + off, len), OM_BA_E_REF);
    m = th_dup(f, n);                                              /* corrupt every 7th byte of frame 0 in turn */
    om_ba_entry(&a, 0, &off, &len, &crc);
    for (i = 0; i < len; i += 7u) {
        int rc;
        m[off + i] ^= 0x5Au;
        om_ba_dec_init(&d, &a, s_slots);
        rc = om_ba_decode(&d, &a, 0, m + off, len);
        CHECK(rc != OM_OK || om_crc32(0, d.pix, a.w * a.h) != crc);
        m[off + i] ^= 0x5Au;
    }
    om_ba_dec_init(&d, &a, s_slots);                        /* truncated input */
    CHECK(om_ba_decode(&d, &a, 0, f + off, len / 2u) != OM_OK);
    m[0x10] ^= 1u;                                                 /* header field: CRC / range refusal */
    CHECK(om_ba_open(&a, m, om_ba_head_bytes(m, n), n) != OM_OK);
    m[0x10] ^= 1u;
    m[OM_BA_INDEX_OFF + 5] ^= 1u;                                  /* index byte: table CRC */
    CHECK_INT(om_ba_open(&a, m, om_ba_head_bytes(m, n), n), OM_E_CRC);
    free(m);
    free(f);
}

/* The naive reference for om_ba_ops: one byte at a time, no word paths. */
static int ref_ops(uint8_t *buf, uint32_t n, const uint8_t *s, uint32_t len)
{
    uint32_t p = 0, pos = 0;
    while (pos < n) {
        uint32_t b = s[p++], v = b & 63u, ln, k, d;
        ln = v < 62u ? v + 1u : v == 62u ? 63u + s[p++] : ((uint32_t)s[p] << 8 | s[p + 1]);
        if (v == 63u) p += 2;
        switch (b >> 6) {
        case 0: break;
        case 1: for (k = 0; k < ln; k++) buf[pos + k] = s[p++]; break;
        case 2:
            d = s[p++];
            if (d & 0x80u) d = (((d & 0x7Fu) << 8) | s[p++]) + 129u;
            else d++;
            for (k = 0; k < ln; k++) buf[pos + k] = buf[pos + k - d];
            break;
        default:
            for (k = 0; k < ln; k++) buf[pos + k] = s[p];
            p++;
            break;
        }
        pos += ln;
    }
    return p == len ? 0 : -1;
}

static uint32_t s_rng = 12345u;
static uint32_t rnd(uint32_t m) { s_rng = s_rng * 1103515245u + 12345u; return (s_rng >> 8) % m; }

static uint32_t put_len(uint8_t *o, uint32_t op, uint32_t n)
{
    if (n <= 62u) { o[0] = (uint8_t)(op << 6 | (n - 1u)); return 1; }
    if (n <= 318u) { o[0] = (uint8_t)(op << 6 | 62u); o[1] = (uint8_t)(n - 63u); return 2; }
    o[0] = (uint8_t)(op << 6 | 63u); o[1] = (uint8_t)(n >> 8); o[2] = (uint8_t)n; return 3;
}

static void test_ops(void)
{
    static uint8_t ops[65536], a[4000], b[4000];
    static const uint32_t dists[] = { 1, 2, 3, 4, 5, 6, 7, 8, 12, 175, 176, 177, 352, 500 };
    uint32_t round, bad = 0, n = 3001u;                            /* odd size: unaligned tails */
    for (round = 0; round < 400u; round++) {
        uint32_t len = 0, pos = 0, i;
        for (i = 0; i < n; i++) a[i] = b[i] = (uint8_t)rnd(256);
        while (pos < n) {
            uint32_t op = rnd(4), ln = 1u + (rnd(5) == 0 ? rnd(400) : rnd(40)), d;
            if (ln > n - pos) ln = n - pos;
            if (op == 2u) {
                d = dists[rnd(sizeof dists / sizeof dists[0])];
                if (d > pos) op = 1u;
                else {
                    len += put_len(ops + len, 2, ln);
                    if (d <= 128u) ops[len++] = (uint8_t)(d - 1u);
                    else { ops[len++] = (uint8_t)(0x80u | ((d - 129u) >> 8)); ops[len++] = (uint8_t)(d - 129u); }
                }
            }
            if (op == 0u) len += put_len(ops + len, 0, ln);
            if (op == 1u) { len += put_len(ops + len, 1, ln); for (i = 0; i < ln; i++) ops[len++] = (uint8_t)rnd(256); }
            if (op == 3u) { len += put_len(ops + len, 3, ln); ops[len++] = (uint8_t)rnd(256); }
            pos += ln;
        }
        if (ref_ops(a + 1, n, ops, len) != 0) bad++;               /* +1: the buffer itself unaligned */
        if (om_ba_ops(b + 1, n, ops, len) != OM_OK) bad++;
        if (memcmp(a, b, sizeof a)) bad++;
    }
    CHECK_INT(bad, 0);
    ops[0] = 0x80u; ops[1] = 0x00u;                                /* COPY 1 from 1 back at pixel 0 */
    CHECK_INT(om_ba_ops(b, 8, ops, 2), OM_BA_E_DIST);
    ops[0] = 0x07u;                                                /* SKIP 8 then a stray byte */
    CHECK_INT(om_ba_ops(b, 8, ops, 2), OM_BA_E_TRAIL);
    ops[0] = 0x08u;                                                /* SKIP 9 > 8 pixels */
    CHECK_INT(om_ba_ops(b, 8, ops, 1), OM_BA_E_OPS);
    ops[0] = 0x47u;                                                /* LIT 8 with 3 bytes of data */
    CHECK_INT(om_ba_ops(b, 8, ops, 4), OM_BA_E_OPS);
    CHECK_INT(om_ba_ops(b, 8, ops, 0), OM_BA_E_OPS);               /* no ops at all */
}

static void test_fixture(void)
{
    uint32_t n, bad;
    uint8_t *f = th_load(FIX("tiny.bootanim.bin"), &n);
    om_ba a;
    CHECK_INT(om_ba_open(&a, f, om_ba_head_bytes(f, n), n), OM_OK);
    CHECK_INT(a.count, 6);
    CHECK_INT(a.w, 16);
    CHECK_INT(a.h, 8);
    CHECK_INT(om_ba_chant_frame(&a), 3);
    CHECK_INT(decode_chain(&a, f, 0, &bad), OM_OK);
    CHECK_INT(bad, 0);
    CHECK(om_ba_open(&a, f, OM_BA_INDEX_OFF - 1u, n) != OM_OK);    /* head not read yet */
    memcpy(f, "OMSATXXX", 8);
    CHECK_INT(om_ba_open(&a, f, om_ba_head_bytes(f, n), n), OM_E_MAGIC);
    free(f);
}

static void put_rec(uint8_t *r, const char *name, uint32_t lba, uint32_t size, int dir)
{
    uint32_t nl = (uint32_t)strlen(name), rl = 33u + nl + ((33u + nl) & 1u);
    memset(r, 0, rl);
    r[0] = (uint8_t)rl;
    om_put32(r + 6, lba);
    om_put32(r + 14, size);
    r[25] = dir ? 2u : 0u;
    r[32] = (uint8_t)nl;
    memcpy(r + 33, name, nl);
}

static void test_isodir(void)
{
    static uint8_t pvd[2048], dir[4096];
    uint32_t lba = 0, size = 0, p = 0;
    pvd[0] = 1; memcpy(pvd + 1, "CD001", 5);
    put_rec(pvd + 156, "\0", 20, 4096, 1);
    CHECK_INT(om_iso_root(pvd, &lba, &size), 0);
    CHECK_INT(lba, 20);
    CHECK_INT(size, 4096);
    put_rec(dir + p, "\0", 20, 4096, 1); p += dir[p];
    put_rec(dir + p, "\1", 20, 4096, 1); p += dir[p];
    put_rec(dir + p, "0.BIN;1", 30, 70000, 0); p += dir[p];
    put_rec(dir + p, "BOOTANIM", 99, 1, 1); p += dir[p];            /* a directory of the same stem: skipped */
    put_rec(dir + 2048, "BOOTANIM.BIN;1", 77, 609663, 0);          /* in the second sector, after zero padding */
    CHECK_INT(om_iso_find(dir, sizeof dir, "BOOTANIM.BIN", &lba, &size), 0);
    CHECK_INT(lba, 77);
    CHECK_INT(size, 609663);
    CHECK_INT(om_iso_find(dir, sizeof dir, "0.BIN", &lba, &size), 0);
    CHECK_INT(lba, 30);
    CHECK_INT(om_iso_find(dir, sizeof dir, "BOOTANIM.BI", &lba, &size), OM_ISO_E_NONE);
    CHECK_INT(om_iso_find(dir, 2048, "BOOTANIM.BIN", &lba, &size), OM_ISO_E_NONE);
    pvd[1] = 'X';
    CHECK_INT(om_iso_root(pvd, &lba, &size), OM_ISO_E_PVD);
    dir[2048] = 20;                                                /* a record shorter than its fixed part */
    CHECK_INT(om_iso_find(dir, sizeof dir, "BOOTANIM.BIN", &lba, &size), OM_ISO_E_ROOT);
}

static void test_pacer_60(void)
{
    om_ba_pacer p;
    uint32_t v, shows = 0, chant_frame = OM_BA_NONE;
    unsigned ev, all = 0;
    om_ba_pacer_init(&p, 215, 137);
    for (v = 0; v < 400 && !p.done; v++) {
        uint32_t w = om_ba_pacer_want(&p);
        CHECK_INT(w, v);                                           /* one new frame per V-blank */
        ev = om_ba_pacer_tick(&p, 1);
        all |= ev;
        if (ev & OM_BA_EV_SHOW) shows++;
        if (ev & OM_BA_EV_CHANT) chant_frame = w;
    }
    CHECK_INT(shows, 215);
    CHECK_INT(chant_frame, 137);
    CHECK_INT(p.chant_vbl, 137);                                   /* exactly the V-blank frame 137 is shown */
    CHECK_INT(p.done_vbl, 214);
    CHECK_INT(p.underruns, 0);
    CHECK_INT(p.fallback_frame, OM_BA_NONE);
    CHECK(!(all & OM_BA_EV_FALLBACK));
    CHECK_INT(om_ba_pacer_tick(&p, 1), 0);                         /* nothing after the end */
}

static void test_pacer_fallback(void)
{
    om_ba_pacer p;
    uint32_t v, last = 0, shown = 0, hold_run = 0, max_hold = 0, chant_frame = OM_BA_NONE;
    om_ba_pacer_init(&p, 215, 137);
    for (v = 0; v < 600 && !p.done; v++) {
        uint32_t w = om_ba_pacer_want(&p);
        int ready = !(v == 51);                                    /* one late frame (51) */
        unsigned ev = om_ba_pacer_tick(&p, ready);
        if (v == 51) CHECK(ev & OM_BA_EV_FALLBACK);
        if (ev & OM_BA_EV_SHOW) {
            if (shown) CHECK(w > last);
            if (p.half && w != 214) CHECK_INT(w & 1u, 0);          /* even frames only after the fallback */
            last = w; shown++; hold_run = 0;
        } else if (++hold_run > max_hold) max_hold = hold_run;
        if (ev & OM_BA_EV_CHANT) chant_frame = w;
    }
    CHECK(p.done);
    CHECK_INT(p.fallback_frame, 51);
    CHECK_INT(p.underruns, 1);
    CHECK_INT(max_hold, 1);                                        /* never more than one V-blank without news */
    CHECK_INT(last, 214);
    CHECK_INT(chant_frame, 138);                                   /* 137 is odd: the first frame >= 137 shown */
    CHECK_INT(p.chant_vbl, 52 + (138 - 52));                       /* 52 = 52 at v52; then 2 V-blanks per 2 frames */
    CHECK_INT(shown, 51 + (214 - 52) / 2 + 1);
}

static void test_pacer_slow(void)
{
    om_ba_pacer p;
    uint32_t v, shown = 0;
    om_ba_pacer_init(&p, 215, 137);
    for (v = 0; v < 40; v++) om_ba_pacer_tick(&p, 1);             /* frames 0..39 */
    CHECK_INT(om_ba_pacer_slow(&p), OM_BA_EV_FALLBACK);            /* next was 40 (even): stays 40 */
    CHECK_INT(p.fallback_frame, 40);
    CHECK_INT(p.reason, 2);
    CHECK_INT(om_ba_pacer_want(&p), 40);
    CHECK_INT(om_ba_pacer_slow(&p), 0);                            /* once */
    for (v = 0; v < 600 && !p.done; v++)
        if (om_ba_pacer_tick(&p, 1) & OM_BA_EV_SHOW) { CHECK_INT(p.shown & 1u, 0); shown++; }
    CHECK(p.done);
    CHECK_INT(shown, (214 - 40) / 2 + 1);
    om_ba_pacer_init(&p, 215, 137);
    for (v = 0; v < 41; v++) om_ba_pacer_tick(&p, 1);             /* next = 41 (odd) -> 42 */
    om_ba_pacer_slow(&p);
    CHECK_INT(om_ba_pacer_want(&p), 42);
}

/* The player's readiness rule against a simulated drive: sectors arrive at `spf` sectors per 100 V-blanks after a
 * pre-roll; a frame is ready once every byte up to its end has arrived. 2x CD = 150 sectors/s = 250 per 100 V-blanks. */
static unsigned simulate(const om_ba *a, uint32_t spf100, uint32_t preroll, om_ba_pacer *p)
{
    uint32_t v, got100 = preroll * 100u, all = 0;
    om_ba_pacer_init(p, a->count, om_ba_chant_frame(a));
    for (v = 0; v < 3000 && !p->done; v++) {
        uint32_t w = om_ba_pacer_want(p), off, len, crc;
        int ready = 1;
        got100 += spf100;
        if (w != OM_BA_NONE) {
            om_ba_entry(a, w, &off, &len, &crc);
            ready = (uint64_t)(got100 / 100u) * 2048u >= off + len;
        }
        all |= om_ba_pacer_tick(p, ready);
    }
    return all;
}

static void test_simulated_cd(void)
{
    uint32_t n;
    uint8_t *f = th_load(ASSET, &n);
    om_ba a;
    om_ba_pacer p;
    unsigned ev;
    CHECK_INT(om_ba_open(&a, f, om_ba_head_bytes(f, n), n), OM_OK);
    ev = simulate(&a, 250, 16, &p);                                /* 2x, 32 KB pre-roll: no fallback */
    CHECK(p.done);
    CHECK(!(ev & OM_BA_EV_FALLBACK));
    CHECK_INT(p.underruns, 0);
    ev = simulate(&a, 125, 16, &p);                                /* 1x: falls back once, then keeps up */
    CHECK(p.done);
    CHECK(ev & OM_BA_EV_FALLBACK);
    printf("test_bootanim: 1x CD -> fallback at frame %u, %u underrun V-blanks, done at V-blank %u\n",
           p.fallback_frame, p.underruns, p.done_vbl);
    free(f);
}

/* A fake drive for om_bs: `rate` sectors arrive per V-blank from `pos`; take() can be told to fail once after it has
 * already deleted the sectors (GetdelSctData then DataReady failing); `dead` stops delivery. */
typedef struct { uint32_t nsec, pos, avail, fail_at, takes, dead, count_err, starts; } fake_cd;
static uint8_t file_byte(uint32_t i) { return (uint8_t)(i * 7u + (i >> 11) * 13u); }
static int32_t fk_count(void *c) { fake_cd *f = c; return f->count_err ? -46 : (int32_t)f->avail; }
static int fk_take(void *c, uint8_t *dst, uint32_t n)
{
    fake_cd *f = c;
    uint32_t i;
    if (n > f->avail) return -1;
    f->avail -= n;
    if (++f->takes == f->fail_at) { f->pos += n; return -46; }      /* deleted, then the transfer failed */
    for (i = 0; i < n * OM_BS_SECTOR; i++) dst[i] = file_byte(f->pos * OM_BS_SECTOR + i);
    f->pos += n;
    return 0;
}
static int fk_start(void *c, uint32_t first, uint32_t n) { fake_cd *f = c; (void)n; f->pos = first; f->avail = 0; f->starts++; return 0; }
static void fk_tick(fake_cd *f) { if (!f->dead && f->pos + f->avail < f->nsec) f->avail++; }

static int run_stream(fake_cd *f, om_bs *s, uint8_t *ring, uint32_t vmax, uint32_t *at)
{
    om_bs_io io = { fk_count, fk_take, fk_start, 0, 0, 0 };
    uint32_t v;
    int32_t r = 0;
    io.ctx = f;
    om_bs_init(s, &io, ring, 64u * OM_BS_SECTOR, f->nsec, 0);
    for (v = 1; v < vmax && !om_bs_done(s); v++) {
        fk_tick(f);
        fk_tick(f);                                              /* 2 sectors per V-blank */
        if ((r = om_bs_pump(s, v, 4)) < 0) break;
    }
    *at = v;
    return r < 0 ? (int)r : 0;
}

static void test_stream(void)
{
    static uint8_t ring[64u * OM_BS_SECTOR];
    fake_cd f;
    om_bs s;
    uint32_t at, i, bad = 0;
    memset(&f, 0, sizeof f); f.nsec = 40;                        /* healthy */
    CHECK_INT(run_stream(&f, &s, ring, 1000, &at), 0);
    CHECK(om_bs_done(&s));
    CHECK_INT(s.restarts, 0);
    memset(&f, 0, sizeof f); f.nsec = 40; f.fail_at = 5;         /* I4: a take fails after deleting its sectors */
    memset(ring, 0, sizeof ring);
    CHECK_INT(run_stream(&f, &s, ring, 1000, &at), 0);
    CHECK(om_bs_done(&s));
    CHECK_INT(s.restarts, 1);
    for (i = 0; i < 40u * OM_BS_SECTOR; i++) bad += ring[i] != file_byte(i);
    CHECK_INT(bad, 0);                                           /* the ring is exact: the restart re-read them */
    memset(&f, 0, sizeof f); f.nsec = 40;                        /* the drive dies mid-stream */
    {
        om_bs_io io = { fk_count, fk_take, fk_start, 0, 0, 0 };
        uint32_t v;
        int32_t r = 0;
        io.ctx = &f;
        om_bs_init(&s, &io, ring, 64u * OM_BS_SECTOR, f.nsec, 0);
        for (v = 1; v < 10000; v++) {
            if (v == 5) f.dead = 1;
            fk_tick(&f);
            if ((r = om_bs_pump(&s, v, 4)) < 0) break;
        }
        CHECK_INT(r, OM_BS_E_STALL);
        CHECK(v <= 5u + 61u);                                    /* named skip within about 1 s of the last sector */
        CHECK_INT(s.restarts, 1);                                /* one restart at 0.5 s */
    }
    memset(&f, 0, sizeof f); f.nsec = 40; f.count_err = 1;       /* the CD block never answers */
    CHECK_INT(run_stream(&f, &s, ring, 1000, &at), OM_BS_E_STALL);
    CHECK(at <= 5u);
    memset(&f, 0, sizeof f); f.nsec = 200;                       /* ring full, consumer idle: not a stall */
    {
        om_bs_io io = { fk_count, fk_take, fk_start, 0, 0, 0 };
        uint32_t v;
        int32_t r = 0;
        io.ctx = &f;
        om_bs_init(&s, &io, ring, 64u * OM_BS_SECTOR, f.nsec, 0);
        for (v = 1; v < 400; v++) { fk_tick(&f); if ((r = om_bs_pump(&s, v, 4)) < 0) break; }
        CHECK_INT(r, 0);
        CHECK_INT(s.got, 64u * OM_BS_SECTOR);
        s.keep = s.got;                                          /* the consumer catches up: streaming resumes */
        for (; v < 1000 && !om_bs_done(&s); v++) { fk_tick(&f); if ((r = om_bs_pump(&s, v, 4)) < 0) break; s.keep = s.got; }
        CHECK(om_bs_done(&s));
        CHECK_INT(s.restarts, 0);
    }
}

/* A CD block that keeps what the hardware may keep (HW bootanim report, 2026-11-19): partition 0 is a queue of file
 * sectors (cap 200, the CD block's buffer). A restart (start) drops the queue (ResetSelector), but the OLD play goes
 * on for `stale` more sectors from `old` before the new play's first sector arrives -- either ahead of the restart
 * point (old = where the drive had got to) or the very same sectors again (old = first: a drive that only paused). */
#define QCAP 200u
static const uint8_t *s_qfile;                                   /* NULL: file_byte(); else this file */
static uint32_t s_qfile_n;
typedef struct {
    uint32_t nsec, pos, end, q[QCAP], qh, qn, infos;
    uint32_t fad0, stale_n, stale_at, stale_dup, old, starts, count_err_at, counts, dead_from, dead_to, vb;
    uint32_t restart_first[8], restart_room[8];
    om_bs *s;
} qcd;
static int32_t q_count(void *c)
{
    qcd *f = c;
    if (++f->counts == f->count_err_at) return -46;
    return (int32_t)f->qn;
}
static int q_take(void *c, uint8_t *dst, uint32_t n)
{
    qcd *f = c;
    uint32_t i, k;
    if (n > f->qn) return -1;
    for (k = 0; k < n; k++) {
        uint32_t sec = f->q[f->qh];
        for (i = 0; i < OM_BS_SECTOR; i++) {
            uint32_t at = sec * OM_BS_SECTOR + i;
            dst[k * OM_BS_SECTOR + i] = !s_qfile ? file_byte(at) : at < s_qfile_n ? s_qfile[at] : 0;
        }
        f->qh = (f->qh + 1u) % QCAP;
        f->qn--;
    }
    return 0;
}
static int32_t q_info(void *c, uint32_t spos)
{
    qcd *f = c;
    f->infos++;
    return spos < f->qn ? (int32_t)(f->fad0 + f->q[(f->qh + spos) % QCAP]) : -46;
}
static int q_drop(void *c, uint32_t n)
{
    qcd *f = c;
    if (n > f->qn) return -1;
    f->qh = (f->qh + n) % QCAP;
    f->qn -= n;
    return 0;
}
static int q_start(void *c, uint32_t first, uint32_t n)
{
    qcd *f = c;
    uint32_t st = f->starts++;
    if (st < 8u && f->s) {
        f->restart_first[st] = first;
        f->restart_room[st] = first * OM_BS_SECTOR - (f->s->keep & ~(OM_BS_SECTOR - 1u));
    }
    f->qh = f->qn = 0;                                           /* ResetSelector: the partition is empty */
    if (st == f->stale_at && f->stale_n) {                       /* the old play is still feeding filter 0 */
        f->old = f->stale_dup ? first : f->pos;
    } else f->stale_n = 0;
    f->pos = first;
    f->end = first + n;
    return 0;
}
static void q_tick(qcd *f)
{
    uint32_t sec;
    f->vb++;
    if (f->vb >= f->dead_from && f->vb < f->dead_to) return;
    if (f->qn >= QCAP) return;                                   /* buffer full: the drive waits */
    if (f->stale_n && f->starts > f->stale_at) { sec = f->old++; f->stale_n--; }
    else if (f->pos < f->end) sec = f->pos++;
    else return;
    f->q[(f->qh + f->qn) % QCAP] = sec;
    f->qn++;
}

/* 120 sectors through a 64-sector ring, consumed as they come (keep = got), 2 sectors per V-blank. */
static uint32_t run_q(qcd *f, om_bs *s, uint8_t *ring, const om_bs_io *io0, uint32_t *bad)
{
    om_bs_io io = *io0;
    uint32_t v, i;
    io.ctx = f;
    f->s = s;
    om_bs_init(s, &io, ring, 64u * OM_BS_SECTOR, f->nsec, 0);
    s->base = f->fad0;
    q_start(f, 0, f->nsec);
    f->starts = 0;
    *bad = 0;
    for (v = 1; v < 2000 && !om_bs_done(s); v++) {
        uint32_t before = s->got;
        q_tick(f);
        q_tick(f);
        if (om_bs_pump(s, v, 4) < 0) break;
        for (i = before; i < s->got; i++) *bad += ring[i & (64u * OM_BS_SECTOR - 1u)] != file_byte(i);
        s->keep = s->got;
    }
    return v;
}

static void test_stream_stale(void)
{
    static uint8_t ring[64u * OM_BS_SECTOR];
    static const om_bs_io old = { q_count, q_take, q_start, 0, 0, 0 };          /* no sector check: as before */
    static const om_bs_io io = { q_count, q_take, q_start, 0, q_info, q_drop };
    qcd f;
    om_bs s;
    uint32_t bad, pass;
    memset(&f, 0, sizeof f); f.nsec = 120;                       /* healthy */
    run_q(&f, &s, ring, &io, &bad);
    CHECK(om_bs_done(&s)); CHECK_INT(bad, 0); CHECK_INT(s.restarts, 0); CHECK_INT(s.dropped, 0);
    CHECK_INT(f.infos, 0);                                       /* no restart: no extra CD-block command per take */
    for (pass = 0; pass < 2; pass++) {                           /* pass 0: the bug, pinned; pass 1: the fix */
        const om_bs_io *p = pass ? &io : &old;
        /* (a) a CD-block error mid-stream while sectors wait; the old play carries on from where the drive had got to */
        memset(&f, 0, sizeof f); f.nsec = 120; f.count_err_at = 25; f.stale_n = 6; f.fad0 = 150u + 1000u;
        run_q(&f, &s, ring, p, &bad);
        printf("test_bootanim: stale-ahead restart, %s: done %d, restarts %u, dropped %u, bad bytes %u\n",
               pass ? "checked" : "unchecked", om_bs_done(&s), s.restarts, s.dropped, bad);
        CHECK(om_bs_done(&s)); CHECK_INT(s.restarts, 1); CHECK_INT(s.why, OM_BS_WHY_COUNT);
        if (pass) { CHECK_INT(bad, 0); CHECK_INT(s.dropped, 6); } else CHECK(bad > 0);
        /* (b) the drive pauses 0.6 s, the stall restart fires, then the old play resumes with the same sectors */
        memset(&f, 0, sizeof f); f.nsec = 120; f.stale_n = 6; f.stale_dup = 1;
        f.dead_from = 20; f.dead_to = 92;                        /* q_tick runs twice a V-blank */
        run_q(&f, &s, ring, p, &bad);
        printf("test_bootanim: duplicate-run restart, %s: done %d, restarts %u, dropped %u, bad bytes %u\n",
               pass ? "checked" : "unchecked", om_bs_done(&s), s.restarts, s.dropped, bad);
        CHECK(om_bs_done(&s)); CHECK_INT(s.restarts, 1); CHECK_INT(s.why, OM_BS_WHY_STALL);
        CHECK_INT(s.why_sector, 19);                             /* the drive went quiet after 19 sectors */
        if (pass) { CHECK_INT(bad, 0); CHECK_INT(s.dropped, 6); } else CHECK(bad > 0);
        CHECK(f.restart_room[0] <= 64u * OM_BS_SECTOR);          /* a restart never starts beyond what the ring holds */
    }
    {   /* om_bs_seek: back to the sector of a byte, keep = that byte, the play restarted there; not a restart */
        memset(&f, 0, sizeof f); f.nsec = 120;
        run_q(&f, &s, ring, &io, &bad);
        f.s = &s;
        CHECK_INT(om_bs_seek(&s, 40u * OM_BS_SECTOR + 100u, 5000), 0);
        CHECK_INT(s.got, 40u * OM_BS_SECTOR); CHECK_INT(s.keep, 40u * OM_BS_SECTOR + 100u);
        CHECK_INT(f.pos, 40); CHECK_INT(f.end, 120); CHECK_INT(s.seeks, 1); CHECK_INT(s.restarts, 0);
        CHECK(!om_bs_done(&s));
    }
}

/* The real asset through the stream and the decoder, as bootplay.c does it: a 128 KB ring, the drive at 2 sectors per
 * V-blank, frames decoded in order as soon as they are in (wrap linearised), keep = the end of the last frame. */
static int stream_asset(const om_bs_io *io0, qcd *f, uint32_t *decoded, uint32_t *crc_ok)
{
    static uint8_t ring[64u * OM_BS_SECTOR], lin[OM_BA_MAX_FRAME];
    uint32_t n, v, i = 0;
    uint8_t *file = th_load(ASSET, &n);
    om_ba a;
    om_ba_dec d;
    om_bs s;
    om_bs_io io = *io0;
    int rc = 0;
    s_qfile = file; s_qfile_n = n;
    CHECK_INT(om_ba_open(&a, file, om_ba_head_bytes(file, n), n), OM_OK);
    f->nsec = (n + OM_BS_SECTOR - 1u) / OM_BS_SECTOR;
    io.ctx = f;
    f->s = &s;
    om_bs_init(&s, &io, ring, sizeof ring, f->nsec, 0);
    q_start(f, 0, f->nsec);
    f->starts = 0;
    s.keep = a.data_off;
    om_ba_dec_init(&d, &a, s_slots);
    *crc_ok = 0;
    for (v = 1; v < 5000 && i < a.count; v++) {
        uint32_t off, len, crc, pos;
        const uint8_t *src;
        q_tick(f); q_tick(f);
        if (om_bs_pump(&s, v, 4) < 0) { rc = OM_BS_E_STALL; break; }
        om_ba_entry(&a, i, &off, &len, &crc);
        if (s.got < off + len) continue;
        pos = off & (sizeof ring - 1u);
        src = ring + pos;
        if (pos + len > sizeof ring) {
            memcpy(lin, ring + pos, sizeof ring - pos);
            memcpy(lin + (sizeof ring - pos), ring, len - (sizeof ring - pos));
            src = lin;
        }
        if ((rc = om_ba_decode(&d, &a, i, src, len)) != OM_OK) break;
        *crc_ok += om_crc32(0, d.pix, a.w * a.h) == crc;
        s.keep = off + len;
        i++;
    }
    *decoded = i;
    s_qfile = 0;
    free(file);
    return rc;
}

static void test_stream_asset_restart(void)
{
    static const om_bs_io io = { q_count, q_take, q_start, 0, q_info, q_drop };
    qcd f;
    uint32_t dec, ok;
    int rc;
    memset(&f, 0, sizeof f);                                     /* healthy: every frame, every CRC */
    CHECK_INT(stream_asset(&io, &f, &dec, &ok), OM_OK);
    CHECK_INT(dec, 215); CHECK_INT(ok, 215);
    memset(&f, 0, sizeof f); f.count_err_at = 60; f.stale_n = 12;   /* a restart near frame 40, stale sectors after it */
    rc = stream_asset(&io, &f, &dec, &ok);
    printf("test_bootanim: asset, restart with stale sectors: rc %d after %u frames, %u CRCs good\n", rc, dec, ok);
    CHECK_INT(rc, OM_OK); CHECK_INT(dec, 215); CHECK_INT(ok, 215);
    memset(&f, 0, sizeof f); f.dead_from = 120; f.dead_to = 190; f.stale_n = 12; f.stale_dup = 1;
    rc = stream_asset(&io, &f, &dec, &ok);                      /* a 0.6 s drive pause, then the same sectors twice */
    printf("test_bootanim: asset, stall restart with a repeated run: rc %d after %u frames, %u CRCs good\n", rc, dec, ok);
    CHECK_INT(rc, OM_OK); CHECK_INT(dec, 215); CHECK_INT(ok, 215);
}

/* Re-sync after a frame that would not decode (bootplay.c): truncated data fails; an odd frame needs nothing (E is
 * intact, the next even frame decodes); an even frame leaves E half-written, and om_ba_dec_restore puts ref(i) back
 * from a copy (bootplay's shadow of the slot that frame was uploaded to). Every later CRC must still match. */
static void test_restore(void)
{
    uint32_t n, i, off, len, crc, ok = 0, npix;
    uint8_t *f = th_load(ASSET, &n);
    static uint8_t keep2[OM_BA_MAX_PIX];                          /* what the shadow of frame i-2 would hold */
    static uint8_t keep1[OM_BA_MAX_PIX];
    om_ba a;
    om_ba_dec d;
    CHECK_INT(om_ba_open(&a, f, om_ba_head_bytes(f, n), n), OM_OK);
    npix = a.w * a.h;
    om_ba_dec_init(&d, &a, s_slots);
    for (i = 0; i < 76u; i++) {                                    /* frames 0..75 */
        om_ba_entry(&a, i, &off, &len, &crc);
        CHECK_INT(om_ba_decode(&d, &a, i, f + off, len), OM_OK);
        memcpy(keep2, keep1, npix);
        memcpy(keep1, d.pix, npix);
    }
    om_ba_entry(&a, 76, &off, &len, &crc);                          /* 76 (even) on truncated data: fails mid-way */
    CHECK(om_ba_decode(&d, &a, 76, f + off, len - 1u) < 0);
    CHECK_INT(om_ba_decode(&d, &a, 76, f + off, len), OM_BA_E_REF); /* E is no frame: a plain retry is refused */
    om_ba_dec_restore(&d, &a, 74, keep2);                           /* the shadow holding 74 */
    for (i = 76; i < a.count; i++) {
        om_ba_entry(&a, i, &off, &len, &crc);
        if (i == 77u) {                                            /* 77 (odd) fails too: skip it, E still holds 76 */
            CHECK(om_ba_decode(&d, &a, 77, f + off, 1u) < 0);
            continue;
        }
        CHECK_INT(om_ba_decode(&d, &a, i, f + off, len), OM_OK);
        ok += om_crc32(0, d.pix, npix) == crc;
    }
    CHECK_INT(ok, a.count - 77u);
    om_ba_dec_init(&d, &a, s_slots);                                /* frame 0 fails: back to black, then it decodes */
    om_ba_entry(&a, 0, &off, &len, &crc);
    CHECK(om_ba_decode(&d, &a, 0, f + off, len - 1u) < 0);
    om_ba_dec_restore(&d, &a, -2, 0);
    CHECK_INT(om_ba_decode(&d, &a, 0, f + off, len), OM_OK);
    CHECK_INT(om_crc32(0, d.pix, npix), crc);
    free(f);
}

int main(void)
{
    test_restore();
    test_stream();
    test_stream_stale();
    test_stream_asset_restart();
    test_ops();
    test_asset();
    test_fixture();
    test_isodir();
    test_pacer_60();
    test_pacer_fallback();
    test_pacer_slow();
    test_simulated_cd();
    return th_done("test_bootanim");
}
