/* menu/src/loadbg.c -- see loadbg.h. BSD-3-Clause. */
#include <string.h>
#include "bootanim.h"
#include "loadbg.h"
#include "om_fmt.h"

#define INDEX_OFF 592u                                       /* header 64 + info 16 + palette 512 (as BOOTANIM.BIN) */

static void entry(const om_lbg *g, uint32_t i, uint32_t *off, uint32_t *len, uint32_t *crc)
{
    const uint8_t *e = g->index + 12u * i;
    *off = om_be32(e);
    *len = om_be32(e + 4);
    *crc = om_be32(e + 8);
}

uint32_t om_lbg_head_bytes(const uint8_t *file, uint32_t have)
{
    if (have < OM_HEADER_SIZE) return 0;
    return om_be32(file + 0x10) + om_be32(file + 0x14);       /* table offset + table size (§5.1) */
}

int om_lbg_open(om_lbg *g, const uint8_t *file, uint32_t have, uint32_t file_size)
{
    om_header h;
    const uint8_t *info = file + 64;
    uint32_t i, prev_end;
    int rc;
    if (have < INDEX_OFF) return OM_E_SHORT;
    if ((rc = om_header_read(file, have, file_size, "OMSATLBG", &h)) != OM_OK) return rc;
    if (h.sec_off[0] != 64u || h.sec_size[0] != 16u || h.sec_off[1] != 80u || h.sec_size[1] != 512u ||
        h.sec_off[2] != 0u || h.sec_size[2] != 0u || h.table_off != INDEX_OFF || h.table_size != 12u * h.count)
        return OM_E_FIELD;
    g->w = om_be16(info);
    g->h = om_be16(info + 2);
    g->scale = om_be16(info + 12);
    g->fps = om_be16(info + 4);
    if (om_be16(info + 6) != OM_LBG_CODEC || (g->fps != 60u && g->fps != 30u) || om_be32(info + 8) != 0u || g->w == 0 ||
        (g->w & 1u) || g->w > OM_LBG_MAX_W || g->h == 0 || g->h > OM_LBG_MAX_H || (g->scale != 1u && g->scale != 2u) ||
        g->w * g->scale > OM_LBG_MAX_W || g->h * g->scale > OM_LBG_MAX_H || h.count < 2u || h.count > 512u)
        return OM_LBG_E_INFO;
    g->file = file;
    g->index = file + INDEX_OFF;
    g->palette = file + 80;
    g->n = h.count - 1u;
    prev_end = h.data_off;
    for (i = 0; i < h.count; i++) {                          /* every entry inside the data section, in order */
        uint32_t off, ln, crc;
        entry(g, i, &off, &ln, &crc);
        if (ln == 0 || ln > OM_LBG_MAX_ENTRY || off < prev_end || off > h.data_off + h.data_size ||
            ln > h.data_off + h.data_size - off)
            return OM_LBG_E_INFO;
        prev_end = off + ln;
    }
    return OM_OK;
}

uint16_t om_lbg_color(const om_lbg *g, uint32_t i) { return (uint16_t)(om_be16(g->palette + 2u * (i & 0xFFu)) & 0x7FFFu); }

static int apply(om_lbg_play *p, const om_lbg *g, uint32_t i, int check)
{
    uint32_t off, ln, crc;
    int rc;
    entry(g, i, &off, &ln, &crc);
    p->ops = g->file + off;
    p->ops_len = ln;
    if ((rc = om_ba_ops(p->pix, g->w * g->h, p->ops, ln)) != OM_OK) return rc;
    if (check && om_crc32(0, p->pix, g->w * g->h) != crc) return OM_LBG_E_CRC;
    return OM_OK;
}

int om_lbg_start(om_lbg_play *p, const om_lbg *g, uint8_t *pix)
{
    p->pix = pix;
    p->frame = 0;
    memset(pix, 0, g->w * g->h);
    return apply(p, g, 0, 1);                                 /* entry 0: the keyframe, over zeros */
}


uint32_t om_lbg_key_need(const om_lbg *g)
{
    uint32_t off, ln, crc;
    entry(g, 0, &off, &ln, &crc);
    return off + ln;
}

uint32_t om_lbg_next(const om_lbg_play *p, const om_lbg *g) { return p->frame + 1u >= g->n ? 0u : p->frame + 1u; }

void om_lbg_entry_of(const om_lbg *g, uint32_t frame, uint32_t *off, uint32_t *len)
{
    uint32_t crc;
    entry(g, frame + 1u, off, len, &crc);
}

int om_lbg_dec_begin(om_lbg_dec *d, om_lbg_play *p, const om_lbg *g, const uint8_t *ops, uint32_t len, int check)
{
    uint32_t off, ln;
    d->next = om_lbg_next(p, g);
    entry(g, d->next + 1u, &off, &ln, &d->crc);
    if (len != ln) return OM_LBG_E_INFO;
    d->s = ops;
    d->end = ops + len;
    d->pos = 0;
    d->check = check;
    p->ops = ops;
    p->ops_len = len;
    return OM_OK;
}

int om_lbg_dec_run(om_lbg_dec *d, om_lbg_play *p, const om_lbg *g, uint32_t max_ops)
{
    uint8_t *buf = p->pix;
    uint32_t n = g->w * g->h;
    const uint8_t *s = d->s, *end = d->end;
    uint32_t pos = d->pos;
    while (pos < n && max_ops--) {
        uint32_t b, ln, v;
        if (s >= end) return OM_BA_E_OPS;
        b = *s++;
        v = b & 63u;
        if (v < 62u) ln = v + 1u;
        else if (v == 62u) { if (s >= end) return OM_BA_E_OPS; ln = 63u + *s++; }
        else { if (end - s < 2) return OM_BA_E_OPS; ln = ((uint32_t)s[0] << 8) | s[1]; s += 2; }
        if (ln == 0 || ln > n - pos) return OM_BA_E_OPS;
        switch (b >> 6) {
        case 0:                                              /* SKIP */
            break;
        case 1:                                              /* LIT */
            if ((uint32_t)(end - s) < ln) return OM_BA_E_OPS;
            memcpy(buf + pos, s, ln);
            s += ln;
            break;
        case 2: {                                            /* COPY, forward (overlap allowed) */
            uint32_t dist, k;
            if (s >= end) return OM_BA_E_OPS;
            dist = *s++;
            if (dist & 0x80u) {
                if (s >= end) return OM_BA_E_OPS;
                dist = (((dist & 0x7Fu) << 8) | *s++) + 129u;
            } else dist += 1u;
            if (dist > pos) return OM_BA_E_DIST;
            if (dist >= ln) memcpy(buf + pos, buf + pos - dist, ln);
            else for (k = 0; k < ln; k++) buf[pos + k] = buf[pos + k - dist];
            break;
        }
        default:                                             /* FILL */
            if (s >= end) return OM_BA_E_OPS;
            memset(buf + pos, *s++, ln);
            break;
        }
        pos += ln;
    }
    d->s = s;
    d->pos = pos;
    if (pos < n) return 1;
    if (s != end) return OM_BA_E_TRAIL;
    if (d->check && om_crc32(0, buf, n) != d->crc) return OM_LBG_E_CRC;
    p->frame = d->next;
    return 0;
}
