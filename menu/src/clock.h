/* menu/src/clock.h -- SMPC RTC (INTBACK status, HWF-27) to the top-bar clock (spec §7.4). BSD-3-Clause. */
#ifndef OM_CLOCK_H
#define OM_CLOCK_H
#include <stdint.h>
#define OM_RTC_STE 0x80u            /* OREG0 bit7: SETTIME done since SMPC cold reset (HWF-27) */
typedef struct { uint8_t valid, hour, minute; } om_clock;
int om_clock_from_rtc(uint8_t status, uint8_t hour, uint8_t minute, int is_bcd, om_clock *c);
void om_clock_fmt(const om_clock *c, int h24, char out[8]);   /* "23:59", "11:59P", " 9:30A", "--:--" */
#endif
