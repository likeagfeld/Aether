/* menu/src/isodir.c -- see isodir.h. Directory record layout per ECMA-119 §9.1: [0] record length, [2..9] extent LBA
 * (both-byte order: little- then big-endian), [10..17] data length (both-byte), [25] flags, [32] name length,
 * [33..] name. The PVD root record is at offset 156 (§8.4.18). BSD-3-Clause. */
#include "isodir.h"
#include "om_fmt.h"

static int rec_extent(const uint8_t *r, uint32_t *lba, uint32_t *size)
{
    if (r[0] < 34u) return OM_ISO_E_ROOT;
    *lba = om_be32(r + 6);
    *size = om_be32(r + 14);
    return 0;
}

int om_iso_root(const uint8_t *pvd, uint32_t *lba, uint32_t *size)
{
    static const char id[6] = { 1, 'C', 'D', '0', '0', '1' };
    uint32_t i;
    for (i = 0; i < 6; i++)
        if (pvd[i] != (uint8_t)id[i]) return OM_ISO_E_PVD;
    return rec_extent(pvd + 156, lba, size);
}

static int name_eq(const uint8_t *n, uint32_t nl, const char *want)
{
    uint32_t i;
    for (i = 0; i < nl; i++)                                 /* drop the ";<version>" suffix */
        if (n[i] == ';') { nl = i; break; }
    if (nl && n[nl - 1] == '.') nl--;                        /* and a trailing '.' (a name with no extension) */
    for (i = 0; i < nl && want[i]; i++)
        if (n[i] != (uint8_t)want[i]) return 0;
    return i == nl && !want[i];
}

int om_iso_find(const uint8_t *dir, uint32_t len, const char *name, uint32_t *lba, uint32_t *size)
{
    uint32_t p = 0;
    while (p < len) {
        const uint8_t *r = dir + p;
        uint32_t rl = r[0];
        if (rl == 0) {                                       /* padding to the end of this sector */
            p = (p / OM_SECTOR + 1u) * OM_SECTOR;
            continue;
        }
        if (rl < 34u || p + rl > len || 33u + r[32] > rl) return OM_ISO_E_ROOT;
        if (!(r[25] & 2u) && name_eq(r + 33, r[32], name))   /* flag bit 1 = directory */
            return rec_extent(r, lba, size);
        p += rl;
    }
    return OM_ISO_E_NONE;
}
