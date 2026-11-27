/* menu/app/fwcheck.h -- the Satiator firmware read once after API mode, judged against the
 * tested minimum (menu/src/fwver.h) once SETTINGS.INI is known. M3.5 decisions 1 and 2: below the minimum with
 * fw_check on, a blocking NBG0 notice (A continues, A+B+C+Start resets); with fw_check=off, or an unreadable version,
 * a status line only, never blocking. Every outcome shows on the diagnostics screen, page 1. BSD-3-Clause. */
#ifndef OM_FWCHECK_H
#define OM_FWCHECK_H
#include <stdint.h>

void om_fw_capture(void);             /* main.c, right after s_mode succeeds: s_get_fw_version into a static */
int om_fw_status(void);               /* OM_FW_* against OM_FW_MIN_TESTED */
const char *om_fw_string(void);       /* as read ("" on a read failure) */
int om_fw_rc(void);
/* After SETTINGS.INI is loaded and before the interstitial hands over: the blocking notice (OLD and fw_check on; msg
 * then ""), else a status message into msg ("" when there is nothing to say). */
void om_fw_decide(int fw_check, char *msg, uint32_t cap);
#endif
