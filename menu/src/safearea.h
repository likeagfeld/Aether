/* menu/src/safearea.h -- the overscan (safe-area) levels (carried from M3.5 Task 4) and the
 * transform that insets the layout. The layout table (layout.h) is authored at an 8 px margin; a level's margin m maps
 * a design-space pixel edge e (0..320 across, 0..224 down) to m + round((e - 8) * (W - 2m) / (W - 16)), clamped to
 * [0, W], so the whole UI scales about the screen center. Standard (m = 8) is the identity. BSD-3-Clause. */
#ifndef OM_SAFEAREA_H
#define OM_SAFEAREA_H
#include <stdint.h>
enum { OM_OVS_OFF, OM_OVS_STD, OM_OVS_LARGE, OM_OVS_MAX, OM_OVS_COUNT };
int om_safe_margin(uint8_t level);       /* 0, 8, 16, 24; out of range -> 8 */
const char *om_safe_key(uint8_t level);  /* "off" "8" "16" "24" */
const char *om_safe_label(uint8_t level);/* "Off" "8 px" "16 px" "24 px" */

#define OM_SAFE_DESIGN 8                /* the margin the layout table is authored at (layout.h, test_layout) */
#define OM_SAFE_BOOT   24               /* boot screens (animation tag, interstitial) always sit inside this */
typedef struct { uint8_t level, ident; int16_t m; } om_safe;
extern om_safe g_om_safe;               /* the level in force; starts at Standard */
void om_safe_set(om_safe *t, uint8_t level);   /* out of range -> Standard */
int om_safe_x(const om_safe *t, int e); /* a design edge -> a screen edge */
int om_safe_y(const om_safe *t, int e);
/* An inclusive box [x0, x1] x [y0, y1] -> [f(x0), f(x1 + 1) - 1] x [f(y0), f(y1 + 1) - 1]; f is monotone, so boxes
 * that touch stay touching (no gaps, no overlaps). out = { x0, y0, x1, y1 }, inclusive, never inverted. */
void om_safe_rect(const om_safe *t, int x0, int y0, int x1, int y1, int out[4]);
/* The same without the clamp to the screen, for drawing (menu/ui/v1.c, vdp2theme.c): a part that is partly off screen
 * (a marquee copy, the settings panel sliding in from the right) keeps its size instead of being squeezed against the
 * edge. Equal to the clamped forms wherever those do not clamp. */
int om_safe_xu(const om_safe *t, int e);
int om_safe_yu(const om_safe *t, int e);
void om_safe_rect_u(const om_safe *t, int x0, int y0, int x1, int y1, int out[4]);
#endif
