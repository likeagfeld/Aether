/* menu/src/bgfmt.h -- BG.BIN (contract/SPEC.md §5.7): 64-byte common header "OMSATBGI", 256 x RGB555 palette (VDP2
 * CRAM mode 0, HWF-30), 320x224 8-bpp pixels. BSD-3-Clause. */
#ifndef OM_BGFMT_H
#define OM_BGFMT_H
#include <stdint.h>
#include "om_fmt.h"
#define OM_BG_W    320u
#define OM_BG_H    224u
#define OM_BG_FILE (64u + 512u + OM_BG_W * OM_BG_H)
typedef struct { const uint8_t *pal, *pix; } om_bg;
int om_bg_open(om_bg *b, const uint8_t *buf, uint32_t len);
uint16_t om_bg_color(const om_bg *b, uint32_t i);
#endif
