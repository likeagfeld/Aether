/* menu/ui/rtc.c -- SMPC RTC through INTBACK status (HWF-27), polled once a second through SGL's own command path so it
 * cannot collide with SGL's per-frame INTBACK for the pads:
 *  - slGetStatus() is slRequestCommand(SMPC_GETSTS, SMPC_NO_WAIT) (SL_DEF.H:1550). It never touches an SMPC register:
 *    measured this session in LIBSGL.A sglH00.o, _slRequestCommand (+0xa44) takes SGL's SMPC semaphore with tas.b
 *    (SLPROG +0x0, 0x060FFFB8) and _slExecGetStatus (+0x968) only rewrites SGL's INTBACK parameter word (IREG0 = 0x01,
 *    status + peripherals) and sets a flag (_slSetIntBack +0x4be); SGL's own frame code issues the INTBACK.
 *  - Its return value is OK (0) on success and NG (-1) when the semaphore is held (SL_DEF.H:63-64; ST-238-R1-051795.html
 *    :5825-5848 "Return Value OK: Successful NG: Failed"; disassembly: SLPROG returns T-1, so 0 = got it). The brief's
 *    `if (slGetStatus())` would therefore be true on FAILURE; this file tests `== OK` instead.
 *  - ST-238-R1 :5825-5848: "The settings are reflected in the data starting from the second frame after the settings
 *    are made", so Smpc_Status is read two frames after an accepted request (HWF-16).
 *  - The mode is persistent (HWF-40, traced in review-11): _slExecGetStatus sets SmpcControlFlag bit 1, the per-frame
 *    _slSmpcCashSend (sglH00 +0x698) re-issues the INTBACK every frame through _slComPrmIntBack (sglH04 +0x0, IREG0-2
 *    from 0x060FFFA8), and only _slIntBackCancel (+0x49e) clears the bit. main.c:88 set it at boot, so after boot the
 *    once-a-second request rewrites an identical word and is effectively a no-op; Smpc_Status is fresh every frame.
 * A bad RTC byte (om_clock_from_rtc OM_E_FIELD) shows --:-- and is reported through om_rtc_rc() so the view can name it
 * once; STE = 0 is not an error (rc 0, --:--). BSD-3-Clause. */
#include "sgl.h"
#include "rtc.h"

#define OM_RTC_IS_BCD 1             /* HWF-27 CONFIRMED: SGL mirrors OREG4-7 raw BCD (sglH03.o _slGetSmpcStatus) */
#define OM_RTC_SETTLE 2             /* frames from an accepted request to valid data (ST-238-R1 slGetStatus remarks) */

static uint16_t s_frames = 59;
static uint8_t s_pending, s_have;
static int s_rc;
static om_clock s_last;

int om_rtc_rc(void) { return s_rc; }

int om_rtc_tick(om_clock *out)
{
    om_clock c;
    int rc;
    if (s_pending) {
        if (--s_pending)
            return 0;
        rc = om_clock_from_rtc(Smpc_Status->cond, Smpc_Status->rtc.hour, Smpc_Status->rtc.minute, OM_RTC_IS_BCD, &c);
        if (!s_have || rc != s_rc || c.valid != s_last.valid || c.hour != s_last.hour || c.minute != s_last.minute) {
            s_last = c;
            s_rc = rc;
            s_have = 1;
            *out = c;
            return 1;
        }
        return 0;
    }
    if (++s_frames >= 60) {
        if (slGetStatus() == OK) { s_frames = 0; s_pending = OM_RTC_SETTLE; }
        else s_frames = 59;         /* semaphore busy: retry next frame */
    }
    return 0;
}
