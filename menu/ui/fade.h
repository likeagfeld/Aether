/* menu/ui/fade.h -- screen fades through VDP2 color offset A (SGL slColOffset*), Task 13b fix round 1,
 * shared with Task 13c. mask = SGL's color-offset screen bits (NBG0ON..NBG3ON, RBG0ON, SPRON, BACKON). The caller owns
 * slScrDisp. Durations follow the anim= setting (om_anim_frames: off = instant). BSD-3-Clause. */
#ifndef OM_FADE_H
#define OM_FADE_H
#include <stdint.h>

typedef void (*om_fade_each)(void *ctx);           /* before every slSynch of the ramp: resubmit that frame's sprites */
/* Offset from -> to (-255..255, all three channels) over ms; slSynch per step. Ending at 0 switches the offset off. */
void om_fade(uint16_t mask, int from, int to, uint16_t ms, uint8_t anim, om_fade_each each, void *ctx);
void om_fade_out(uint16_t mask, uint16_t ms, uint8_t anim, om_fade_each each, void *ctx);   /* veil/0 -> -255, stays on */
void om_fade_in(uint16_t mask, uint16_t ms, uint8_t anim, om_fade_each each, void *ctx);    /* -255 -> 0, then off */
void om_fade_black(uint16_t mask);                 /* -255 at once (applied at the next slSynch) */
/* The menu's veil behind an open panel: offset r, g, b (-255..0) on mask, applied only when it changes; all 0 switches
 * it off. A fade-out starts from it (no brightening first); any fade, and om_fade_clear, end it. */
void om_fade_veil(uint16_t mask, int r, int g, int b);
void om_fade_clear(uint16_t mask);                 /* offset 0 and off, the veil forgotten */
#endif
