/* menu/src/padgest.h -- the error screens' "C: reset console" gesture (review-final-m3 I4, Ruling M3-R28a / M3-R15d).
 * C is also part of A+B+C+Start, so a plain C press would reset a console the user is about to return to the official
 * menu. The gesture fires on C's RELEASE, and only when C was the only button held from its press to its release; any
 * other button seen meanwhile disarms it until every button is up. A button held when the screen appears is not a
 * press (the caller seeds `st` with om_cgest_init(held)). Pure C: the menu (SGL pad), the launcher (SMPC pad) and the
 * host tests share it. BSD-3-Clause. */
#ifndef OM_PADGEST_H
#define OM_PADGEST_H
#include <stdint.h>
typedef struct { uint16_t prev; uint8_t armed, spoiled; } om_cgest;
void om_cgest_init(om_cgest *g, uint16_t held);
/* One sample per frame (held: pressed = 1). Returns 1 on the frame C-alone is released. */
int om_cgest_step(om_cgest *g, uint16_t held, uint16_t c_mask);
#endif
