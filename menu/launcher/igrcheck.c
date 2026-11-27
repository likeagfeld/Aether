/* menu/launcher/igrcheck.c -- see igr.h. Pure C (host-tested by menu/host-tests/test_igr.c). BSD-3-Clause. */
#include <string.h>
#include "igr.h"

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* ip: the IP as it sits at 0x06002000 (at least 0x1000 bytes readable). stage_bytes: hook + stub image size.
 *  - the header must be the standard one (ST-040-R4 §4.2);
 *  - IP + 0x270 must hold 0x06000284: the security code's `mov.l @(0x270),r2; mov.l @r2,r2; jsr @r2` (IP +0x20A,
 *    measured in a retail IP and in the BIOS's own copy, ROM 0x630A) is the cell the hook replaces;
 *  - the stub home (IP + 0x800 .. + 0xD48) must lie inside the IP, which the first read never targets (HWF-20), and
 *    the staging area must lie past the IP's end and below 0x0600A000. stage_bytes 0: staged elsewhere (CWX, in
 *    LWRAM), so the IP may be up to 8000H (ST-040-R4 §4.3). */
int om_igr_ip_check(const uint8_t *ip, uint32_t stage_bytes)
{
    uint32_t size;
    if (!ip || memcmp(ip, "SEGA SEGASATURN ", 16) != 0)
        return OM_IGR_E_HEADER;
    if (be32(ip + (OM_IGR_SEC_CELL - OM_IGR_IP)) != OM_IGR_READ1ST)
        return OM_IGR_E_SECWORD;
    size = be32(ip + 0xE0);
    if (size < OM_IGR_STUB_END - OM_IGR_IP || size > (stage_bytes ? OM_IGR_STAGE - OM_IGR_IP : 0x8000u)
        || stage_bytes > OM_IGR_STAGE_END - OM_IGR_STAGE)
        return OM_IGR_E_IPSIZE;
    return OM_IGR_OK;
}

uint32_t om_igr_bios_execdmp_of(uint32_t v)
{
    return v >= 0x00000400u && v < 0x00080000u ? v : 0u;
}

uint32_t om_igr_seal(uint32_t *img, uint32_t longs)
{
    uint32_t s = 0, i;
    if (!img || !longs)
        return 0;
    img[longs - 1] = 0;
    for (i = 0; i + 1 < longs; i++)
        s += img[i];
    img[longs - 1] = OM_IGR_SEAL - s;
    return OM_IGR_SEAL;
}

const char *om_igr_strerror(int why)
{
    switch (why) {
    case OM_IGR_OK: return "ON";
    case OM_IGR_E_OFF: return "OFF (setting)";
    case OM_IGR_E_HEADER: return "OFF (IP header)";
    case OM_IGR_E_SECWORD: return "OFF (security code)";
    case OM_IGR_E_IPSIZE: return "OFF (IP size)";
    default: return "OFF";
    }
}
