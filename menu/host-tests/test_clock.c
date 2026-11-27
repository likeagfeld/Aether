/* menu/host-tests/test_clock.c -- SMPC RTC decode (HWF-27) and the top-bar format. BSD-3-Clause. */
#include "th.h"
#include "clock.h"
#include "om_fmt.h"

static const char *fmt(uint8_t st, uint8_t h, uint8_t m, int bcd, int h24)
{
    static char out[8];
    om_clock c;
    om_clock_from_rtc(st, h, m, bcd, &c);
    om_clock_fmt(&c, h24, out);
    return out;
}

int main(void)
{
    om_clock c;
    CHECK_STR(fmt(0x00, 0x23, 0x59, 1, 1), "--:--");             /* STE = 0: never set since cold reset */
    CHECK_STR(fmt(0x80, 0x23, 0x59, 1, 1), "23:59");
    CHECK_STR(fmt(0x80, 0x23, 0x59, 1, 0), "11:59P");
    CHECK_STR(fmt(0x80, 0x00, 0x05, 1, 1), "00:05");
    CHECK_STR(fmt(0x80, 0x00, 0x05, 1, 0), "12:05A");
    CHECK_STR(fmt(0x80, 0x12, 0x00, 1, 0), "12:00P");
    CHECK_STR(fmt(0x80, 0x09, 0x30, 1, 0), " 9:30A");
    CHECK_STR(fmt(0xC0, 0x09, 0x30, 1, 1), "09:30");             /* other OREG0 bits (RESD) do not matter */
    CHECK_INT(om_clock_from_rtc(0x80, 0x2A, 0x00, 1, &c), OM_E_FIELD);   /* not BCD */
    CHECK_INT(c.valid, 0);
    CHECK_INT(om_clock_from_rtc(0x80, 0x24, 0x00, 1, &c), OM_E_FIELD);   /* hour 24 */
    CHECK_INT(om_clock_from_rtc(0x80, 0x10, 0x60, 1, &c), OM_E_FIELD);   /* minute 60 */
    CHECK_STR(fmt(0x80, 0x24, 0x00, 1, 1), "--:--");
    CHECK_STR(fmt(0x80, 23, 59, 0, 1), "23:59");                  /* binary mode, if HWF-27 says SGL decodes */
    CHECK_INT(om_clock_from_rtc(0x80, 24, 0, 0, &c), OM_E_FIELD);
    return th_done("test_clock");
}
