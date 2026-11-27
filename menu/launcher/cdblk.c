/* menu/launcher/cdblk.c -- see cdblk.h. Everything but authentication uses SEGA's CDC library (LIBCD.A), with
 * semantics per ST-38-R1 (External Specification: Saturn CD Communication Interface) and signatures per the SGL 3.02
 * header build/sgl/inc_clean/SEGA_CDC.H (they differ from the 1994 document in places -- e.g. CDC_DataReady takes a
 * direction, CDC_ResetSelector a buffer number; the header is what links). The library's own command/response and
 * data-ready waits time out by fixed-cycle polling and return CDC_ERR_TMOUT (per ST-38-R1 §3.1 "Communications
 * Error", §3.3 (2)). Device authentication has no DTS source (ST-38-R1 has no such function: grep count 0,
 * measured). It is a raw command (0xE0, completion EFLS; status 0xE1, result CR2) in the register style of
 * libsatiator's exec_cmd (public domain, satiator.c:74-93). HWF-18 records it as UNVERIFIED-ACCEPTED. Every wait
 * here is bounded and returns a distinct code. HWF-17. */
#ifdef CDBLK_HOST_TEST
#include "fake_cdc.h"                                    /* host tests: LIBCD and the registers modeled (fake_cdc.c) */
#else
#include "sega_cdc.h"
#define CD_HIRQ (*(volatile uint16_t *)0x25890008u)     /* HWF-18 */
#define CD_CR1  (*(volatile uint16_t *)0x25890018u)
#define CD_CR2  (*(volatile uint16_t *)0x2589001Cu)
#define CD_CR3  (*(volatile uint16_t *)0x25890020u)
#define CD_CR4  (*(volatile uint16_t *)0x25890024u)
#endif
#include "cdblk.h"
#include "vbwait.h"
#define CD_RST_ALL 0xFC                                  /* reset mode, every "initialise" bit (HWF-17) */
#define SPINS 20000000u                                  /* register-poll cap (iterations) */
#define FRAMES 600                                       /* om_txt_wait_vblank cap: 10 s at 60 Hz (HWF-11) */

static int32_t s_last;                                   /* step * 100 - CDC error of the last failure (0 = none) */
static uint32_t s_spins = SPINS;                         /* cap of the cmok()/ended() polls (om_cd_spin_cap) */
void om_cd_spin_cap(uint32_t spins) { s_spins = spins ? spins : SPINS; }
void om_cd_clear_error(void) { s_last = 0; }
int32_t om_cd_last_error(void) { return s_last; }
#ifdef OM_CD_DIAG                                        /* the launcher's error screen only (sbuild LCFLAGS) */
static uint16_t s_pre[5];                                /* HIRQ, CR1-CR4 as the last cmok() left them */
static om_cd_diag s_diag;
const om_cd_diag *om_cd_last_diag(void) { return &s_diag; }
static void regs(uint16_t r[5]) { r[0] = CD_HIRQ; r[1] = CD_CR1; r[2] = CD_CR2; r[3] = CD_CR3; r[4] = CD_CR4; }
#define PRE() regs(s_pre)
#define EHST_MAX(t) do { if ((t) > s_diag.ehst_max) s_diag.ehst_max = (t); } while (0)
#else
#define PRE() ((void)0)
#define EHST_MAX(t) ((void)(t))
#endif
static int cdc_fail(int step, Sint32 err)
{
    s_last = (int32_t)step * 100 - (int32_t)err;
#ifdef OM_CD_DIAG
    {
        uint32_t i;
        for (i = 0; i < 5; i++) s_diag.pre[i] = s_pre[i];
        regs(s_diag.post);
    }
#endif
    return OM_CD_E_CDC;
}

/* Wait for HIRQ CMOK before a command (bounded): LIBCD refuses one with CDC_ERR_CMDBUSY while the last has not ended
 * (SEGA_CDC.H:115; HWF-45). */
static void cmok(void)
{
    uint32_t t;
    for (t = 0; !(CD_HIRQ & CDC_HIRQ_CMOK) && t < s_spins; t++) ;
    PRE();                                               /* the diagnostics' "before": every LIBCD call follows this */
}

/* Selector commands (CdSetCon, ResetSelector) end with HIRQ ESEL, file-system ones (AbortFile) with EFLS (SEGA_CDC.H:
 * 35-38); LIBCD answers CDC_ERR_CMDBUSY to a selector command while the previous one has not ended (HWF-45, measured
 * on Mednafen in Task 13b: CdSetCon right after ResetSelector). Bounded. */
static void ended(uint16_t flag)
{
    uint32_t t;
    for (t = 0; !(CD_HIRQ & flag) && t < s_spins; t++) ;
}

/* The end of a host I/O this file started (get-and-delete, delete): HIRQ EHST. LIBCD refuses the next get-and-delete
 * or delete with CDC_ERR_CMDBUSY until EHST has been seen; on the console EHST comes some time after CDC_DataEnd,
 * on Mednafen at once (HWF-79). Bounded; s_diag keeps the longest wait. */
static void host_io_end(void)
{
    uint32_t t;
    for (t = 0; !(CD_HIRQ & CDC_HIRQ_EHST) && t < s_spins; t++) ;
    EHST_MAX(t);
}

static int raw_cmd(uint16_t c1, uint16_t c2, uint16_t c3, uint16_t c4, uint16_t wait)
{
    uint32_t t;
    CD_HIRQ = (uint16_t)~(CDC_HIRQ_CMOK | wait);
    CD_CR1 = c1; CD_CR2 = c2; CD_CR3 = c3; CD_CR4 = c4;
    for (t = 0; !(CD_HIRQ & CDC_HIRQ_CMOK); t++) if (t > SPINS) return OM_CD_E_CMOK;
    if (wait) for (t = 0; !(CD_HIRQ & wait); t++) if (t > SPINS) return OM_CD_E_WAIT;
    return 0;
}

int om_cd_reset(int rc_out[4])
{
    Sint32 cnt;
    cmok(); rc_out[0] = CDC_AbortFile();
    ended(CDC_HIRQ_EFLS);
    cmok(); rc_out[1] = CDC_DataEnd(&cnt);
    cmok(); rc_out[2] = CDC_ResetSelector(CD_RST_ALL, 0);
    ended(CDC_HIRQ_ESEL);
    cmok(); rc_out[3] = CDC_SetSctLen(CDC_SLEN_2048, CDC_SLEN_2048);
    return 0;                                            /* upstream ignores these results; we only report them */
}

int om_cd_init_for_boot(int rc_out[4])
{
    *(volatile uint32_t *)0x06000278u = 0;               /* as jhloader.c:12-13 (BIOS work words; HWF-19) */
    *(volatile uint32_t *)0x0600027Cu = 0;
    return om_cd_reset(rc_out);
}

int om_cd_auth(uint16_t *status)
{
    uint32_t i;
    int rc;
    /* HWF-55: clear DCHG (disc changed) as iapetus cd_auth and the BIOS's own init do. The disc swap leaves it set,
     * and the BIOS boot's status check then fails the first loadcd_boot call with -1 before reading the header. */
    CD_HIRQ = (uint16_t)~(CDC_HIRQ_DCHG | CDC_HIRQ_EFLS);
    rc = raw_cmd(0xE000, 0x0000, 0x0000, 0x0000, CDC_HIRQ_EFLS);
    *status = 0;
    if (rc) return rc;
    for (i = 0; i < FRAMES; i++) {
        if ((rc = raw_cmd(0xE100, 0x0000, 0x0000, 0x0000, 0))) return rc;
        *status = CD_CR2;
        if (*status) return 0;
        om_txt_wait_vblank();
    }
    return OM_CD_E_AUTH;
}

int om_cd_ready(void) { return (CD_HIRQ & CDC_HIRQ_CMOK) != 0; }
uint16_t om_cd_hirq(void) { return CD_HIRQ; }
uint16_t om_cd_peri(void) { CdcStat st; return CDC_GetPeriStat(&st) == CDC_ERR_OK ? (uint16_t)st.status : 0xFFFFu; }

int om_cd_stream_start(int32_t fad, int32_t n)
{
    CdcPly ply;
    Sint32 e;
    s_last = 0;                                                      /* a fresh stream: no stale error */
    cmok();
    ended(CDC_HIRQ_ESEL);
    if ((e = CDC_CdSetCon(0)) != CDC_ERR_OK) return cdc_fail(1, e); /* CD device -> filter 0 -> partition 0 */
    CDC_PLY_STYPE(&ply) = CDC_PTYPE_FAD;
    CDC_PLY_SFAD(&ply) = fad;
    CDC_PLY_ETYPE(&ply) = CDC_PTYPE_FAD;
    CDC_PLY_EFAS(&ply) = n;                                          /* end position = sector count (HWF-17) */
    CDC_PLY_PMODE(&ply) = CDC_PM_DFL;
    cmok();
    return (e = CDC_CdPlay(&ply)) == CDC_ERR_OK ? 0 : cdc_fail(2, e);
}

int32_t om_cd_stream_count(void)
{
    Sint32 got = 0, e;
    cmok();
    return (e = CDC_GetSctNum(0, &got)) == CDC_ERR_OK ? (int32_t)got : cdc_fail(3, e);
}

int om_cd_take(uint8_t *dst, int32_t n)
{
    Sint32 cnt, e;
    uint32_t i, words = (uint32_t)n * 512u;                          /* 2048-byte sectors, 32-bit DATATRNS reads */
    volatile uint32_t *dt;
    cmok();
    if ((e = CDC_GetdelSctData(0, 0, n)) != CDC_ERR_OK) return cdc_fail(4, e);
    if ((e = CDC_DataReady(CDC_DRDY_GET)) != CDC_ERR_OK) {           /* ST-38-R1 §3.3 (2); the range is deleted */
        e = cdc_fail(5, e);                                          /* even so (p.49), so EHST follows */
        CDC_DataEnd(&cnt);
        host_io_end();
        return e;
    }
    dt = (volatile uint32_t *)CDC_GetDataPtr();
    for (i = 0; i < words; i++)
        ((uint32_t *)dst)[i] = *dt;
    if ((e = CDC_DataEnd(&cnt)) != CDC_ERR_OK) return cdc_fail(6, e);
    host_io_end();
    return 0;
}

int32_t om_cd_sector_fad(int32_t spos)
{
    CdcSct sct;
    Sint32 e;
    cmok();
    return (e = CDC_GetSctInfo(0, spos, &sct)) == CDC_ERR_OK ? (int32_t)CDC_SCT_FAD(&sct) : cdc_fail(12, e);
}

/* Delete Sector Data (HWF-56: the sectors after them move up). Its end raises HIRQ EHST, as a take's does: LIBCD clears
 * EHST when it issues the delete and refuses it while EHST is clear (HWF-79), so it is not cleared here; waited for
 * after, bounded, so the next count does not see the sectors still there. */
int om_cd_drop(int32_t n)
{
    Sint32 e;
    cmok();
    if ((e = CDC_DelSctData(0, 0, n)) != CDC_ERR_OK) return cdc_fail(13, e);
    host_io_end();
    return 0;
}

/* HWF-56: a seek with "no change" pauses the play (the drive keeps its position and spin); the default position stops
 * it. Selector commands do not touch the drive, so a restart pauses first. */
int om_cd_pause(void)
{
    CdcPos pos;
    Sint32 e;
    CDC_POS_PTYPE(&pos) = CDC_PTYPE_NOCHG;
    cmok();
    return (e = CDC_CdSeek(&pos)) == CDC_ERR_OK ? 0 : cdc_fail(7, e);
}

int om_cd_stream_stop(void)
{
    CdcPos pos;
    Sint32 e;
    int rc[4];
    int first = 0, i;
    CDC_POS_PTYPE(&pos) = CDC_PTYPE_DFL;                             /* the default position: stop the play (HWF-56) */
    cmok();
    if ((e = CDC_CdSeek(&pos)) != CDC_ERR_OK) first = cdc_fail(7, e);
    om_cd_reset(rc);                                                 /* always: drop the buffer, disconnect the drive */
    for (i = 0; i < 4 && !first; i++)
        if (rc[i] != CDC_ERR_OK) first = cdc_fail(8 + i, rc[i]);
    return first;
}

int om_cd_read(uint8_t *dst, int32_t fad, int32_t n)
{
    int t, rc;
    int32_t c;
    if ((rc = om_cd_stream_start(fad, n)) != 0) return rc;
#ifdef OM_CD_DIAG
    s_diag.cnt_max = 0;
#endif
    for (t = 0; ; t++) {
        if ((c = om_cd_stream_count()) >= n) break;
#ifdef OM_CD_DIAG
        if (c > s_diag.cnt_max) s_diag.cnt_max = c;
#endif
        if (t > FRAMES) {
#ifdef OM_CD_DIAG
            CdcStat st;
            cmok();
            s_diag.cur_rc = CDC_GetCurStat(&st);                     /* a real command: answers where GetPeriStat */
            s_diag.cur_status = st.status;                           /* reports a non-periodic CR1 as an error */
            s_diag.cur_fad = st.report.fad;
#endif
            return OM_CD_E_SECTORS;
        }
        om_txt_wait_vblank();
    }
    return om_cd_take(dst, n);
}

int om_cd_put(const uint8_t *src, int32_t n)
{
    Sint32 cnt;
    uint32_t i, t, words = (uint32_t)n * 512u;
    volatile uint32_t *dt;
    if (CDC_PutSctData(0, n) != CDC_ERR_OK) return OM_CD_E_CDC;
    if (CDC_DataReady(CDC_DRDY_PUT) != CDC_ERR_OK) { CDC_DataEnd(&cnt); return OM_CD_E_CDC; }   /* ST-38-R1 §3.3 (2) */
    dt = (volatile uint32_t *)CDC_GetDataPtr();
    for (i = 0; i < words; i++)
        *dt = ((const uint32_t *)src)[i];
    if (CDC_DataEnd(&cnt) != CDC_ERR_OK) return OM_CD_E_CDC;
    for (t = 0; !(CDC_GetHirqReq() & CDC_HIRQ_EHST); t++)            /* jhloader.c:97: wait for EHST */
        if (t > SPINS) return OM_CD_E_EHST;
    return 0;
}

int om_cd_present(void)
{
    CdcStat st;
    int code;
    if (CDC_GetPeriStat(&st) != CDC_ERR_OK) return -1;
    code = st.status & CDC_STC_MSK;                                  /* GAME_CD/CD_CTRL.C:91-97 */
    return !(code == CDC_ST_OPEN || code == CDC_ST_NODISC);
}
