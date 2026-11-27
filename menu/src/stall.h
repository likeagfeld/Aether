/* menu/src/stall.h -- the hang watchdog's rule (menu/app/hang.h runs it from the V-blank hook): the menu's frame beat
 * (every om_pad_poll) not moving for OM_STALL_VB V-blanks shows the stall once; the beat moving again clears it, so
 * only a stall still going on stays on screen. Pure: the host test drives it with made-up beats. BSD-3-Clause. */
#ifndef OM_STALL_H
#define OM_STALL_H
#include <stdint.h>

#define OM_STALL_VB 240u            /* ~4 s at 60 Hz: past every legitimate wait but a Satiator timeout (~28 s) */

typedef struct { uint32_t beat, n; uint8_t shown; } om_stall;
enum { OM_STALL_NONE, OM_STALL_SHOW, OM_STALL_CLEAR };

/* one V-blank: the beat as it is now; OM_STALL_SHOW once a stall reaches limit V-blanks, OM_STALL_CLEAR when the beat
 * moves after one was shown */
int om_stall_step(om_stall *s, uint32_t beat, uint32_t limit);
/* out = label, then each of v[0..n) as " xxxxxxxx" (n <= 4 with a label of up to 4 characters: <= 40 columns) */
void om_stall_hex(char *out, const char *label, const uint32_t *v, int n);
#endif
