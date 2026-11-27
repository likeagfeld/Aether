/* menu/src/biosid.h -- name the console's BIOS image by the CRC-32 of its 512 KB boot ROM (Q1). The rows
 * mirror tools/omsat/biosid.py (tools/tests/test_biosid.py keeps them equal). BSD-3-Clause. */
#ifndef OM_BIOSID_H
#define OM_BIOSID_H
#include <stdint.h>
enum { OM_BIOS_UNKNOWN, OM_BIOS_JP, OM_BIOS_US, OM_BIOS_OTHER };
typedef struct { uint32_t crc; const char *name; uint8_t family; } om_bios_id;
const om_bios_id *om_bios_lookup(uint32_t crc);                    /* NULL = not in the table */
uint32_t om_bios_crc(const volatile uint8_t *rom, uint32_t len);   /* zlib CRC-32 (om_crc32); len a multiple of 256 */
#define OM_BIOS_ROM_CT  0x20000000u   /* boot ROM, cache-through (HWF-12) */
#define OM_BIOS_ROM_LEN 0x80000u      /* 512 KB */
#endif
