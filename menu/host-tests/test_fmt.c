#include "th.h"
#include "om_fmt.h"

int main(void)
{
    uint32_t n;
    uint8_t *gdb = th_load(FIX("tiny.games.db"), &n), *m;
    om_header h;
    CHECK_INT(om_crc32(0, "123456789", 9), 0xCBF43926u);                        /* CRC-32/IEEE check value */
    CHECK_INT(om_crc32(om_crc32(0, "1234", 4), "56789", 5), 0xCBF43926u);       /* chains like zlib.crc32 */
    CHECK_INT(om_crc32(0, "", 0), 0u);
    CHECK_INT(om_be32((const uint8_t *)"\x12\x34\x56\x78"), 0x12345678u);
    CHECK_INT(om_be16((const uint8_t *)"\xAB\xCD"), 0xABCDu);
    CHECK_INT(om_header_read(gdb, n, n, "OMSATGDB", &h), OM_OK);
    CHECK_INT(h.count, 4);
    CHECK_INT(h.table_off, 64);
    CHECK_INT(h.build_date, 20261117u);
    CHECK_INT(om_header_read(gdb, n, n, "OMSATART", &h), OM_E_MAGIC);
    m = th_dup(gdb, n); m[9] = 2;
    CHECK_INT(om_header_read(m, n, n, "OMSATGDB", &h), OM_E_VERSION); free(m);
    m = th_dup(gdb, n); m[11] = 63;
    CHECK_INT(om_header_read(m, n, n, "OMSATGDB", &h), OM_E_HDRSIZE); free(m);
    m = th_dup(gdb, n); m[64 + 20] ^= 1;                                         /* a table byte */
    CHECK_INT(om_header_read(m, n, n, "OMSATGDB", &h), OM_E_CRC); free(m);
    CHECK_INT(om_header_read(gdb, n, n - 1, "OMSATGDB", &h), OM_E_RANGE);        /* pool ends at EOF */
    CHECK_INT(om_header_read(gdb, 70, n, "OMSATGDB", &h), OM_E_SHORT);           /* table not in buffer */
    CHECK_INT(om_header_read(gdb, 10, n, "OMSATGDB", &h), OM_E_SHORT);
    CHECK_INT(om_in_range(0xFFFFFFF0u, 0x20u, 0xFFFFFFFFu), 0);                  /* no wrap-around */
    CHECK_INT(om_in_range(4, 4, 8), 1);
    CHECK_STR(om_strerror(OM_E_CRC), "CRC mismatch");
    CHECK_STR(om_strerror(-104), "SD no file");          /* om_sd_err(-FR_NO_FILE) */
    CHECK_STR(om_strerror(-119), "SD invalid parameter");
    CHECK_STR(om_strerror(-4), "bad header size");      /* the band never collides */
    CHECK_STR(om_strerror(-120), "SATIATOR: NO RESPONSE");   /* M2 deferred (e): OM_SD_TIMEOUT's on-screen name */
    CHECK_STR(om_strerror(-101), "SD disk error");
    CHECK_STR(om_strerror(-119), "SD invalid parameter");
    CHECK_STR(om_strerror(-98), "not attempted");    /* OM_AUDIO_E_NOTTRIED (review-c2-12-14.md Minor) */
    free(gdb);
    return th_done("test_fmt");
}
