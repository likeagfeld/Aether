/* menu/host-tests/test_logoblend.c -- logo edges blended with the backdrop at upload (VDP1 has 1-bit
 * transparency, HWF-01/HWF-02). BSD-3-Clause. */
#include "th.h"
#include "logoblend.h"

static int lx, ly;
static uint16_t black(void *c, int x, int y) { (void)c; lx = x; ly = y; return 0x0000; }
static uint16_t white(void *c, int x, int y) { (void)c; lx = x; ly = y; return 0x7FFF; }

int main(void)
{
    static const uint8_t idx[4] = { 0, 1, 2, 3 };
    static const uint8_t pal[4] = { 0x7C, 0x1F, 0x00, 0x1F };            /* 1 = R31 B31, 2 = R31 */
    static const uint8_t alpha[2] = { 255, 128 };
    om_logo_img im = { 4, 1, 2, 1, idx, pal, alpha, 2, 1 };
    uint16_t out[4];
    om_logo_blend(&im, 100, 50, black, 0, out);
    CHECK_INT(out[0], 0x0000);                                           /* index 0: transparent */
    CHECK_INT(out[1], 0xFC1F);                                           /* opaque: the color, bit 15 set */
    CHECK_INT(out[2], 0x8010);                                           /* R31 at 128/255 over black: R16 */
    CHECK_INT(out[3], 0x0000);                                           /* past the palette: transparent */
    CHECK_INT(lx, 102); CHECK_INT(ly, 50);                               /* sampled where the pixel lands */
    om_logo_blend(&im, 0, 0, white, 0, out);
    CHECK_INT(out[2], 0xBDFF);                                           /* over white: R31 G15 B15 */
    {
        static const uint8_t a0[2] = { 255, 0 };
        om_logo_img z = { 4, 1, 2, 1, idx, pal, a0, 2, 1 };
        om_logo_blend(&z, 0, 0, white, 0, out);
        CHECK_INT(out[2], 0x0000);                                       /* alpha 0: transparent */
    }
    {
        static const uint8_t rgb[4] = { 0x80, 0x1F, 0xFF, 0xFF };        /* opaque R31; white */
        static const uint8_t a4[1] = { 0xF8 };                           /* 15, 8 (= 136) */
        om_logo_blend_direct(rgb, a4, 2, 1, 0, 0, black, 0, out);
        CHECK_INT(out[0], 0x801F);
        CHECK_INT(out[1], 0xC631);                                       /* white at 136/255 over black: 17,17,17 */
    }
    CHECK_INT(om_mix555(0x7FFF, 0x0000, 255), 0xFFFF);
    CHECK_INT(om_mix555(0x7FFF, 0x0000, 0), 0x8000);
    return th_done("test_logoblend");
}
