/* menu/src/vrtile.c -- see vrtile.h. Layout (gen_vrtiles.py): u16 w, h, count, 0; per tile u16 n, u16 ops length,
 * n x u16 palette, the codec-2 ops (bootanim.c om_ba_ops), zero padding to 4. Big-endian, read byte-wise (objcopy -I
 * binary gives no alignment). BSD-3-Clause. */
#include <stdint.h>
#include <string.h>
#include "anim.h"
#include "bootanim.h"
#include "om_fmt.h"
#include "vrtile.h"

static uint32_t be16(const uint8_t *p) { return (uint32_t)p[0] << 8 | p[1]; }

int om_vrtile_open(const uint8_t *file, uint32_t len, const uint8_t **tiles, uint32_t *tiles_len)
{
    om_header h;
    int rc = om_header_read(file, len, len, "OMSATVRT", &h);
    if (rc != OM_OK) return rc;
    if (h.table_size < 8 || h.count != be16(file + h.table_off + 4)) return OM_E_FIELD;
    *tiles = file + h.table_off;
    *tiles_len = h.table_size;
    return OM_OK;
}

int om_vrtile_decode(const uint8_t *blob, uint32_t len, unsigned i, const uint16_t roles[OM_VRT_ROLES], uint16_t *out)
{
    const uint32_t npix = OM_VRT_W * OM_VRT_H;
    uint16_t lut[256];
    uint8_t *idx = (uint8_t *)out + npix;                       /* the upper half of out (out is 2 * npix bytes) */
    uint32_t off = 8, k, n = 0, olen = 0, p;
    int rc;
    if (len < 8 || be16(blob) != OM_VRT_W || be16(blob + 2) != OM_VRT_H || i >= be16(blob + 4))
        return OM_E_FIELD;
    for (k = 0; k <= i; k++) {
        if (off + 4 > len) return OM_E_SHORT;
        n = be16(blob + off);
        olen = be16(blob + off + 2);
        if (!n || n > 255u || off + 4 + 2 * n + olen > len) return OM_E_RANGE;
        if (k < i) off += (4 + 2 * n + olen + 3) & ~3u;
    }
    lut[0] = (uint16_t)(roles[0] | 0x8000u);
    for (k = 1; k <= n; k++) {
        uint32_t w = be16(blob + off + 4 + 2 * (k - 1)), role = w >> 8, lv = w & 0xFFu;
        if (role >= OM_VRT_ROLES) return OM_E_FIELD;
        lut[k] = (uint16_t)(om_c555_mix(roles[0], roles[role], lv + (lv >> 7)) | 0x8000u);   /* 255 -> 256 */
    }
    memset(idx, 0, npix);
    if ((rc = om_ba_ops(idx, npix, blob + off + 4 + 2 * n, olen)) != 0)
        return rc;
    for (p = 0; p < npix; p++) {                                /* in place: word p (bytes 2p, 2p+1) never passes
                                                                 * index byte npix + p, read first */
        uint32_t v = idx[p];
        out[p] = v <= n ? lut[v] : lut[0];
    }
    return OM_OK;
}
