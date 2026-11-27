/* menu/src/om_fmt.h -- big-endian access, CRC-32 and the 64-byte common header (contract/SPEC.md §5.1).
 * Aether, BSD-3-Clause. Portable C99: compiled for the Saturn and for host tests. */
#ifndef OM_FMT_H
#define OM_FMT_H
#include <stdint.h>

#define OM_HEADER_SIZE 64u
#define OM_SECTOR      2048u

enum {
    OM_OK = 0, OM_E_SHORT = -1, OM_E_MAGIC = -2, OM_E_VERSION = -3, OM_E_HDRSIZE = -4,
    OM_E_RANGE = -5, OM_E_CRC = -6, OM_E_TABLESIZE = -7, OM_E_FIELD = -8, OM_E_FULL = -9
};

typedef struct {
    uint32_t count, table_off, table_size, data_off, data_size, build_date, table_crc;
    uint32_t sec_off[3], sec_size[3];
} om_header;

uint16_t om_be16(const uint8_t *p);
uint32_t om_be32(const uint8_t *p);
void om_put16(uint8_t *p, uint16_t v);
void om_put32(uint8_t *p, uint32_t v);
uint32_t om_crc32(uint32_t crc, const void *data, uint32_t len);
int om_in_range(uint32_t off, uint32_t size, uint32_t total);
/* buf must hold at least [0, table_off + table_size) for the CRC check; file_size bounds every range. */
int om_header_read(const uint8_t *buf, uint32_t buf_len, uint32_t file_size, const char magic[8], om_header *h);
const char *om_strerror(int err);
#endif
