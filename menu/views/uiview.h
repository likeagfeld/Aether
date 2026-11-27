/* menu/views/uiview.h -- what the Folders view and its overlays share: the theme-derived colours of the
 * approved mockup, pre-blended for VDP1 (no per-pixel alpha on VDP1 over VDP2: HWF-32, decision D1), the Z bands, and
 * the Gouraud-table slots. BSD-3-Clause. */
#ifndef OM_UIVIEW_H
#define OM_UIVIEW_H
#include <stdint.h>

typedef struct {
    uint16_t txt, row, dim, faint, acc, acc2, gold, folder, err;   /* RGB555 | 0x8000 (VDP1 RGB codes, HWF-01) */
    uint16_t panel;          /* the theme panel colour (CRAM 33) */
    uint8_t ratio;           /* NBG2's colour-calculation ratio register (HWF-31) */
    uint16_t list_eff;       /* the list panel as seen: panel over the background at the ratio (D1) */
    uint16_t det_eff, ov_eff;
    uint16_t bar_l, bar_r;   /* the cursor bar's ends: accent at 235/255 and at 45 % of that over list_eff */
    uint16_t ovbar_l, ovbar_r, ovbar_base;   /* ovbar_base: the Gouraud quad's COLR for OM_G_OVBAR */
} om_style;

/* Gouraud tables at OM_VRAM_GOUR + 8 n (VDP1_Manual.txt 5.3, HWF-43) */
enum { OM_G_BAR, OM_G_OVBAR, OM_G_HAIR_L, OM_G_HAIR_R, OM_G_COVER, OM_G_LD_REFL, OM_G_DIM, OM_G_COUNT };
/* LD: Task 13c; DIM: the header logo behind an open panel (folders.c) */

/* Z bands (menu/ui/v1.h): bigger is further back; equal Z keeps submission order (HWF-42, emulator-tier). */
enum {
    Z_HAIR = 199, Z_LOGO = 198, Z_FRAME = 197, Z_COVER = 196, Z_SHIMCLIP = 195, Z_SHIMMER = 194, Z_NOART = 193,
    Z_LISTCLIP = 188, Z_BAR = 187, Z_SCROLL = 186, Z_ROWS = 185, Z_SELCLIP = 183, Z_SEL = 182,
    Z_HDR = 178, Z_CRUMBCLIP = 177, Z_CRUMB = 176, Z_COUNT = 175,
    Z_DTCLIP = 172, Z_DTITLE = 171, Z_DMCLIP = 170, Z_DMETA = 169,
    Z_OVCLIP = 166, Z_OVFRAME = 165, Z_OVBAR = 164, Z_OVTXT = 163, Z_OVVALCLIP = 160, Z_OVVAL = 159,
    Z_FOOTCLIP = 150, Z_FOOT = 149, Z_REASONCLIP = 147, Z_REASON = 146, Z_RCCLIP = 144, Z_RC = 143
};

/* colours */
uint16_t om_ui_mix(uint16_t a, uint16_t b, uint32_t a255);        /* b over a at alpha a255/255, RGB code */
#endif
