/* menu/src/biglogo.c -- see biglogo.h. The decode and blend are interstitial.c's logo_upload of M6 Task 6,
 * moved here unchanged so the interstitial and the screensaver share them; the table is read byte-wise (om_be16: the
 * CD read gives no alignment guarantee for the fields). BSD-3-Clause. */
#include <stdint.h>
#include <string.h>
#include "biglogo.h"
#include "bootanim.h"
#include "om_fmt.h"

int om_biglogo_open(const uint8_t *file, uint32_t len, const uint8_t **tbl, uint32_t *tlen)
{
    om_header h;
    int rc;
    if (!file) return OM_E_SHORT;
    if ((rc = om_header_read(file, len, len, OM_BIGLOGO_MAGIC, &h)) != OM_OK) return rc;
    if (h.count != 1u || h.table_size < 8u) return OM_E_FIELD;
    *tbl = file + h.table_off;
    *tlen = h.table_size;
    return OM_OK;
}

int om_biglogo_blend(const uint8_t *b, uint32_t tlen, uint8_t *scratch, uint32_t slen, int sx, int sy,
                     om_backdrop_fn bg, void *ctx, volatile uint16_t *out)
{
    const uint32_t npix = OM_BIGLOGO_W * OM_BIGLOGO_H;
    uint32_t w, h, n, olen, ops;
    int rc;
    if (!b || tlen < 8u || slen < npix) return OM_BA_E_INFO;
    w = om_be16(b); h = om_be16(b + 2); n = om_be16(b + 4); olen = om_be16(b + 6);
    ops = 8u + 3u * n + (n & 1u);
    if (w != OM_BIGLOGO_W || h != OM_BIGLOGO_H || !n || n >= 256u || ops + olen > tlen) return OM_BA_E_INFO;
    memset(scratch, 0, npix);
    rc = om_ba_ops(scratch, npix, b + ops, olen);
    if (!rc) {
        om_logo_img im;
        im.w = OM_BIGLOGO_W;
        im.h = OM_BIGLOGO_H;
        im.n = (uint16_t)n;
        im.first = 1;
        im.idx = scratch;
        im.pal = b + 8;
        im.alpha = b + 8u + 2u * n;
        im.pal_stride = 2;
        im.alpha_stride = 1;
        om_logo_blend(&im, sx, sy, bg, ctx, out);
    }
    memset(scratch, 0, npix);                                  /* the scratch is handed back zeroed */
    return rc;
}
