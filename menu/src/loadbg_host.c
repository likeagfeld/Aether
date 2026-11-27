/* menu/src/loadbg_host.c -- the parts of loadbg.h only the host tests and tools use (Task 13c round 3): the changed-run
 * walker and om_lbg_need. Kept out of the Saturn builds (tools/sbuild.py skips *_host.c) so the menu's HWRAM does not
 * carry them; the menu stages, decodes and DMAs whole frames (menu/views/loading.c). BSD-3-Clause. */
#include "bootanim.h"
#include "loadbg.h"
#include "om_fmt.h"

static void entry(const om_lbg *g, uint32_t i, uint32_t *off, uint32_t *len, uint32_t *crc)
{
    const uint8_t *e = g->index + 12u * i;
    *off = om_be32(e);
    *len = om_be32(e + 4);
    *crc = om_be32(e + 8);
}

void om_lbg_it_init(om_lbg_it *it, const om_lbg_play *p)
{
    it->s = p->ops;
    it->end = p->ops + p->ops_len;
    it->row = it->col = it->left = 0;
}

int om_lbg_it_next(om_lbg_it *it, const om_lbg *g, uint32_t *row, uint32_t *col, uint32_t *n)
{   /* op headers only (the file was validated by the decode that just ran); row/col tracked without a division */
    uint32_t w = g->w;
    while (!it->left) {                                       /* the next op that changed pixels */
        uint32_t b, v, ln, op;
        if (it->s >= it->end) return 0;
        b = *it->s++;
        v = b & 63u;
        op = b >> 6;
        if (v < 62u) ln = v + 1u;
        else if (v == 62u) ln = 63u + *it->s++;
        else { ln = ((uint32_t)it->s[0] << 8) | it->s[1]; it->s += 2; }
        if (op == 1u) it->s += ln;                            /* LIT bytes */
        else if (op == 2u) it->s += (*it->s & 0x80u) ? 2 : 1; /* COPY distance */
        else if (op == 3u) it->s++;                           /* FILL byte */
        if (op != 0u) {
            it->left = ln;
            it->rrow = it->row;
            it->rcol = it->col;
        }
        it->col += ln;
        while (it->col >= w) { it->col -= w; it->row++; }
    }
    {   /* the op's pixels, one row piece at a time */
        uint32_t take = w - it->rcol < it->left ? w - it->rcol : it->left;
        *row = it->rrow;
        *col = it->rcol;
        *n = take;
        it->left -= take;
        it->rcol += take;
        if (it->rcol == w) { it->rcol = 0; it->rrow++; }
    }
    return 1;
}

uint32_t om_lbg_runs(const om_lbg_play *p, const om_lbg *g, om_lbg_emit emit, void *ctx)
{
    om_lbg_it it;
    uint32_t row, col, n, count = 0;
    om_lbg_it_init(&it, p);
    while (om_lbg_it_next(&it, g, &row, &col, &n)) {
        emit(ctx, row, col, n);
        count++;
    }
    return count;
}

uint32_t om_lbg_need(const om_lbg *g, uint32_t frame)
{
    uint32_t off, ln, crc;
    entry(g, frame + 1u, &off, &ln, &crc);
    return off + ln;
}


int om_lbg_step(om_lbg_play *p, const om_lbg *g, int check)
{
    uint32_t next = p->frame + 1u;
    int rc;
    if (next >= g->n) next = 0;
    {   /* entry f + 1 holds frame f */
        uint32_t off, ln, crc;
        entry(g, next + 1u, &off, &ln, &crc);
        p->ops = g->file + off;
        p->ops_len = ln;
        if ((rc = om_ba_ops(p->pix, g->w * g->h, p->ops, ln)) != OM_OK) return rc;
        if (check && om_crc32(0, p->pix, g->w * g->h) != crc) return OM_LBG_E_CRC;
    }
    p->frame = next;
    return OM_OK;
}
