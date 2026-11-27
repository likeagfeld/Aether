/* menu/ui/vdp1tex.c -- see vdp1tex.h. TEXTURE = {Hsize, Vsize, CGadr = VRAM byte address / 8,
 * HVsize = ((w & 0x1F8) << 5) | h} exactly as SGL.H:29 TEXDEF builds it (HWF-06). Addresses: omlayout.h (HWF-08). */
#include "omlayout.h"
#include "vdp1tex.h"

#define HV(w, h) ((Uint16)((((w) & 0x1F8) << 5) | (h)))

/* aligned(4): SGL's slave reads TEXTURE+4 with one mov.l (EntryZbuf); at 2 mod 4 it takes an address error and
 * slSynch hangs (SH7604 HW manual 4.3.1; debug-folders-vdp1-hang.md). sbuild check_sgl_tables gates it. */
TEXTURE om_tex[OM_TEX_COUNT] __attribute__((aligned(4))) = {
    { 160, 160, OM_VRAM_HERO_A >> 3, HV(160, 160) },
    { 128, 200, OM_VRAM_HERO_A >> 3, HV(128, 200) },
    { 160, 160, OM_VRAM_HERO_B >> 3, HV(160, 160) },
    { 128, 200, OM_VRAM_HERO_B >> 3, HV(128, 200) },
    { OM_JP_CANVAS_W, OM_JP_CANVAS_H, OM_VRAM_JP_A >> 3, HV(OM_JP_CANVAS_W, OM_JP_CANVAS_H) },
    { OM_JP_CANVAS_W, OM_JP_CANVAS_H, OM_VRAM_JP_B >> 3, HV(OM_JP_CANVAS_W, OM_JP_CANVAS_H) },
    { 160, 160, OM_VRAM_PLACEHOLDER >> 3, HV(160, 160) },   /* sentinel only ("no art"): never drawn, not uploaded */
    { OM_LOGO_W, OM_LOGO_H, OM_VRAM_LOGO >> 3, HV(OM_LOGO_W, OM_LOGO_H) },   /* header logo, menu/ui/logo.c */
};
