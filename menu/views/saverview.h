/* menu/views/saverview.h -- the screensaver screen's logo. It owns only the logo; the menu's
 * fades and the frame stay in folders.c (they use its statics). The motion is menu/src/saver.c. BSD-3-Clause. */
#ifndef OM_SAVERVIEW_H
#define OM_SAVERVIEW_H
#include <stdint.h>

/* The art job canceled (OM_LW_STAGE free), the big logo (BIGLOGO.BIN in LWRAM) blended against black into
 * OM_VRAM_SAVER, the motion reset inside the safe area of `margin` px. 0, or the logo's error (then no logo is drawn;
 * the saver is a black screen and the music plays on). */
int om_saver_begin(uint32_t seed, int margin);
void om_saver_draw(void);            /* this frame's logo: one scaled sprite, at identity; no slSynch */
int om_saver_rc(void);               /* om_saver_begin's result (diagnostics) */
#endif
