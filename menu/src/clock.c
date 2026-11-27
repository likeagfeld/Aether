/* menu/src/clock.c -- see clock.h. BSD-3-Clause. */
#include <string.h>
#include "clock.h"
#include "om_fmt.h"

static int bcd(uint8_t v, uint8_t max, uint8_t *out)       /* HWF-27: two BCD digits per byte */
{
    uint8_t hi = (uint8_t)(v >> 4), lo = (uint8_t)(v & 15u);
    if (hi > 9 || lo > 9 || hi * 10 + lo > max)
        return OM_E_FIELD;
    *out = (uint8_t)(hi * 10 + lo);
    return OM_OK;
}

int om_clock_from_rtc(uint8_t status, uint8_t hour, uint8_t minute, int is_bcd, om_clock *c)
{
    c->valid = c->hour = c->minute = 0;
    if (!(status & OM_RTC_STE))
        return OM_OK;                                   /* never set: the cold-reset default would be a lie */
    if (is_bcd) {
        if (bcd(hour, 23, &c->hour) || bcd(minute, 59, &c->minute))
            return OM_E_FIELD;
    } else {
        if (hour > 23 || minute > 59)
            return OM_E_FIELD;
        c->hour = hour;
        c->minute = minute;
    }
    c->valid = 1;
    return OM_OK;
}

void om_clock_fmt(const om_clock *c, int h24, char out[8])
{
    uint8_t h;
    if (!c->valid) {
        memcpy(out, "--:--", 6);
        return;
    }
    h = c->hour;
    if (!h24) {
        h = (uint8_t)(h % 12u);
        if (!h)
            h = 12;
    }
    out[0] = h >= 10 ? (char)('0' + h / 10) : (h24 ? '0' : ' ');
    out[1] = (char)('0' + h % 10);
    out[2] = ':';
    out[3] = (char)('0' + c->minute / 10);
    out[4] = (char)('0' + c->minute % 10);
    if (h24)
        out[5] = 0;
    else {
        out[5] = c->hour < 12 ? 'A' : 'P';
        out[6] = 0;
    }
}
