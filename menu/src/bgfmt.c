/* menu/src/bgfmt.c -- see bgfmt.h. BSD-3-Clause. */
#include "bgfmt.h"
#include "om_fmt.h"

int om_bg_open(om_bg *b, const uint8_t *buf, uint32_t len)
{
    om_header h;
    int rc;
    if (len < OM_HEADER_SIZE)
        return OM_E_SHORT;
    if ((rc = om_header_read(buf, len, len, "OMSATBGI", &h)) != OM_OK)
        return rc;
    if (len != OM_BG_FILE || h.count != 1 || h.table_off != 64u || h.table_size != 512u + OM_BG_W * OM_BG_H ||
        h.sec_off[0] != 64u || h.sec_size[0] != 512u || h.sec_off[1] != 576u || h.sec_size[1] != OM_BG_W * OM_BG_H)
        return OM_E_FIELD;
    b->pal = buf + 64;
    b->pix = buf + 576;
    return OM_OK;
}

uint16_t om_bg_color(const om_bg *b, uint32_t i) { return (uint16_t)(om_be16(b->pal + 2u * (i & 0xFFu)) & 0x7FFFu); }
