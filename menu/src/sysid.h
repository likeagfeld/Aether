/* menu/src/sysid.h -- Saturn System ID (IP.BIN 0x000-0x0FF) per ST-040-R4 §4.2; raw offsets per contract v1
 * clarification 4. BSD-3-Clause. */
#ifndef SYSID_H
#define SYSID_H
#include <stdint.h>
#include "om_fmt.h"

typedef struct {
    char maker[17], product[11], version[7], date[9], device[9], area[11], periph[17], title[113];
    uint8_t disc, disc_total, area_flags;
} om_sysid;

int om_sysid_parse(const uint8_t *buf, uint32_t len, om_sysid *s);
uint8_t om_area_flags(const char *area);
void om_disc_n_m(const char *device, uint8_t *n, uint8_t *m);
int om_cue_first_file(const char *text, uint32_t len, char *out, uint32_t cap);
/* 2026-11-23 (SPEC v1 clarification 7): every FILE line's file name, path components stripped, NUL-separated into out
 * (names that no longer fit are dropped); a FILE line with no name or an unclosed quote is skipped. Returns the count. */
int om_cue_files(const char *text, uint32_t len, char *out, uint32_t cap);
#endif
