/* menu/launcher/le.h -- little-endian conversions for the Satiator .desc format (little-endian: research_satiator.md
 * §1.4; the one documented exception to the project's big-endian rule). Replaces <endian.h> in the MPL files. The
 * byte-wise form is correct on the big-endian SH-2 and on little-endian hosts alike, and it is its own inverse, so
 * it also serves cue2desc.c's le32toh-style use at :336. BSD-3-Clause. */
#ifndef OM_LE_H
#define OM_LE_H
#include <stdint.h>
#include <string.h>
static inline uint16_t om_le16(uint16_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; uint16_t r; memcpy(&r, b, 2); return r; }
static inline uint32_t om_le32(uint32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    uint32_t r;
    memcpy(&r, b, 4);
    return r;
}
#undef htole16
#undef htole32
#define htole16(x) om_le16((uint16_t)(x))
#define htole32(x) om_le32((uint32_t)(x))
#endif
