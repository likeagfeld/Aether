/* menu/src/tga.c -- see tga.h. Truevision TGA header fields are little-endian. Pixel format: HWF-01. BSD-3-Clause. */
#include <string.h>
#include "omlayout.h"
#include "tga.h"

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint16_t rgb(uint32_t r, uint32_t g, uint32_t b) { return (uint16_t)(0x8000u | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)); }

static uint16_t conv(const uint8_t *p, uint32_t bits, int alpha_bit)
{
    if (bits == 24) return rgb(p[2], p[1], p[0]);
    if (bits == 32) return p[3] >= 128 ? rgb(p[2], p[1], p[0]) : 0;
    {                                                   /* 15/16: A RRRRR GGGGG BBBBB, little-endian */
        uint16_t v = le16(p);
        if (alpha_bit && !(v & 0x8000)) return 0;
        return (uint16_t)(0x8000u | ((v & 0x1Fu) << 10) | (v & 0x3E0u) | ((v >> 10) & 0x1Fu));
    }
}

int om_tga_decode(const uint8_t *f, uint32_t len, uint16_t *canvas, om_tga_info *info)
{
    uint32_t idlen, cmtype, type, cmfirst, cmlen, cmbits, w, h, bpp, desc, cmbytes, data, bytespp;
    uint32_t cw, ch, s, x0, y0, x, y, dx, dy;
    int alpha_bit;
    if (len < 18)
        return OM_TGA_E_SHORT;
    idlen = f[0]; cmtype = f[1]; type = f[2];
    cmfirst = le16(f + 3); cmlen = le16(f + 5); cmbits = f[7];
    w = le16(f + 12); h = le16(f + 14); bpp = f[16]; desc = f[17];
    if ((type != 1 && type != 2) || (desc & 0x10))
        return OM_TGA_E_TYPE;                          /* RLE 9/10/11 and right-to-left are refused */
    if (type == 2 && bpp != 16 && bpp != 24 && bpp != 32)
        return OM_TGA_E_BPP;
    if (type == 1 && (bpp != 8 || cmtype != 1 || (cmbits != 15 && cmbits != 16 && cmbits != 24 && cmbits != 32)))
        return OM_TGA_E_BPP;
    if (!w || !h)
        return OM_TGA_E_SIZE;
    /* DEVIATION (genuine bug, not in the brief's own text as written): the brief computes
     * `need = data + w*h*bytespp` -- a multiplication of three attacker-controlled fields (w,h up to
     * 65535 each, bytespp up to 4) -- and compares it to `len` *before* checking w/h against the hero
     * canvas size a few lines later. w*h*bytespp can wrap a 32-bit uint (e.g. w=h=46341, bpp=16 gives
     * w*h*2 = 4297064962, which is 97666 mod 2^32: a tiny "need" a short file would pass), which is
     * exactly this task's named hostile class ("a declared width x height x bpp that overflows"). In the
     * brief's own statement order this never reaches a bad read only because the canvas-size check
     * immediately afterwards (`w > cw || h > ch`) unconditionally rejects any w/h that large anyway --
     * safety was an accident of ordering, not a property of the guard. Moving the canvas-size check
     * first bounds w,h (<= 160, <= 200) at every later point a multiplication involving them happens, so
     * w*h*bytespp is at most 160*200*4 = 128000 and cannot overflow by construction; om_in_range
     * (subtraction/compare only, never size*n) replaces the raw `need` compare for the colour-map and
     * pixel-data spans, matching the "every size guard uses division, never n*size" lesson. */
    info->layout = (uint8_t)(w * 100 >= h * 85 ? OM_TGA_SQUARE : OM_TGA_TALL);
    cw = info->layout == OM_TGA_SQUARE ? 160 : 128;
    ch = info->layout == OM_TGA_SQUARE ? 160 : 200;
    if (w > cw || h > ch)
        return OM_TGA_E_SIZE;
    if (!om_in_range(18u, idlen, len))
        return OM_TGA_E_SHORT;                          /* image-ID field past EOF */
    cmbytes = cmtype ? cmlen * ((cmbits + 7) / 8) : 0;
    if (!om_in_range(18u + idlen, cmbytes, len))
        return OM_TGA_E_SHORT;                          /* colour map past EOF */
    data = 18 + idlen + cmbytes;
    bytespp = (bpp + 7) / 8;
    if (!om_in_range(data, w * h * bytespp, len))        /* w<=160, h<=200, bytespp<=4: product <= 128000 */
        return OM_TGA_E_SHORT;
    s = (w * 2 <= cw && h * 2 <= ch) ? 2 : 1;
    info->w = (uint16_t)w; info->h = (uint16_t)h; info->scale = (uint8_t)s;
    x0 = (cw - w * s) / 2;
    y0 = (ch - h * s) / 2;
    alpha_bit = (desc & 0x0F) != 0;
    for (x = 0; x < OM_HERO_PIXELS; x++)
        canvas[x] = 0;
    for (y = 0; y < h; y++) {
        uint32_t row = (desc & 0x20) ? y : h - 1 - y;   /* bit 5 set: top-left origin */
        const uint8_t *src = f + data + y * w * bytespp;
        for (x = 0; x < w; x++) {
            uint16_t v;
            if (type == 1) {
                uint32_t idx = src[x];
                if (idx < cmfirst || idx - cmfirst >= cmlen)
                    return OM_TGA_E_DATA;
                v = conv(f + 18 + idlen + (idx - cmfirst) * ((cmbits + 7) / 8), cmbits, alpha_bit);
            } else
                v = conv(src + x * bytespp, bpp, alpha_bit);
            for (dy = 0; dy < s; dy++)
                for (dx = 0; dx < s; dx++)
                    canvas[(y0 + row * s + dy) * cw + x0 + x * s + dx] = v;
        }
    }
    return 0;
}

const char *om_tga_strerror(int err)
{
    switch (err) {
    case OM_TGA_E_SHORT: return "TGA truncated";
    case OM_TGA_E_TYPE: return "TGA type not supported";
    case OM_TGA_E_BPP: return "TGA bit depth not supported";
    case OM_TGA_E_SIZE: return "TGA larger than hero";
    case OM_TGA_E_DATA: return "TGA bad color index";
    default: return "TGA error";
    }
}
