/* menu/ui/rtc.h -- the top-bar clock source (spec §7.4). BSD-3-Clause. */
#ifndef OM_RTC_H
#define OM_RTC_H
#include "clock.h"
int om_rtc_tick(om_clock *out);     /* call once per frame; 1 = *out holds a new value to draw */
int om_rtc_rc(void);                /* rc of the last decode: 0, or OM_E_FIELD for a bad RTC byte */
#endif
