/* menu/host-tests/fake_cdc.h -- the slice of SEGA's LIBCD (SGL 3.02 SEGA_CDC.H) that menu/launcher/cdblk.c uses, for
 * host tests only (cdblk.c includes it when built with -DCDBLK_HOST_TEST). Names, values and structure layouts are
 * the header's (build/sgl/inc/SEGA_CDC.H, not in the repo); fake_cdc.c models the behavior. BSD-3-Clause. */
#ifndef FAKE_CDC_H
#define FAKE_CDC_H
#include <stdint.h>

typedef int32_t Sint32;
typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;

#define CDC_HIRQ_CMOK   0x0001
#define CDC_HIRQ_DRDY   0x0002
#define CDC_HIRQ_CSCT   0x0004
#define CDC_HIRQ_BFUL   0x0008
#define CDC_HIRQ_PEND   0x0010
#define CDC_HIRQ_DCHG   0x0020
#define CDC_HIRQ_ESEL   0x0040
#define CDC_HIRQ_EHST   0x0080
#define CDC_HIRQ_ECPY   0x0100
#define CDC_HIRQ_EFLS   0x0200
#define CDC_PM_DFL      0x00
#define CDC_STC_MSK     0x0f
enum { CDC_ERR_OK = 0, CDC_ERR_CMDBUSY = -1, CDC_ERR_CMDNG = -2, CDC_ERR_TMOUT = -3, CDC_ERR_PUT = -4,
       CDC_ERR_REJECT = -5, CDC_ERR_WAIT = -6, CDC_ERR_TRNS = -7, CDC_ERR_PERI = -8 };
enum { CDC_ST_OPEN = 0x06, CDC_ST_NODISC = 0x07 };
enum { CDC_PTYPE_DFL, CDC_PTYPE_FAD, CDC_PTYPE_TNO, CDC_PTYPE_NOCHG };
enum { CDC_DRDY_GET, CDC_DRDY_PUT };
enum { CDC_SLEN_2048 = 0 };

typedef struct { Uint8 status; struct { Uint8 flgrep, ctladr, tno, idx; Sint32 fad; } report; } CdcStat;
typedef struct { Sint32 ptype; union { Sint32 fad; struct { Uint8 tno, idx; } trkidx; } pbody; } CdcPos;
typedef struct { CdcPos start, end; Uint8 pmode; } CdcPly;
typedef struct { Sint32 fad; Uint8 fn, cn, sm, ci; } CdcSct;
#define CDC_POS_PTYPE(pos)  ((pos)->ptype)
#define CDC_POS_FAD(pos)    ((pos)->pbody.fad)
#define CDC_PLY_PMODE(ply)  ((ply)->pmode)
#define CDC_PLY_STYPE(ply)  CDC_POS_PTYPE(&(ply)->start)
#define CDC_PLY_SFAD(ply)   CDC_POS_FAD(&(ply)->start)
#define CDC_PLY_ETYPE(ply)  CDC_POS_PTYPE(&(ply)->end)
#define CDC_PLY_EFAS(ply)   CDC_POS_FAD(&(ply)->end)
#define CDC_SCT_FAD(sct)    ((sct)->fad)

Sint32 CDC_AbortFile(void);
Sint32 CDC_DataEnd(Sint32 *cnt);
Sint32 CDC_DataReady(Sint32 dtype);
Uint32 *CDC_GetDataPtr(void);
Sint32 CDC_ResetSelector(Sint32 rflag, Sint32 bufno);
Sint32 CDC_SetSctLen(Sint32 getslen, Sint32 putslen);
Sint32 CDC_GetPeriStat(CdcStat *stat);
Sint32 CDC_GetCurStat(CdcStat *stat);
Sint32 CDC_CdSetCon(Sint32 filtno);
Sint32 CDC_CdPlay(CdcPly *ply);
Sint32 CDC_CdSeek(CdcPos *pos);
Sint32 CDC_GetSctNum(Sint32 bufno, Sint32 *snum);
Sint32 CDC_GetdelSctData(Sint32 bufno, Sint32 spos, Sint32 snum);
Sint32 CDC_GetSctInfo(Sint32 bufno, Sint32 spos, CdcSct *sct);
Sint32 CDC_DelSctData(Sint32 bufno, Sint32 spos, Sint32 snum);
Sint32 CDC_PutSctData(Sint32 filtno, Sint32 snum);
Uint16 CDC_GetHirqReq(void);

/* The CD-block registers. Every HIRQ access goes through fk_hirq_reg(), which advances the model one tick (sector
 * arrival, a pending EHST), so a wait loop that polls HIRQ sees time pass. Writes overwrite (the real register only
 * clears the 0 bits; the paths under test do not write it). */
volatile uint16_t *fk_hirq_reg(void);
extern volatile uint16_t fk_cr[4];
#define CD_HIRQ (*fk_hirq_reg())
#define CD_CR1  (fk_cr[0])
#define CD_CR2  (fk_cr[1])
#define CD_CR3  (fk_cr[2])
#define CD_CR4  (fk_cr[3])

/* The model's knobs and counters (fake_cdc.c). */
#define FK_NEVER (-1L)
void fk_reset(long ehst_latency, long arrive_every);  /* EHST rises this many ticks after a host I/O ends (FK_NEVER:
                                                        * never); a sector arrives every arrive_every ticks (0: none) */
void fk_add_sectors(int32_t n);                       /* sectors land in partition 0 now */
int32_t fk_sectors(void);                             /* sectors waiting in partition 0 */
long fk_ticks(void);
extern int fk_cmdbusy;                                      /* LIBCD CMDBUSY refusals so far */
#endif
