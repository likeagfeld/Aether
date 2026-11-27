/* menu/ui/vdp2theme.h -- theme rendering on VDP2 (spec §7.2, decision D1): NBG1 = BG.BIN bitmap, NBG2 = the panels
 * (pixel-exact since Task 13a) with colour calculation against NBG1, NBG0 = SGL's ASCII text (boot and fatal screens).
 * The cursor bar moved to VDP1 in Task 13a (a Gouraud quad that glides per pixel; menu/views/folders.c). BSD-3-Clause. */
#ifndef OM_VDP2THEME_H
#define OM_VDP2THEME_H
#include <stdint.h>
#include "safearea.h"
#include "theme.h"
#define OM_V2_COL_PANEL   1u        /* palette-2 colour indices (CRAM 32 + n, HWF-30) */
#define OM_V2_COL_CURSOR  2u
#define OM_V2_COL_SHADOW  3u
/* slCurColor palette for text_hi: SGL's NBG0 is 256-colour, so slCurColor(c) colours text from CRAM word 256c + dot (HWF-41);
 * c = 1 would hit the NBG1 bitmap palette (256..511), c = 2 -> word 513, which nothing else uses (HWF-30 corrected). */
#define OM_TEXT_HI_PAL    2u
#define OM_CRAM_TEXT      1u        /* NBG0 dot 1 under slCurColor(0) (HWF-30: SGL font dots are {0,1}) */
#define OM_CRAM_TEXT_HI   (256u * OM_TEXT_HI_PAL + 1u)
void om_v2_init(void);
void om_v2_apply_colors(const om_theme *t);
const om_theme *om_v2_theme(void);           /* the colours last applied (the VDP1 UI pre-blends with them) */
void om_v2_bg_gradient(uint16_t top, uint16_t bottom);
int om_v2_bg_load(const uint8_t *buf, uint32_t len);
/* Pixel-exact panels on NBG2: stage up to 8 rectangles (inclusive, screen pixels, painted in order) per
 * frame; end draws edge cells as cached chars and writes only the map cells that changed. soft = the 3 px dithered
 * edge of a drop shadow. An empty rectangle (x1 < x0) is "absent" but keeps its index, so a slot's other contents
 * are compared against the same slot from frame to frame. */
void om_v2_rects_begin(void);
void om_v2_rect(int x0, int y0, int x1, int y1, unsigned col, int soft);
void om_v2_rects_end(void);
void om_v2_rects_flush(void);                 /* finish now what the budget left (call while faded out) */
uint16_t om_v2_bg_at(int x, int y);           /* RGB555 of the NBG1 background at a screen pixel */
/* the safe-area transform om_v2_rect and om_v2_bg_at map through (NULL = identity), as om_v1_set_safe */
void om_v2_set_safe(const om_safe *t);
void om_v2_vblank(void);                      /* the V-blank hook (slIntFunction): counts blanks, applies map writes */
uint32_t om_v2_vblanks(void);                 /* V-blanks counted by the hook */
/* Hardware flicker instrument (2026-11-19, hw-flicker-report.md): what the V-blank hook and rects_end saw since the
 * last om_v2_hwstat_reset (the Folders view's start). The diagnostics screen (page 2) shows it. */
typedef struct {
    uint32_t vbl;          /* hooks counted (fields) */
    uint32_t late_in;      /* hook entered with TVSTAT VBLANK already clear (HWF-10): SGL's blank-in work ran long */
    uint32_t late_out;     /* hook left with VBLANK clear after writing map cells: writes reached the active display */
    uint32_t max_w;        /* most map cells one hook wrote */
    uint32_t cpu_flush;    /* rects_end flushed a list the hook had not written yet (written during the display) */
    uint32_t direct;       /* cells rects_end wrote at once (more than PEND_MAX changed) */
    uint32_t carry;        /* rects_end calls that left cells for later frames (the per-frame cell budget) */
    uint32_t carry_run;    /* the longest run of consecutive such calls: frames NBG2 lagged the VDP1 picture */
    uint32_t v1_over;      /* fields whose EDSR BEF read 0: VDP1 had not finished the previous frame (transfer-over) */
} om_v2_stat;
void om_v2_hwstat_reset(void);
const om_v2_stat *om_v2_hwstat(void);
#endif
