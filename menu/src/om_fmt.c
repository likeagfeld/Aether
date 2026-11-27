/* menu/src/om_fmt.c -- see om_fmt.h. Check order matches tools/omsat/binfmt.py validate_common. BSD-3-Clause. */
#include <string.h>
#include "om_fmt.h"

uint16_t om_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t om_be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
void om_put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
void om_put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static uint32_t s_tab[256];
static int s_tab_ok;

uint32_t om_crc32(uint32_t crc, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    if (!s_tab_ok) {
        uint32_t i, k, c;
        for (i = 0; i < 256; i++) {
            c = i;
            for (k = 0; k < 8; k++)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            s_tab[i] = c;
        }
        s_tab_ok = 1;
    }
    crc = ~crc;
    while (len--)
        crc = s_tab[(crc ^ *p++) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

int om_in_range(uint32_t off, uint32_t size, uint32_t total)
{
    return off <= total && size <= total - off;
}

int om_header_read(const uint8_t *buf, uint32_t buf_len, uint32_t file_size, const char magic[8], om_header *h)
{
    int i;
    if (buf_len < OM_HEADER_SIZE || file_size < OM_HEADER_SIZE)
        return OM_E_SHORT;
    if (memcmp(buf, magic, 8))
        return OM_E_MAGIC;
    if (om_be16(buf + 8) != 1)
        return OM_E_VERSION;
    if (om_be16(buf + 10) != OM_HEADER_SIZE)
        return OM_E_HDRSIZE;
    h->count = om_be32(buf + 0x0C);
    h->table_off = om_be32(buf + 0x10);
    h->table_size = om_be32(buf + 0x14);
    h->data_off = om_be32(buf + 0x18);
    h->data_size = om_be32(buf + 0x1C);
    h->build_date = om_be32(buf + 0x20);
    h->table_crc = om_be32(buf + 0x24);
    for (i = 0; i < 3; i++) {
        h->sec_off[i] = om_be32(buf + 0x28 + 8 * i);
        h->sec_size[i] = om_be32(buf + 0x2C + 8 * i);
    }
    if (!om_in_range(h->table_off, h->table_size, file_size) || !om_in_range(h->data_off, h->data_size, file_size))
        return OM_E_RANGE;
    for (i = 0; i < 3; i++)
        if (h->sec_size[i] && !om_in_range(h->sec_off[i], h->sec_size[i], file_size))
            return OM_E_RANGE;
    if (!om_in_range(h->table_off, h->table_size, buf_len))
        return OM_E_SHORT;
    if (om_crc32(0, buf + h->table_off, h->table_size) != h->table_crc)
        return OM_E_CRC;
    return OM_OK;
}

const char *om_strerror(int err)
{
    switch (err) {
    case OM_OK: return "ok";
    case OM_E_SHORT: return "file too short";
    case OM_E_MAGIC: return "wrong magic";
    case OM_E_VERSION: return "unknown version";
    case OM_E_HDRSIZE: return "bad header size";
    case OM_E_RANGE: return "range beyond file";
    case OM_E_CRC: return "CRC mismatch";
    case OM_E_TABLESIZE: return "table size";
    case OM_E_FIELD: return "bad field";
    case OM_E_FULL: return "buffer full";
    case -86: return "bad loop info";               /* OM_LBG_E_INFO (loadbg.h) */
    case -87: return "loop frame CRC";              /* OM_LBG_E_CRC */
    case -88: return "sound RAM in use by the DSP"; /* OM_AUDIO_E_DSP (menu/app/audio.h) */
    case -89: return "background DMA timed out";    /* OM_LBG_E_DMA */
    case -97: return "sound CPU not held";          /* OM_AUDIO_E_68K (menu/app/audio.h) */
    case -98: return "not attempted";               /* OM_AUDIO_E_NOTTRIED (menu/app/audio.h, M6 Task 19 fold-in) */
    default: break;
    }
    if (err == -120)        /* menu/app/sd.h OM_SD_TIMEOUT == om_sd_err(SAT_ETIMEDOUT),
                              * satiator.h's bounded-exec_cmd sentinel (-20) through om_sd_err's -100+fr */
        return "SATIATOR: NO RESPONSE";
    if (err <= -101 && err >= -119) {                /* om_sd_err(): libsatiator FRESULT fr -> -100 - fr */
        static const char *const fr[19] = { "SD disk error", "SD internal error", "SD not ready", "SD no file",
            "SD no path", "SD invalid name", "SD denied", "SD exists", "SD invalid object", "SD write protected",
            "SD invalid drive", "SD not enabled", "SD no filesystem", "SD mkfs aborted", "SD timeout", "SD locked",
            "SD not enough core", "SD too many open files", "SD invalid parameter" };
        return fr[-101 - err];
    }
    return "error";
}
