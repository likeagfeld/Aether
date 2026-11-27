/* menu/src/idle.h -- the screensaver's idle timer, in V-blanks (om_v2_vblanks; unsigned
 * differences survive the wrap). Pure C, host-tested (menu/host-tests/test_idle.c). BSD-3-Clause. */
#ifndef OM_IDLE_H
#define OM_IDLE_H
#include <stdint.h>

typedef struct { uint32_t last, timeout; uint8_t active; } om_idle;
enum { OM_IDLE_RUN, OM_IDLE_START, OM_IDLE_WAKE };
uint32_t om_idle_vblanks(uint8_t minutes, uint32_t fps_x100);   /* 0 = never */
void om_idle_init(om_idle *i, uint32_t timeout, uint32_t now);
void om_idle_set(om_idle *i, uint32_t timeout, uint32_t now);    /* a new timeout, counting from now (0: off) */
/* input: any button pressed or held this frame. START once the timeout passes without input; then WAKE on the first
 * input (the caller acts on nothing else that frame); a button still held after the wake is plain activity. */
int om_idle_step(om_idle *i, uint32_t now, int input);
#endif
