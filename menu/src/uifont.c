/* menu/src/uifont.c -- see uifont.h. BSD-3-Clause. */
#include "uifont.h"

const om_face *om_face_get(int id)
{
    static const om_face *const k[OM_FACE_COUNT] = { &om_face_item, &om_face_sel, &om_face_small, &om_face_title,
                                                     &om_face_brand, &om_face_label, &om_face_hero,
                                                     &om_face_label8 };
    return id >= 0 && id < OM_FACE_COUNT ? k[id] : &om_face_item;
}

int om_uf_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    unsigned c = p[0];
    if (!c)
        return -1;
    if (c < 0x80) {
        *s += 1;
        return c >= 0x20 && c < 0x7F ? (int)(c - 0x20) : '?' - 0x20;
    }
    if (c == 0xC2 && p[1] == 0xB7) { *s += 2; return OM_UF_MIDDOT; }
    if (c == 0xE2 && p[1] == 0x80 && p[2] == 0xBA) { *s += 3; return OM_UF_RSAQUO; }
    if (c == 0xE2 && p[1] == 0x80 && p[2] == 0xA6) { *s += 3; return OM_UF_ELLIPSIS; }
    /* anything else: skip one whole UTF-8 sequence (or one stray byte) and show '?' */
    {
        int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1, i;
        for (i = 1; i < n && (p[i] & 0xC0) == 0x80; i++) ;
        *s += i;
    }
    return '?' - 0x20;
}

int om_uf_height(const om_face *f) { return f->asc + f->desc; }

static int extent_t(const om_face *f, const char *s, int *pen8_out, int track8)
{
    int pen8 = 0, ink = 0, g, first = 1;
    while ((g = om_uf_next(&s)) >= 0) {
        const om_glyph *gl = &f->g[g];
        int x;
        if (!first)
            pen8 += track8;                                    /* letter spacing goes between glyphs */
        first = 0;
        x = ((pen8 + 4) >> 3) + gl->xoff + gl->w;
        if (gl->w && x > ink)
            ink = x;
        pen8 += gl->adv8;
    }
    if (pen8_out)
        *pen8_out = pen8;
    return ink > (pen8 + 7) >> 3 ? ink : (pen8 + 7) >> 3;
}

int om_uf_width(const om_face *f, const char *s) { return extent_t(f, s, 0, 0); }
int om_uf_width_tracked(const om_face *f, const char *s, int track8) { return extent_t(f, s, 0, track8); }

static void put(uint8_t *buf, int stride_px, int x, int y, unsigned v)
{
    uint8_t *b = buf + y * (stride_px >> 1) + (x >> 1);
    if (x & 1) {
        if ((*b & 0x0F) < v) *b = (uint8_t)((*b & 0xF0) | v);
    } else if ((*b >> 4) < v)
        *b = (uint8_t)((*b & 0x0F) | (v << 4));
}

static void blit(const uint8_t *src, int sw, int sh, uint8_t *buf, int stride_px, int w, int h, int x0, int y0)
{
    int sstride = (sw + 1) >> 1, y, x;
    for (y = 0; y < sh; y++) {
        int dy = y0 + y;
        if (dy < 0 || dy >= h)
            continue;
        for (x = 0; x < sw; x++) {
            int dx = x0 + x;
            unsigned v = src[y * sstride + (x >> 1)];
            v = (x & 1) ? (v & 0x0F) : (v >> 4);
            if (v && dx >= 0 && dx < w)
                put(buf, stride_px, dx, dy, v);
        }
    }
}

int om_uf_draw(const om_face *f, const char *s, uint8_t *buf, int stride_px, int w, int h, int x, int base)
{
    return om_uf_draw_tracked(f, s, buf, stride_px, w, h, x, base, 0);
}

int om_uf_draw_tracked(const om_face *f, const char *s, uint8_t *buf, int stride_px, int w, int h, int x, int base,
                       int track8)
{
    int pen8 = x * 8, g, first = 1;
    while ((g = om_uf_next(&s)) >= 0) {
        const om_glyph *gl = &f->g[g];
        if (!first)
            pen8 += track8;
        first = 0;
        if (gl->w)
            blit(f->bits + gl->off, gl->w, gl->h, buf, stride_px, w, h, ((pen8 + 4) >> 3) + gl->xoff,
                 base + gl->yoff);
        pen8 += gl->adv8;
    }
    return (pen8 + 4) >> 3;
}

void om_uf_icon(const om_icon *ic, uint8_t *buf, int stride_px, int w, int h, int x, int y)
{
    blit(ic->bits, ic->w, ic->h, buf, stride_px, w, h, x, y);
}

void om_uf_fade(uint8_t *buf, int stride_px, int h, int from_x, int len)
{   /* column from_x + i keeps (len - i) / (len + 1) of its coverage: a soft cut, never an ellipsis (addendum 3) */
    int x, y;
    for (x = from_x; x < from_x + len && x < stride_px; x++) {
        unsigned keep = (unsigned)(from_x + len - x), div = (unsigned)len + 1u;
        if (x < 0)
            continue;
        for (y = 0; y < h; y++) {
            uint8_t *b = buf + y * (stride_px >> 1) + (x >> 1);
            unsigned v = (x & 1) ? (*b & 0x0Fu) : (unsigned)(*b >> 4);
            v = v * keep / div;
            *b = (uint8_t)((x & 1) ? ((*b & 0xF0u) | v) : ((*b & 0x0Fu) | (v << 4)));
        }
    }
}

const om_flag *om_flag_for(char c)
{
    int i;
    for (i = 0; i < OM_NFLAGS; i++)
        if (om_flags[i].code == c)
            return &om_flags[i];
    return 0;
}
