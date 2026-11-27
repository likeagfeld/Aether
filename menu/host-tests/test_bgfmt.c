/* menu/host-tests/test_bgfmt.c -- BG.BIN reader against the golden fixture (contract/SPEC.md §5.7). BSD-3-Clause. */
#include "th.h"
#include "bgfmt.h"

int main(void)
{
    uint32_t n, i, zeros = 0;
    uint8_t *f = th_load(FIX("tiny.bg.bin"), &n), *m;
    om_bg b;
    CHECK_INT(n, OM_BG_FILE);
    CHECK_INT(om_bg_open(&b, f, n), OM_OK);
    CHECK_INT(om_bg_color(&b, 0), 0);
    for (i = 0; i < OM_BG_W * OM_BG_H; i++) zeros += b.pix[i] == 0;
    CHECK_INT(zeros, 0);
    CHECK_INT((om_bg_color(&b, b.pix[0]) & 0x1Fu) <= 2, 1);       /* fixture gradient: R=0 at the left edge */
    CHECK(om_bg_open(&b, f, n - 1) != OM_OK);                     /* truncated */
    m = th_dup(f, n);
    m[600] ^= 1;
    CHECK_INT(om_bg_open(&b, m, n), OM_E_CRC);
    free(m);
    m = th_dup(f, n);
    memcpy(m, "OMSATXXX", 8);
    CHECK_INT(om_bg_open(&b, m, n), OM_E_MAGIC);
    free(m); free(f);
    return th_done("test_bgfmt");
}
