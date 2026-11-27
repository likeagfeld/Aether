/* menu/src/saver.h -- decision 8: the screensaver logo's motion. A DVD-style bounce inside
 * the safe area at 0.75 / 0.5 px a frame (Q8), and a 64-frame coin-spin (a horizontal squash by |cos|) on each bounce
 * and every 480 frames. Pure C, host-tested (menu/host-tests/test_saver.c). BSD-3-Clause. */
#ifndef OM_SAVER_H
#define OM_SAVER_H
#include <stdint.h>

#define OM_SAVER_VX 192                 /* 0.75 px per frame, Q8 */
#define OM_SAVER_VY 128                 /* 0.5 px per frame */
#define OM_SAVER_SPIN_LEN 64
#define OM_SAVER_SPIN_EVERY 480         /* frames (8 s at 60 fps) since the last spin began */
typedef struct { int32_t x, y, vx, vy; int16_t w, h, x0, y0, x1, y1; uint16_t t, spin; } om_saver;
typedef struct { int16_t x0, y0, x1, y1; } om_saver_pose;   /* the logo's drawn rectangle, inclusive */
/* w x h logo inside the inclusive area (x0, y0)-(x1, y1); seed picks the start point and directions. An axis with no
 * room to move stands still. */
void om_saver_init(om_saver *s, int w, int h, int x0, int y0, int x1, int y1, uint32_t seed);
void om_saver_step(om_saver *s, om_saver_pose *p);
#endif
