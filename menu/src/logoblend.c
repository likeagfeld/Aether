/* menu/src/logoblend.c -- see logoblend.h. Out-pixels are written as 16-bit words (VDP1 VRAM: HWF-23).
 * BSD-3-Clause. */
#include "logoblend.h"
#include "om_fmt.h"

uint16_t om_mix555(uint16_t fg, uint16_t bg, uint32_t a)
{
    uint32_t r = ((fg & 31u) * a + (bg & 31u) * (255u - a) + 127u) / 255u;
    uint32_t g = (((fg >> 5) & 31u) * a + ((bg >> 5) & 31u) * (255u - a) + 127u) / 255u;
    uint32_t b = (((fg >> 10) & 31u) * a + ((bg >> 10) & 31u) * (255u - a) + 127u) / 255u;
    return (uint16_t)(0x8000u | b << 10 | g << 5 | r);              /* bit 15: an RGB pixel, opaque (HWF-01) */
}

void om_logo_blend(const om_logo_img *im, int sx, int sy, om_backdrop_fn bg, void *ctx, volatile uint16_t *out)
{
    uint32_t x, y, i = 0;
    for (y = 0; y < im->h; y++)
        for (x = 0; x < im->w; x++, i++) {
            uint32_t k = im->idx[i], e, a;
            uint16_t c;
            if (!k || k < im->first || (e = k - im->first) >= im->n) { out[i] = 0; continue; }
            a = im->alpha[e * im->alpha_stride];
            c = (uint16_t)(om_be16(im->pal + e * im->pal_stride) & 0x7FFFu);
            out[i] = !a ? 0 : a == 255u ? (uint16_t)(c | 0x8000u)
                   : om_mix555(c, bg ? bg(ctx, sx + (int)x, sy + (int)y) : 0, a);
        }
}

void om_logo_blend_direct(const uint8_t *rgb_be, const uint8_t *alpha4, int w, int h, int sx, int sy,
                          om_backdrop_fn bg, void *ctx, volatile uint16_t *out)
{
    int x, y;
    uint32_t i = 0;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++, i++) {
            uint32_t a = ((alpha4[i >> 1] >> ((i & 1u) ? 0 : 4)) & 15u) * 17u;
            uint16_t c = (uint16_t)(om_be16(rgb_be + 2u * i) & 0x7FFFu);
            out[i] = !a ? 0 : a == 255u ? (uint16_t)(c | 0x8000u) : om_mix555(c, bg ? bg(ctx, sx + x, sy + y) : 0, a);
        }
}
