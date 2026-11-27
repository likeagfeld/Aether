/* menu/src/marquee.h -- the one marquee style (controller addendum 3), reusable (Task 13c's loading screen): text wider
 * than its box holds 1.0 s, scrolls left at about 30 px/s with smoothstep easing at both ends (so the speed peaks at
 * 1.5x the average), and wraps continuously: a second copy follows at text width + OM_MQ_GAP, and when it reaches the
 * start the cycle (hold included) begins again. The view draws the copy at x0 - off and x0 - off + om_marquee_period(),
 * inside a VDP1 user clip of the box, never re-rasterising. Portable C, host-tested (test_marquee.c). BSD-3-Clause. */
#ifndef OM_MARQUEE_H
#define OM_MARQUEE_H
#include <stdint.h>

enum { OM_MQ_FITS, OM_MQ_SCROLL, OM_MQ_FADE };
/* Review-13a I1: what a marquee element does -- fits; scrolls (its whole texture fits the raster cap); or, wider than
 * the cap, is rasterised at box width and fade-cut (it never draws past its box). */
int om_marquee_mode(int text_w, int box_w, int cap);

#define OM_MQ_GAP   24           /* px between the end of the text and its next copy */
#define OM_MQ_SPEED 30           /* px per second, average over the travel */

/* Travel distance of one cycle: text_w + OM_MQ_GAP, or 0 when the text fits its box (no marquee). */
int32_t om_marquee_period(int text_w, int box_w);
/* Frames in one cycle (hold + travel) at fps_x100 (5994 or 5000); 0 when it fits. */
uint32_t om_marquee_cycle(int text_w, int box_w, uint32_t fps_x100);
/* Offset in px (0 .. period-1) at frame t since the marquee (re)started. */
int32_t om_marquee_off(uint32_t t, int text_w, int box_w, uint32_t fps_x100);
#endif
