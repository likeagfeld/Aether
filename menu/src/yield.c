/* menu/src/yield.c -- see yield.h. BSD-3-Clause. */
#include "yield.h"

void (*om_yield_fn)(void);
void (*om_yield_draw_fn)(void);
volatile uint8_t om_yield_wr;
