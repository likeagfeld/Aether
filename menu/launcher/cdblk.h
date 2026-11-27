/* menu/launcher/cdblk.h -- CD-block operations the region-free boot needs (BSD-3-Clause, new code). */
#ifndef OM_CDBLK_H
#define OM_CDBLK_H
#include <stdint.h>
#define OM_CD_E_CMOK     -40
#define OM_CD_E_WAIT     -41
#define OM_CD_E_AUTH     -42
#define OM_CD_E_SECTORS  -43
#define OM_CD_E_BOOTWAIT -44   /* BIOS loadcd_boot kept answering 1 (busy) past its cap (jhboot.c) */
#define OM_CD_E_EHST     -45
#define OM_CD_E_CDC      -46
int om_cd_reset(int rc_out[4]);         /* AbortFile, DataEnd, ResetSelector, SetSctLen 2048 (rc of each) */
int om_cd_init_for_boot(int rc_out[4]); /* om_cd_reset + the BIOS work words jhboot needs (HWF-19): launcher only */
int om_cd_auth(uint16_t *status);
int om_cd_read(uint8_t *dst, int32_t fad, int32_t n);   /* blocking: start, wait for n sectors, take */
/* Streaming (the boot animation): start a play of n sectors from fad into buffer partition 0 and return;
 * om_cd_stream_count = sectors waiting there (or OM_CD_E_CDC); om_cd_take moves n of them (n <= count) to dst, which
 * must be 4-byte aligned; om_cd_stream_stop pauses the drive and drops what is left. */
int om_cd_ready(void);                  /* HIRQ CMOK = 1 now: ready for a command (one read, no wait) */
uint16_t om_cd_hirq(void);              /* HIRQ as it is now (diagnostics) */
uint16_t om_cd_peri(void);              /* CD status code from the periodic report, 0xFFFF if none (diagnostics) */
/* Cap of the polls this file makes before each LIBCD command (default 20,000,000 spins, the launcher's). The menu's boot
 * animation sets a short cap so a wedged CD block costs a fraction of a second per command; its own budgets are in
 * V-blanks (menu/app/bootplay.c). 0 restores the default. */
void om_cd_spin_cap(uint32_t spins);
void om_cd_clear_error(void);int om_cd_stream_start(int32_t fad, int32_t n);
int32_t om_cd_stream_count(void);
int om_cd_take(uint8_t *dst, int32_t n);
int om_cd_stream_stop(void);             /* stop the play + om_cd_reset, the reset even if the stop failed */
int om_cd_pause(void);                   /* pause the play (CdSeek "no change", HWF-56): it stops feeding the selector */
int32_t om_cd_sector_fad(int32_t spos);  /* FAD of waiting sector spos of partition 0 (CDC_GetSctInfo), or OM_CD_E_CDC */
int om_cd_drop(int32_t n);               /* delete the first n waiting sectors of partition 0 (CDC_DelSctData) */
/* Which LIBCD call made the last OM_CD_E_CDC, as step * 100 - CdcErr (CDC_ERR_*, SEGA_CDC.H:113-122): step 1 CdSetCon,
 * 2 CdPlay, 3 GetSctNum, 4 GetdelSctData, 5 DataReady, 6 DataEnd, 7 CdSeek, 8-11 om_cd_reset's four calls in the stop,
 * 12 GetSctInfo, 13 DelSctData.
 * 0 = none since the last stream start. */
int32_t om_cd_last_error(void);
/* The last OM_CD_E_CDC's registers (HIRQ, CR1-CR4): pre = as they were just before the failing LIBCD call, post = at its
 * failure; ehst_max = the longest wait for EHST after a take (polls; sizes the console's host-I/O latency). */
/* For a read that timed out (OM_CD_E_SECTORS, hardware NiGHTS 2026-11-24): cnt_max = the most sectors the partition
 * ever held during the wait, cur_status / cur_fad = the drive status byte and frame address of a Get CD Status
 * (CDC_GetCurStat) at the time-out, cur_rc its result. */
typedef struct {
    uint16_t pre[5], post[5]; uint32_t ehst_max;
    int32_t cnt_max, cur_rc, cur_fad; uint8_t cur_status;
} om_cd_diag;
const om_cd_diag *om_cd_last_diag(void);
int om_cd_put(const uint8_t *src, int32_t n);
int om_cd_present(void);
#endif
