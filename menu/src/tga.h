/* menu/src/tga.h -- BOX.TGA decoder (spec §5.6 step 1) into the 25,600-pixel RGB555 hero canvas. BSD-3-Clause. */
#ifndef TGA_H
#define TGA_H
#include <stdint.h>
#include "om_fmt.h"

#define OM_TGA_SQUARE  0
#define OM_TGA_TALL    1
#define OM_TGA_E_SHORT -30
#define OM_TGA_E_TYPE  -31
#define OM_TGA_E_BPP   -32
#define OM_TGA_E_SIZE  -33
#define OM_TGA_E_DATA  -34

typedef struct { uint16_t w, h; uint8_t layout, scale; } om_tga_info;

int om_tga_decode(const uint8_t *file, uint32_t len, uint16_t *canvas, om_tga_info *info);
const char *om_tga_strerror(int err);
#endif
