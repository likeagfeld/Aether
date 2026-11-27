/* menu/launcher/smpcpad.h -- standard pad on port 1 via SMPC INTBACK, bare metal (no SGL in the launcher). */
#ifndef OM_SMPCPAD_H
#define OM_SMPCPAD_H
#include <stdint.h>
#define OM_PAD_B (1u << 8)    /* SGL PER_DGT_TB (SL_DEF.H:1257) */
#define OM_PAD_A (1u << 10)   /* SGL PER_DGT_TA (SL_DEF.H:1255) */
#define OM_PAD_C (1u << 9)    /* SGL PER_DGT_TC (SL_DEF.H:1256) */
#define OM_PAD_Z (1u << 4)    /* SGL PER_DGT_TZ (SL_DEF.H:1261); held at step 1 = the BRAM listing */
void om_smpc_pad_init(void);                  /* once, display on: calibrates polls per line (smpcwait.h) */
uint16_t om_smpc_pad(void);                   /* one field; pressed = 1 */
int om_smpc_pad_ok(void);                     /* 1 when that field's read got pad data (0 = no window, no pad) */
int om_smpc_pad_fallback(int *last_rc);       /* 1 while the fallback timing is engaged; *last_rc = why */
uint32_t om_smpc_pad_ppl(void);               /* calibrated polls per line, 0 = calibration failed */
/* Exit C (review-final-m3 I4): SMPC SYSRES, HWF-53. Call it straight after an om_smpc_pad() that
 * read the pad: that read ended inside the INTBACK window, so this field is past "300 us after V-BLANK-IN" and before
 * the next V-BLANK-IN, the window SMPC_Manual.txt:949-950 sets for SYSRES. Returns only on failure. */
#define OM_SMPC_E_BUSY    -63         /* SF stayed set: the SMPC never took the command */
#define OM_SMPC_E_NORESET -64         /* SYSRES issued, the console did not reset within the wait */
int om_smpc_sysres(void);
/* a Type B command (SMPC_Manual.txt:798, Figure 2.2: wait for SF clear, set SF, write COMREG), then a wait
 * for SF clear so the next command is never issued over it (the sequential-issue rule, :1004-1007). Issued only
 * between om_smpc_pad() calls, never inside one. 0, or OM_SMPC_E_BUSY when SF stayed set. */
#define OM_SMPC_RESENAB 0x19u         /* reset button -> NMI enabled (SMPC_Manual.txt:1331-1352) */
#define OM_SMPC_RESDISA 0x1Au         /* disabled; the power-on default (:1354-1375) */
int om_smpc_cmd_b(uint8_t cmd);
/* the reset button's NMI mode, read by an INTBACK that asks for the SMPC status
 * only (IREG0 01H "returns ... reset button mode", IREG1 PEN = 0: no peripheral data; SMPC_Manual.txt:1441-1455,
 * :1497-1520), issued in the same window as the pad read (om_pad_read, HWF-14). OREG0 bit 6 RESD: 0 = reset enabled,
 * 1 = disabled, the power-on default (SMPC_Manual.txt:1536-1550; ST-162-R1 PER_SS_RESET). Returns 0, 1, or
 * OM_SMPC_E_BUSY when 3 fields gave no reading. Up to 3 fields (~50 ms). */
int om_smpc_resd(void);
/* the console clock as "YYYYMMDD-HHMMSS" (SMPC status INTBACK, OREG0 STE + OREG1-7, SMPC_Manual.txt:
 * 1536-1580). 0; OM_SMPC_RTC_UNSET when STE = 0 (never set since a cold reset) or a field is not valid BCD; or
 * OM_SMPC_E_BUSY. out is "" on any failure. Up to 3 fields. */
#define OM_SMPC_RTC_UNSET -65
int om_smpc_rtc(char out[16]);
/* the pure part (host-tested, smpcwait.c): OREG0..OREG7 as read -> out, 0 or OM_SMPC_RTC_UNSET */
int om_rtc_fmt(const uint8_t oreg[8], char out[16]);

#endif
