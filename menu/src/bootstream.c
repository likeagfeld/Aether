/* menu/src/bootstream.c -- see bootstream.h. BSD-3-Clause. */
#include "bootstream.h"

void om_bs_init(om_bs *s, const om_bs_io *io, uint8_t *ring, uint32_t ring_size, uint32_t nsec, uint32_t now)
{
    s->io = *io;
    s->ring = ring;
    s->ring_size = ring_size;
    s->nsec = nsec;
    s->got = s->keep = 0;
    s->last_rx = now;
    s->restarts = s->io_errors = 0;
    s->stall_vb = 30u;                          /* 0.5 s: restart once */
    s->abort_vb = 60u;                          /* 1 s without a sector: give up */
    s->max_restarts = 3u;
    s->stall_restarted = s->check = 0;
    s->err = 0;
    s->base = s->dropped = s->seeks = s->why = s->why_sector = s->why_vb = 0;
    s->why_err = 0;
}

int om_bs_done(const om_bs *s) { return s->got >= s->nsec * OM_BS_SECTOR; }

static uint32_t room(const om_bs *s)
{
    return s->ring_size - (s->got - (s->keep & ~(OM_BS_SECTOR - 1u)));
}

static int restart(om_bs *s, uint32_t why, uint32_t now)
{
    uint32_t first = s->got / OM_BS_SECTOR;
    if (!s->why) { s->why = why; s->why_sector = first; s->why_vb = now; s->why_err = why == OM_BS_WHY_STALL ? 0 : s->err; }
    if (++s->restarts > s->max_restarts) return OM_BS_E_STALL;
    s->check = 1;                                        /* from here sectors of the old play may arrive */
    if ((s->err = s->io.start(s->io.ctx, first, s->nsec - first)) < 0) s->io_errors++;
    return 0;
}

int om_bs_seek(om_bs *s, uint32_t byte, uint32_t now)
{
    uint32_t first = byte / OM_BS_SECTOR;
    s->got = first * OM_BS_SECTOR;
    s->keep = byte;
    s->last_rx = now;
    s->stall_restarted = 0;
    s->seeks++;
    s->check = 1;
    if ((s->err = s->io.start(s->io.ctx, first, s->nsec - first)) < 0) s->io_errors++;
    return s->err;
}

/* How many of the first `lim` waiting sectors (the first known to be at address h) continue it: h, h+1, ... A run
 * once broken stays broken (a later play starts elsewhere), so a binary search finds its end: <= 8 info calls. */
static uint32_t run_len(om_bs *s, uint32_t h, uint32_t lim)
{
    uint32_t lo = 1, hi = lim;
    while (lo < hi) {
        uint32_t mid = (lo + hi + 1u) / 2u;
        if (s->io.info(s->io.ctx, mid - 1u) == (int32_t)(h + mid - 1u)) lo = mid;
        else hi = mid - 1u;
    }
    return lo;
}

int32_t om_bs_pump(om_bs *s, uint32_t now, uint32_t max)
{
    uint32_t before = s->got, drops = 0;
    int32_t n;
    if (om_bs_done(s)) return 0;
    n = s->io.count(s->io.ctx);
    if (n < 0) {                                         /* the drive did not answer: restart from where we are */
        s->err = (int)n;
        s->io_errors++;
        if (restart(s, OM_BS_WHY_COUNT, now) < 0) return OM_BS_E_STALL;
        n = 0;
    }
    while (n > 0 && max > 0 && !om_bs_done(s)) {
        uint32_t k = (uint32_t)n, pos = s->got & (s->ring_size - 1u), r = room(s) / OM_BS_SECTOR;
        int rc;
        if (r == 0) break;
        if (k > max) k = max;
        if (k > r) k = r;
        if (k > (s->ring_size - pos) / OM_BS_SECTOR) k = (s->ring_size - pos) / OM_BS_SECTOR;  /* not across the end */
        if (k > s->nsec - s->got / OM_BS_SECTOR) k = s->nsec - s->got / OM_BS_SECTOR;
        if (s->io.info && s->check) {                    /* is the head the sector we need, and how far does it run? */
            uint32_t want = s->base + s->got / OM_BS_SECTOR;
            int32_t h = s->io.info(s->io.ctx, 0);
            if (h < 0) {
                s->err = (int)h;
                s->io_errors++;
                if (restart(s, OM_BS_WHY_INFO, now) < 0) return OM_BS_E_STALL;
                break;
            }
            if ((uint32_t)h != want) {                   /* a sector of an earlier play: drop its whole run */
                uint32_t m = run_len(s, (uint32_t)h, (uint32_t)n);
                if ((uint32_t)h < want && m > want - (uint32_t)h) m = want - (uint32_t)h;
                if (++drops > 4u) break;                 /* bounded per call */
                if ((rc = s->io.drop(s->io.ctx, m)) < 0) { s->err = rc; s->io_errors++; break; }
                s->dropped += m;
                n -= (int32_t)m;
                continue;
            }
            if (k > 1u && s->io.info(s->io.ctx, k - 1u) != (int32_t)(want + k - 1u)) k = run_len(s, want, k);
        }
        if ((rc = s->io.take(s->io.ctx, s->ring + pos, k)) < 0) {
            s->err = rc;                                 /* the sectors may be gone from the CD buffer */
            s->io_errors++;
            if (restart(s, OM_BS_WHY_TAKE, now) < 0) return OM_BS_E_STALL;
            break;
        }
        s->got += k * OM_BS_SECTOR;
        n -= (int32_t)k;
        max -= k;
    }
    if (s->got != before || room(s) < OM_BS_SECTOR || om_bs_done(s)) {   /* news, or nothing is due */
        s->last_rx = now;
        s->stall_restarted = 0;
    } else if (now - s->last_rx >= s->abort_vb) {
        return OM_BS_E_STALL;
    } else if (now - s->last_rx >= s->stall_vb && !s->stall_restarted) {
        s->stall_restarted = 1;
        if (restart(s, OM_BS_WHY_STALL, now) < 0) return OM_BS_E_STALL;
    }
    return (int32_t)(s->got - before);
}
