/* menu/launcher/lgread.c -- see lgread.h. BSD-3-Clause. */
#include "lgread.h"
#include "loadbg.h"
#include "satiator.h"

int om_lgr_read(om_lgr *c, uint32_t end, uint32_t cap)
{
    uint32_t at = c->rp & ~3u;
    int n;
    if (at != c->rp) {                                       /* s_read stores whole words: never an unaligned buffer */
        if (s_seek(c->fd, (int)at, C_SEEK_SET) < 0) return -1;
        c->rp = at;
    }
    if (end <= at) return -1;
    if ((n = s_read(c->fd, c->file + at, (int)(end - at > cap ? cap : end - at))) <= 0) return -1;
    c->rp = at + (uint32_t)n;
    return n;
}

int om_lgr_from(om_lgr *c, uint32_t off)
{
    off &= ~3u;
    c->s1 = c->e1 = c->rp = off;
    return s_seek(c->fd, (int)off, C_SEEK_SET) < 0 ? -1 : 0;
}

int om_lgr_some(om_lgr *c, uint32_t cap)
{
    uint32_t end = c->wrap ? c->s1 : c->size, ws;
    if (c->rp >= end) {
        ws = om_lbg_head_bytes(c->file, c->size);
        if ((c->w0 & ~3u) > ws) ws = c->w0 & ~3u;
        if (c->wrap || c->s1 <= ws) return 0;
        c->wrap = 1;                                         /* the part before where an adopted frame started */
        c->rp = c->e0 = c->ws = ws;
        if (s_seek(c->fd, (int)c->rp, C_SEEK_SET) < 0) return -1;
        return 1;
    }
    if (om_lgr_read(c, end, cap) < 0) return -1;
    if (c->wrap) c->e0 = c->rp; else c->e1 = c->rp;
    return 1;
}

int om_lgr_has(const om_lgr *c, uint32_t head, uint32_t off, uint32_t len)
{
    if (off >= c->s1 && off + len <= c->e1) return 1;
    if (off < head || (c->wrap && off < c->ws)) return 0;
    return off + len <= c->e0 || (c->e0 >= c->s1 && off + len <= c->e1);
}

uint32_t om_lgr_ahead(const om_lgr *c, const om_lbg *g, uint32_t frame, uint32_t max)
{
    uint32_t head = om_lbg_head_bytes(c->file, c->size), k, off, len;
    for (k = 0; k < max; k++) {
        frame = frame + 1u >= g->n ? 0u : frame + 1u;        /* om_lbg_next */
        om_lbg_entry_of(g, frame, &off, &len);
        if (!om_lgr_has(c, head, off, len)) break;
    }
    return k;
}

int om_lgr_flip(uint32_t us, uint32_t period, int vblank)
{
    if (us < period) return OM_LGR_LATER;
    if (vblank) return OM_LGR_FLIP;
    return us >= period + OM_LGR_LATE_US ? OM_LGR_WAIT : OM_LGR_LATER;
}

int32_t om_lgr_credit(int32_t credit, uint32_t elapsed_us)
{
    int64_t c = (int64_t)credit + (int64_t)elapsed_us * OM_LGR_SHARE / 100;
    return c > OM_LGR_CREDIT_MAX ? OM_LGR_CREDIT_MAX : (int32_t)c;
}
