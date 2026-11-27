/* menu/src/artsched.h -- spec §7.4 "Art loading": read only after the cursor rests ~150 ms; double-buffered, the old
 * image stays until the new one is complete. BSD-3-Clause. */
#ifndef ARTSCHED_H
#define ARTSCHED_H
#include <stdint.h>

#define OM_ART_NONE 0xFFFFFFFFu

typedef struct {
    uint32_t want, shown, loading_item;
    uint16_t still, debounce;
    uint8_t loading, front;
} om_artsched;

void om_artsched_init(om_artsched *s, uint16_t debounce_frames);
void om_artsched_select(om_artsched *s, uint32_t item);
int om_artsched_should_start(om_artsched *s);
int om_artsched_stale(const om_artsched *s);
void om_artsched_abort(om_artsched *s);
void om_artsched_done(om_artsched *s, int ok);
uint8_t om_artsched_back(const om_artsched *s);
uint16_t om_debounce_frames(uint32_t hz_x100, uint32_t ms);
#endif
