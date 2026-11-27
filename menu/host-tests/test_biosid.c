/* menu/host-tests/test_biosid.c -- M6 Task 1. BSD-3-Clause. */
#include "th.h"
#include "biosid.h"
int main(void)
{
    static uint8_t rom[1024];
    uint32_t i;
    for (i = 0; i < sizeof rom; i++) rom[i] = (uint8_t)(i * 7u + 3u);
    CHECK_INT(om_bios_crc(rom, sizeof rom), 0x5d3de8edu);   /* zlib.crc32 of the same bytes (measured) */
    CHECK(om_bios_lookup(0) == 0);
    CHECK_STR(om_bios_lookup(0xb3c63c25u)->name, "SATURN 1.003 J");
    CHECK(om_bios_lookup(0x4afcf0fau)->family == OM_BIOS_US);
    return th_done("test_biosid");
}
