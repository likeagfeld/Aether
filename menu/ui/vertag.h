/* menu/ui/vertag.h -- the small "FARKUS 0.1" tag at the bottom right of the startup screen (user decision): drawn
 * by the boot animation (menu/app/bootplay.c) and the library-loading interstitial (menu/views/interstitial.c), never
 * by the menu. One VDP1 lookup-table sprite in its own VRAM slot (omlayout.h OM_VRAM_VERTAG, outside the boot-only
 * range both of those screens overwrite), rasterised once in the 13a/13d label face (om_face_label8). BSD-3-Clause. */
#ifndef OM_VERTAG_H
#define OM_VERTAG_H
#include <stdint.h>
void om_vertag_init(void);      /* rasterise once; cheap to call again (does nothing the second time) */
/* Draw right-aligned inside the 8 px safe area. bg: the RGB555 under the tag; a256: 256 = the tag's full (40 %) level,
 * 0 = not drawn. */
void om_vertag_draw(uint16_t bg, uint32_t a256, int z);
#endif
