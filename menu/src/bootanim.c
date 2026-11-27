/* menu/src/bootanim.c -- see bootanim.h. BSD-3-Clause. */
#include <string.h>
#include "bootanim.h"
#include "om_fmt.h"

uint32_t om_ba_head_bytes(const uint8_t *buf, uint32_t have)
{
    if (have < OM_HEADER_SIZE) return 0;
    return om_be32(buf + 0x10) + om_be32(buf + 0x14);           /* table offset + table size (§5.1) */
}

int om_ba_open(om_ba *a, const uint8_t *buf, uint32_t have, uint32_t file_size)
{
    om_header h;
    const uint8_t *info = buf + 64;
    uint32_t i;
    int rc;
    if (have < OM_BA_INDEX_OFF) return OM_E_SHORT;
    if ((rc = om_header_read(buf, have, file_size, "OMSATBAN", &h)) != OM_OK) return rc;
    if (h.sec_off[0] != 64u || h.sec_size[0] != 16u || h.sec_off[1] != 80u || h.sec_size[1] != 512u ||
        h.sec_off[2] != 0u || h.sec_size[2] != 0u || h.table_off != OM_BA_INDEX_OFF || h.table_size != 12u * h.count)
        return OM_E_FIELD;
    a->w = om_be16(info);
    a->h = om_be16(info + 2);
    a->fps = om_be16(info + 4);
    a->chant_x100 = om_be32(info + 8);
    if (om_be16(info + 6) != OM_BA_CODEC || a->w == 0 || a->w % 8u || a->w > OM_BA_MAX_W || a->h == 0 || a->h > OM_BA_MAX_H ||
        a->fps != 60u || h.count == 0 || h.count > OM_BA_MAX_FRAMES || a->chant_x100 >= 100u * h.count)
        return OM_BA_E_INFO;
    a->count = h.count;
    a->index = buf + OM_BA_INDEX_OFF;
    a->palette = buf + 80;
    a->data_off = h.data_off;
    a->data_end = h.data_off + h.data_size;
    a->file_size = file_size;
    for (i = 0; i < a->count; i++) {                             /* every frame inside the data section, in order */
        uint32_t off, len, crc;
        om_ba_entry(a, i, &off, &len, &crc);
        if (len == 0 || len > OM_BA_MAX_FRAME || off < a->data_off || off > a->data_end || len > a->data_end - off)
            return OM_BA_E_INFO;
        if (i && off < om_be32(a->index + 12u * (i - 1u)) + om_be32(a->index + 12u * (i - 1u) + 4u))
            return OM_BA_E_INFO;
    }
    return OM_OK;
}

void om_ba_entry(const om_ba *a, uint32_t i, uint32_t *off, uint32_t *len, uint32_t *crc)
{
    const uint8_t *e = a->index + 12u * i;
    *off = om_be32(e);
    *len = om_be32(e + 4);
    *crc = om_be32(e + 8);
}

uint16_t om_ba_color(const om_ba *a, uint32_t i) { return (uint16_t)(om_be16(a->palette + 2u * (i & 0xFFu)) & 0x7FFFu); }
int32_t om_ba_ref(uint32_t i) { return (i & 1u) ? (int32_t)i - 1 : (int32_t)i - 2; }

uint32_t om_ba_chant_frame(const om_ba *a)
{
    uint32_t f = 0, x = a->chant_x100;
    while (x >= 100u) { x -= 100u; f++; }                        /* no division: once, at open */
    return f;
}

typedef uint32_t __attribute__((may_alias)) u32a;   /* word stores into a byte buffer */

/* A 32-bit word from 4 bytes in memory order, for the target's byte order (the host tests run little-endian). */
static inline uint32_t word4(const uint8_t *s)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return (uint32_t)s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24);
#else
    return ((uint32_t)s[0] << 24) | ((uint32_t)s[1] << 16) | ((uint32_t)s[2] << 8) | (uint32_t)s[3];
#endif
}

/* n bytes from `from` to `to` (to > from allowed to overlap forward, as COPY needs): byte stores to a 4-byte boundary,
 * then words assembled from bytes (aligned word copies when the distance is a multiple of 4, a repeated pattern when it
 * is 1 or 2), then the tail. */
static inline void copy_fwd(uint8_t *to, const uint8_t *from, uint32_t n)
{
    uint32_t d = (uint32_t)(to - from);
    if (n >= 8u && d != 3u) {
        while ((uintptr_t)to & 3u) { *to++ = *from++; n--; }
        if (d >= 4u && !(d & 3u) && !((uintptr_t)from & 3u)) {
            for (; n >= 4u; n -= 4u, to += 4, from += 4) *(u32a *)to = *(const u32a *)from;
        } else if (d >= 4u) {
            for (; n >= 4u; n -= 4u, to += 4, from += 4) *(u32a *)to = word4(from);
        } else {                                                 /* d = 1 or 2 (overlap): a repeated pattern */
            uint8_t pat[4];
            uint32_t w;
            pat[0] = from[0]; pat[1] = d == 1u ? from[0] : from[1]; pat[2] = pat[0]; pat[3] = pat[1];
            w = word4(pat);
            for (; n >= 4u; n -= 4u, to += 4) *(u32a *)to = w;
            from = to - d;
        }
    }
    while (n--) *to++ = *from++;
}

int om_ba_ops(uint8_t *buf, uint32_t n, const uint8_t *src, uint32_t len)
{
    const uint8_t *p = src, *end = src + len;
    uint32_t pos = 0;
    while (pos < n) {
        uint32_t b, ln, v;
        if (p >= end) return OM_BA_E_OPS;
        b = *p++;
        v = b & 63u;
        if (v < 62u) ln = v + 1u;
        else if (v == 62u) { if (p >= end) return OM_BA_E_OPS; ln = 63u + *p++; }
        else { if (end - p < 2) return OM_BA_E_OPS; ln = ((uint32_t)p[0] << 8) | p[1]; p += 2; }
        if (ln == 0 || ln > n - pos) return OM_BA_E_OPS;
        switch (b >> 6) {
        case 0:                                                  /* SKIP: the reference pixels stay */
            break;
        case 1:                                                  /* LIT */
            if ((uint32_t)(end - p) < ln) return OM_BA_E_OPS;
            copy_fwd(buf + pos, p, ln);
            p += ln;
            break;
        case 2: {                                                /* COPY from dist back in this frame */
            uint32_t d;
            if (p >= end) return OM_BA_E_OPS;
            d = *p++;
            if (d & 0x80u) {
                if (p >= end) return OM_BA_E_OPS;
                d = (((d & 0x7Fu) << 8) | *p++) + 129u;
            } else d += 1u;
            if (d > pos) return OM_BA_E_DIST;
            copy_fwd(buf + pos, buf + pos - d, ln);
            break;
        }
        default: {                                               /* FILL */
            uint8_t *to = buf + pos;
            uint32_t k = ln, w;
            if (p >= end) return OM_BA_E_OPS;
            w = *p++;
            if (k >= 8u) {
                while ((uintptr_t)to & 3u) { *to++ = (uint8_t)w; k--; }
                w *= 0x01010101u;
                for (; k >= 4u; k -= 4u, to += 4) *(u32a *)to = w;
            }
            while (k--) *to++ = (uint8_t)w;
            break;
        }
        }
        pos += ln;
    }
    return p == end ? OM_OK : OM_BA_E_TRAIL;
}

void om_ba_dec_init(om_ba_dec *d, const om_ba *a, uint8_t *slots)
{
    uint32_t n = a->w * a->h;
    d->e = slots;
    d->q = slots + ((n + 3u) & ~3u);
    d->e_frame = -2;                                             /* black */
    d->q_frame = -3;
    d->pix = d->e;
    memset(slots, 0, n);
}

int om_ba_decode(om_ba_dec *d, const om_ba *a, uint32_t i, const uint8_t *src, uint32_t len)
{
    uint32_t n = a->w * a->h;
    int rc;
    if (i >= a->count || om_ba_ref(i) != d->e_frame) return OM_BA_E_REF;
    if (i & 1u) {                                                /* odd: its reference (the even frame) into Q first */
        memcpy(d->q, d->e, n);
        d->q_frame = -3;
        if ((rc = om_ba_ops(d->q, n, src, len)) != OM_OK) return rc;
        d->q_frame = (int32_t)i;
        d->pix = d->q;
    } else {
        d->e_frame = -3;                                         /* mid-decode: E holds neither frame */
        if ((rc = om_ba_ops(d->e, n, src, len)) != OM_OK) return rc;
        d->e_frame = (int32_t)i;
        d->pix = d->e;
    }
    return OM_OK;
}

void om_ba_dec_restore(om_ba_dec *d, const om_ba *a, int32_t frame, const uint8_t *pix)
{
    uint32_t n = a->w * a->h;
    if (frame == -2) memset(d->e, 0, n);
    else memcpy(d->e, pix, n);
    d->e_frame = frame;
    d->q_frame = -3;
    d->pix = d->e;
}

void om_ba_pacer_init(om_ba_pacer *p, uint32_t count, uint32_t chant_frame)
{
    memset(p, 0, sizeof *p);
    p->count = count;
    p->chant = chant_frame;
    p->shown = p->fallback_frame = p->chant_vbl = p->done_vbl = OM_BA_NONE;
}

uint32_t om_ba_pacer_want(const om_ba_pacer *p)
{
    if (p->done || p->hold) return OM_BA_NONE;
    return p->next;
}

unsigned om_ba_pacer_tick(om_ba_pacer *p, int ready)
{
    uint32_t w = om_ba_pacer_want(p);
    unsigned ev = 0;
    if (p->done) return 0;
    if (w == OM_BA_NONE) {
        p->hold = 0;                                             /* the second V-blank of a 30 fps frame */
    } else if (ready) {
        ev |= OM_BA_EV_SHOW;
        p->shown = w;
        if (!p->chant_done && w >= p->chant) {                   /* at 30 fps the chant frame may be skipped */
            p->chant_done = 1;
            p->chant_vbl = p->vbl;
            ev |= OM_BA_EV_CHANT;
        }
        p->next = w + (p->half ? 2u : 1u);
        p->hold = p->half;
        if (p->next >= p->count) {
            p->done = 1;
            p->done_vbl = p->vbl;
            ev |= OM_BA_EV_DONE;
        }
    } else {
        p->underruns++;
        if (!p->half) {                                          /* first underrun: even frames from here on */
            p->half = 1;
            p->reason = 1;
            p->fallback_frame = w;
            p->next = (w + 1u) & ~1u;
            ev |= OM_BA_EV_FALLBACK;
        }
    }
    p->vbl++;
    return ev;
}

unsigned om_ba_pacer_slow(om_ba_pacer *p)
{
    p->slow++;
    if (p->done || p->half) return 0;
    p->half = 1;
    p->reason = 2;
    p->fallback_frame = p->next;
    p->next = (p->next + 1u) & ~1u;                              /* the next even frame: E holds its reference */
    p->hold = 0;
    if (p->next >= p->count) { p->done = 1; p->done_vbl = p->vbl; return OM_BA_EV_FALLBACK | OM_BA_EV_DONE; }
    return OM_BA_EV_FALLBACK;
}
