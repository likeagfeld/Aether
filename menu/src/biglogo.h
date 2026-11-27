/* menu/src/biglogo.h -- the big Saturn logo (152 x 101) as the file BIGLOGO.BIN (contract/SPEC.md §5.19; magic
 * OMSATBLG; tools/omsat/gen_biglogo.py). It is on the menu disc and read at boot into LWRAM (OM_LW_BIGLOGO); the
 * interstitial (menu/views/interstitial.c) and the screensaver blend it into VDP1 VRAM from there. Until M6 its table
 * was HWRAM .rodata. Pure C, host-tested (menu/host-tests/test_biglogo.c). BSD-3-Clause. */
#ifndef OM_BIGLOGO_H_INCLUDED
#define OM_BIGLOGO_H_INCLUDED
#include <stdint.h>
#include "logoblend.h"

#define OM_BIGLOGO_W 152u
#define OM_BIGLOGO_H 101u
#define OM_BIGLOGO_MAGIC "OMSATBLG"

/* BIGLOGO.BIN in file[0, len): the common header (magic, version, CRC-32 of the table, the table inside the file) and
 * an entry count of 1; *tbl / *tlen = the table (the v2 logo: u16 w, h, n, ops length, n palette words, n alpha bytes
 * (+1 if n is odd), the codec-2 ops). 0 or an OM_E_* code (*tbl untouched then). */
int om_biglogo_open(const uint8_t *file, uint32_t len, const uint8_t **tbl, uint32_t *tlen);
/* The table's indices decoded into scratch (at least OM_BIGLOGO_W * OM_BIGLOGO_H B, handed back zeroed), then blended
 * at screen (sx, sy) over bg (NULL = black) into out (152 * 101 RGB555 words, 16-bit writes, HWF-23). Refuses a table
 * whose size is not 152 x 101, whose n is 0 or above 255, or whose ops run past it. 0, or OM_BA_E_* / OM_E_*. */
int om_biglogo_blend(const uint8_t *tbl, uint32_t tlen, uint8_t *scratch, uint32_t slen, int sx, int sy,
                     om_backdrop_fn bg, void *ctx, volatile uint16_t *out);
#endif
