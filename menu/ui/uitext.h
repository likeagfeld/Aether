/* menu/ui/uitext.h -- proportional anti-aliased text as VDP1 sprites. A slot owns a texture and its own
 * 16-colour LUT in the UI pool of VDP1 VRAM (omlayout.h OM_VRAM_UI). om_ut_text rasterises a string with a face
 * (menu/src/uifont.c) into a 4 bpp buffer and uploads it, only when the face or string changed; om_ut_colors writes the
 * LUT, only when the colours changed. The LUT ramps from the colour UNDER the text to the text colour: VDP1 has no
 * per-pixel alpha, so the anti-aliasing is pre-blended against the known backdrop (index 0 stays transparent, HWF-02).
 * Drawing is a raw lookup-table sprite (menu/ui/v1.c). BSD-3-Clause. */
#ifndef OM_UITEXT_H
#define OM_UITEXT_H
#include <stdint.h>
#include "uifont.h"
#include "v1.h"

typedef struct {
    uint32_t lut, tex;          /* VDP1 VRAM offsets */
    uint32_t blut;              /* 3 band LUTs (1/4, 2/4, 3/4 strength) for a marquee's faded edges; 0 = none */
    uint32_t key;               /* hash of face + string; 0 = nothing rasterised */
    uint16_t cap, h;            /* capacity: width (multiple of 8) and height in pixels */
    uint16_t w8, w;             /* the current texture width (multiple of 8) and the drawn width */
    uint16_t full_w;            /* the string's whole width (> w when it was cut to its clip width and faded) */
    uint16_t fg, bg, fade, c3;  /* LUT inputs last written (c3: the badge letter colour) */
    uint8_t face, asc, lut_ok, blut_ok;   /* blut_ok: the band LUTs match the main LUT (written on first marquee use) */
} om_ut;

void om_ut_reset_pool(void);
uint32_t om_ut_pool_used(void);                         /* bytes of OM_VRAM_UI handed out */
int om_ut_pool_cap(uint32_t end);                       /* no allocation past end from now on (OM_E_FULL if one is) */
int om_ut_alloc(om_ut *t, int cap, int h);              /* OM_E_FULL if the pool is out of room */
/* Rasterise s in face (only if face, string or clipw changed); returns 1 if it re-rasterised, 0 if unchanged, -1 if the
 * frame's raster time is spent (the old texture stays). clipw > 0: a FADE element, cut at clipw px with a 12 px
 * fade-out (addendum 3); clipw < 0: a MARQUEE element in a box of -clipw px -- the whole string when it fits the
 * slot's capacity, else cut and faded at the box; clipw 0: a FIT element. Never an ellipsis. */
#define OM_UT_FADE 12
int om_ut_text(om_ut *t, int face, const char *s, int clipw);
uint32_t om_ut_key(int face, const char *s, int clipw);
int om_ut_text_tracked(om_ut *t, int face, const char *s, int track8);   /* a FIT element, letter-spaced */
/* Review-13a fix-plan 2: rasterisation is budgeted in time, not count. The view sets the frame's budget and asks in
 * priority order (selected row > details > visible rows > breadcrumb > prefetch); a request past it returns -1. */
void om_ut_budget_us(int32_t us);
int32_t om_ut_budget_left(void);
/* A double-buffered slot: text changes are rasterised into the back slot and swapped in when done. */
typedef struct { om_ut s[2]; uint8_t front; } om_ut2;
int om_ut2_alloc(om_ut2 *d, int cap, int h);
int om_ut2_text(om_ut2 *d, int face, const char *s, int clipw);
#define om_ut2_front(d) (&(d)->s[(d)->front])
/* Upload a 4 bpp icon once (it must fit the slot). */
void om_ut_icon(om_ut *t, const om_icon *ic);
/* A region flag (addendum 6) in a 16x8 slot, and its LUT: the flag's palette faded toward bg by level (256 = full). */
void om_ut_flag(om_ut *t, const om_flag *f);
void om_ut_flag_colors(om_ut *t, const om_flag *f, uint16_t bg, uint16_t level256);
/* Text LUT: index i = mix(bg, fg, i/15), then dimmed toward bg by fade (256 = full text, 0 = all background). */
void om_ut_colors(om_ut *t, uint16_t fg, uint16_t bg, uint16_t fade256);
/* Badge LUT (uifont.py): 1..8 bg -> fill, 8..15 fill -> letter. */
void om_ut_badge_colors(om_ut *t, uint16_t bg, uint16_t fill, uint16_t letter);
/* Draw with the top-left at (x, y); om_ut_draw_base puts the baseline at `base` instead. */
void om_ut_draw(const om_ut *t, int x, int y, uint16_t pmod, int z);
void om_ut_draw_base(const om_ut *t, int x, int base, uint16_t pmod, int z);
/* Draw inside the text box x0..x1 with the baseline at `base`: fitting (or fade-cut) text plainly, left or right aligned
 * (with its clip at zclip when pmod has OM_V1_CLIP, for sliding parts); wider text as a marquee: off and period from
 * menu/src/marquee.c, two copies inside a user clip of the box issued at zclip (> z). */
void om_ut_draw_box(const om_ut *t, int x0, int x1, int base, int right, int32_t off, int32_t period, uint16_t pmod,
                    int zclip, int z);
/* A marquee (addendum 9): the text scrolled by off inside x0..x1 (clip rows y0..y1), with copies every `period` px,
 * and its clipped edges faded over OM_UT_FADE px -- the right edge whenever the text runs past it, the left edge once
 * it has started to scroll (off > 0). The fade is LUT banding: three 4 px bands each side redraw the same texture
 * inside their own user clip with a band LUT at 3/4, 2/4 and 1/4 strength, so nothing is re-rasterised per frame.
 * All of one marquee's clips and parts share Z `z` and rely on submission order at equal Z (HWF-42). */
void om_ut_marquee(const om_ut *t, int x, int x0, int x1, int y0, int y1, int base, int32_t off, int32_t period, int z);
/* Scaled into (x0,y0)-(x1,y1) inclusive: the favourite star's pop. */
void om_ut_draw_scaled(const om_ut *t, int x0, int y0, int x1, int y1, uint16_t pmod, int z);

/* Rasterisation timing for the report (FRT, SH7604 hardware manual ch. 11): the last and the worst row, in us. */
uint32_t om_ut_last_us(void);
uint32_t om_ut_worst_us(void);
uint32_t om_ut_raster_count(void);
uint32_t om_ut_take_frame_us(void);                     /* rasterisation us since the last call (phase timing) */
uint16_t om_ut_frc(void);                               /* the FRT counter, for frame-cost measurements */
uint32_t om_ut_frc_us(uint16_t ticks);
#endif
