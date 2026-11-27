/* menu/host-tests/test_biglogo.c -- BIGLOGO.BIN (the interstitial's and the screensaver's big logo, SPEC §5.19)
 * opens through its common header, and its decode + blend gives exactly the pixels the blend rule gives in Python
 * (tools/tests/test_biglogo.py pins the same CRC). BSD-3-Clause. */
#include "th.h"
#include "biglogo.h"
#include "om_fmt.h"
#include "omlayout.h"

#define BLACK_CRC 0xCA0438E6u   /* CRC-32 of the 152 x 101 words, big-endian, blended over black (gen_biglogo.shown_pixels) */

static uint16_t backdrop_const(void *ctx, int x, int y) { (void)x; (void)y; return *(const uint16_t *)ctx; }

int main(void)
{
    uint32_t flen, tlen = 0, i, opaque = 0, crc;
    uint8_t *file = th_load("../app/assets/BIGLOGO.BIN", &flen);
    const uint8_t *tbl = NULL, *t2;
    uint32_t l2;
    static uint8_t scratch[OM_BIGLOGO_W * OM_BIGLOGO_H];
    static uint16_t out[OM_BIGLOGO_W * OM_BIGLOGO_H], out2[OM_BIGLOGO_W * OM_BIGLOGO_H];
    uint8_t be[2];
    uint16_t black = 0, blue = 0x2C42;
    CHECK((flen + 2047u) / 2048u * 2048u <= OM_LW_BIGLOGO_MAX);       /* whole sectors fit its LWRAM region */
    CHECK_INT(OM_LW_BIGLOGO + OM_LW_BIGLOGO_MAX, OM_LW_STAGE);        /* the carve ends where the stage begins */
    CHECK_INT(OM_LW_ARTTAB + OM_LW_ARTTAB_MAX, OM_LW_BIGLOGO);        /* and starts where the ART.PAK table ends */
    CHECK_INT(om_biglogo_open(file, flen, &tbl, &tlen), OM_OK);
    CHECK(tbl == file + 64 && tlen == flen - 64);
    /* decode + blend over black: the CRC both test suites pin */
    memset(scratch, 0x5A, sizeof scratch);
    CHECK_INT(om_biglogo_blend(tbl, tlen, scratch, sizeof scratch, 0, 0, NULL, NULL, out), 0);
    for (i = 0; i < sizeof scratch; i++) if (scratch[i]) break;
    CHECK_INT(i, sizeof scratch);                                     /* scratch handed back zeroed */
    crc = 0;
    for (i = 0; i < OM_BIGLOGO_W * OM_BIGLOGO_H; i++) {
        be[0] = (uint8_t)(out[i] >> 8); be[1] = (uint8_t)out[i];
        crc = om_crc32(crc, be, 2);
        opaque += out[i] != 0;
    }
    CHECK_INT(crc, BLACK_CRC);
    CHECK_INT(opaque, 7094);                                          /* gen_biglogo.shown_pixels: 7,094 of 15,352 (the Aether mark) */
    /* a backdrop function returning black gives the same as NULL; another backdrop changes only edge pixels */
    CHECK_INT(om_biglogo_blend(tbl, tlen, scratch, sizeof scratch, 0, 0, backdrop_const, &black, out2), 0);
    CHECK(memcmp(out, out2, sizeof out) == 0);
    CHECK_INT(om_biglogo_blend(tbl, tlen, scratch, sizeof scratch, 0, 0, backdrop_const, &blue, out2), 0);
    CHECK(memcmp(out, out2, sizeof out) != 0);
    /* refused: the header (magic, CRC, a short file), the table (size, n, ops past the end), a small scratch */
    file[2] ^= 1;
    CHECK_INT(om_biglogo_open(file, flen, &t2, &l2), OM_E_MAGIC);
    file[2] ^= 1;
    file[64 + 200] ^= 1;
    CHECK_INT(om_biglogo_open(file, flen, &t2, &l2), OM_E_CRC);
    file[64 + 200] ^= 1;
    CHECK(om_biglogo_open(file, 40, &t2, &l2) < 0);
    CHECK(om_biglogo_open(file, flen - 8, &t2, &l2) < 0);
    CHECK(om_biglogo_open(NULL, 0, &t2, &l2) < 0);
    CHECK(om_biglogo_blend(tbl, tlen, scratch, sizeof scratch - 1, 0, 0, NULL, NULL, out2) < 0);
    CHECK(om_biglogo_blend(tbl, 7, scratch, sizeof scratch, 0, 0, NULL, NULL, out2) < 0);
    CHECK(om_biglogo_blend(NULL, 0, scratch, sizeof scratch, 0, 0, NULL, NULL, out2) < 0);
    {
        uint8_t *t = th_dup(tbl, tlen);
        t[1] ^= 1;                                                    /* width 152 -> 153 */
        CHECK(om_biglogo_blend(t, tlen, scratch, sizeof scratch, 0, 0, NULL, NULL, out2) < 0);
        t[1] ^= 1;
        t[6] = 0xFF; t[7] = 0xFF;                                     /* ops length past the table */
        CHECK(om_biglogo_blend(t, tlen, scratch, sizeof scratch, 0, 0, NULL, NULL, out2) < 0);
        free(t);
    }
    CHECK_INT(om_biglogo_open(file, flen, &t2, &l2), OM_OK);
    free(file);
    return th_done("test_biglogo");
}
