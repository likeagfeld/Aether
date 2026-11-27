/* menu/src/logoblend.h -- the logos carry alpha, and their soft edges are blended with the backdrop when
 * they are uploaded to VDP1 VRAM. VDP1 has only 1-bit transparency in RGB mode (code 0x0000 = transparent, SPdis
 * clear; HWF-01/HWF-02), so an edge pixel of alpha a becomes the opaque color a/255 over the screen behind it, and
 * alpha 0 stays the transparent code. Alpha 255 keeps the color unchanged (with only 0/255 alpha the upload equals
 * the old 1-bit asset exactly). Pure C, host-tested (menu/host-tests/test_logoblend.c). BSD-3-Clause. */
#ifndef OM_LOGOBLEND_H
#define OM_LOGOBLEND_H
#include <stdint.h>

typedef struct {
    uint16_t w, h, n, first;          /* palette covers indices first .. first + n - 1; index 0 is always transparent */
    const uint8_t *idx, *pal, *alpha; /* pal: big-endian RGB555 words; alpha: bytes */
    uint8_t pal_stride, alpha_stride; /* bytes between entries (built-in 2 / 1) */
} om_logo_img;
typedef uint16_t (*om_backdrop_fn)(void *ctx, int x, int y);   /* RGB555 of the screen behind (x, y) */

uint16_t om_mix555(uint16_t fg, uint16_t bg, uint32_t a);      /* a 0..255; result opaque (bit 15) */
/* An indexed image (the interstitial's big logo) at screen (sx, sy) -> w*h RGB555 words (16-bit writes, HWF-23).
 * bg NULL: the backdrop is black. */
void om_logo_blend(const om_logo_img *im, int sx, int sy, om_backdrop_fn bg, void *ctx, volatile uint16_t *out);
/* A direct-color image (the header logo): rgb_be w*h big-endian RGB555 words, alpha4 one nibble a pixel (high nibble =
 * left pixel; 15 = opaque, 0 = transparent, alpha = nibble * 17). */
void om_logo_blend_direct(const uint8_t *rgb_be, const uint8_t *alpha4, int w, int h, int sx, int sy,
                          om_backdrop_fn bg, void *ctx, volatile uint16_t *out);
#endif
