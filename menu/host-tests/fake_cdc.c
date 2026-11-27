/* menu/host-tests/fake_cdc.c -- a model of SEGA's LIBCD and the CD block for cdblk.c's host tests. BSD-3-Clause.
 *
 * The command gate is LIBCD's own, read from its code (SGL 3.02 LIBCD.A, `objdump -dr`, CDC_REG.O doCmdRsp at
 * 0x170): before a command it ORs HIRQ into a library shadow word; if the command's end-flag mask is not all set in
 * (shadow | HIRQ) it returns CDC_ERR_CMDBUSY (-1) without touching the CD block; without CMOK, CDC_ERR_CMDNG (-2);
 * otherwise it clears CMOK and the mask bits in HIRQ, issues the command and, on success, clears the mask bits in the
 * shadow too. The masks (each function's first argument to CDSUB_UpdCdstat/CDREG_CmdRsp): GetdelSctData and
 * DelSctData 0x80 (EHST, CDC_BIO.O), CdSetCon 0x40 (ESEL, CDC_DEV.O), CdPlay, GetSctNum and DataEnd 0 (CDC_DRV.O,
 * CDC_BIF.O, CDC_CMN.O). The shadow starts at 0x0BE1 (CDREG_InitHirqReq).
 *
 * The CD block side is the model: a host I/O (get-and-delete, delete) ends at DataEnd/the command, and EHST rises
 * `ehst_latency` ticks later (0: at once, as Mednafen does; FK_NEVER: never). A tick is one HIRQ read. */
#include <string.h>
#include "fake_cdc.h"

volatile uint16_t fk_cr[4];
int fk_cmdbusy;
static volatile uint16_t s_hirq;
static uint16_t s_shadow;
static long s_lat, s_every, s_tick, s_ehst_at, s_next_arrive;
static int32_t s_sct, s_get, s_play_left;
static volatile uint32_t s_fifo;

void fk_reset(long ehst_latency, long arrive_every)
{
    s_hirq = 0x0BE1u | CDC_HIRQ_CMOK;
    s_shadow = 0x0BE1u;
    s_lat = ehst_latency;
    s_every = arrive_every;
    s_tick = 0;
    s_ehst_at = -1;
    s_next_arrive = arrive_every;
    s_sct = s_get = s_play_left = 0;
    fk_cmdbusy = 0;
    memset((void *)fk_cr, 0, sizeof fk_cr);
}

void fk_add_sectors(int32_t n) { s_sct += n; }
int32_t fk_sectors(void) { return s_sct; }
long fk_ticks(void) { return s_tick; }

static void tick(void)
{
    s_tick++;
    if (s_ehst_at >= 0 && s_tick >= s_ehst_at) {
        s_hirq |= CDC_HIRQ_EHST;
        s_ehst_at = -1;
    }
    if (s_every > 0 && s_play_left > 0 && s_tick >= s_next_arrive) {
        s_sct++;
        s_play_left--;
        s_next_arrive = s_tick + s_every;
        if (s_play_left == 0)
            s_hirq |= CDC_HIRQ_PEND;
    }
}

volatile uint16_t *fk_hirq_reg(void)
{
    tick();
    return &s_hirq;
}

Uint16 CDC_GetHirqReq(void) { tick(); return s_hirq; }

static void host_io_end(void)
{
    if (s_lat == 0)
        s_hirq |= CDC_HIRQ_EHST;
    else if (s_lat > 0)
        s_ehst_at = s_tick + s_lat;
}

static Sint32 gate(uint16_t mask)
{
    uint16_t r = (uint16_t)(s_shadow | s_hirq);
    s_shadow = r;
    if ((r & mask) != mask) {
        fk_cmdbusy++;
        return CDC_ERR_CMDBUSY;
    }
    if (!(r & CDC_HIRQ_CMOK))
        return CDC_ERR_CMDNG;
    s_hirq &= (uint16_t)~(mask | CDC_HIRQ_CMOK);
    s_hirq |= CDC_HIRQ_CMOK;                                  /* the response: commands answer at once here */
    fk_cr[0] = 0x0100;                                        /* status PAUSE, no periodic flag */
    return CDC_ERR_OK;
}

static Sint32 ok(uint16_t mask)
{
    Sint32 e = gate(mask);
    if (e == CDC_ERR_OK)
        s_shadow &= (uint16_t)~mask;
    return e;
}

Sint32 CDC_AbortFile(void) { Sint32 e = ok(0); if (!e) s_hirq |= CDC_HIRQ_EFLS; return e; }
Sint32 CDC_ResetSelector(Sint32 rflag, Sint32 bufno) { Sint32 e; (void)rflag; (void)bufno; e = ok(0); if (!e) { s_sct = 0; s_hirq |= CDC_HIRQ_ESEL; } return e; }
Sint32 CDC_SetSctLen(Sint32 g, Sint32 p) { (void)g; (void)p; return ok(0); }
Sint32 CDC_GetPeriStat(CdcStat *stat) { memset(stat, 0, sizeof *stat); return CDC_ERR_PERI; }
Sint32 CDC_GetCurStat(CdcStat *stat)                          /* Get CD Status (command 0x00, mask 0) */
{
    Sint32 e = ok(0);
    memset(stat, 0, sizeof *stat);
    stat->status = s_play_left ? 0x03 : 0x01;                 /* PLAY while the play has sectors to go, else PAUSE */
    stat->report.fad = 150;
    return e;
}
Sint32 CDC_CdSetCon(Sint32 filtno) { Sint32 e; (void)filtno; e = ok(CDC_HIRQ_ESEL); if (!e) s_hirq |= CDC_HIRQ_ESEL; return e; }
Sint32 CDC_CdSeek(CdcPos *pos) { (void)pos; s_play_left = 0; return ok(0); }

Sint32 CDC_CdPlay(CdcPly *ply)
{
    Sint32 e = ok(0);
    if (!e) {
        s_play_left = CDC_PLY_EFAS(ply);
        s_next_arrive = s_tick + s_every;
        s_hirq &= (uint16_t)~CDC_HIRQ_PEND;
    }
    return e;
}

Sint32 CDC_GetSctNum(Sint32 bufno, Sint32 *snum)
{
    Sint32 e = ok(0);
    (void)bufno;
    *snum = e ? 0 : s_sct;
    return e;
}

Sint32 CDC_GetSctInfo(Sint32 bufno, Sint32 spos, CdcSct *sct)
{
    (void)bufno; (void)spos;
    memset(sct, 0, sizeof *sct);
    return ok(0);
}

Sint32 CDC_GetdelSctData(Sint32 bufno, Sint32 spos, Sint32 snum)
{
    Sint32 e;
    (void)bufno; (void)spos;
    if ((e = ok(CDC_HIRQ_EHST)) != CDC_ERR_OK)
        return e;
    if (snum > s_sct)
        return CDC_ERR_REJECT;
    s_get = snum;
    s_hirq |= CDC_HIRQ_DRDY;
    return CDC_ERR_OK;
}

Sint32 CDC_DelSctData(Sint32 bufno, Sint32 spos, Sint32 snum)
{
    Sint32 e;
    (void)bufno; (void)spos;
    if ((e = ok(CDC_HIRQ_EHST)) != CDC_ERR_OK)
        return e;
    s_sct -= snum < s_sct ? snum : s_sct;
    host_io_end();
    return CDC_ERR_OK;
}

Sint32 CDC_PutSctData(Sint32 filtno, Sint32 snum) { (void)filtno; (void)snum; return ok(CDC_HIRQ_EHST); }

Sint32 CDC_DataReady(Sint32 dtype)
{
    (void)dtype;
    if (!(s_hirq & CDC_HIRQ_DRDY))
        return CDC_ERR_TMOUT;
    s_hirq &= (uint16_t)~CDC_HIRQ_DRDY;                       /* CDC_CMN.O DataReady: ClrHirqReq(~DRDY) */
    return CDC_ERR_OK;
}

Uint32 *CDC_GetDataPtr(void) { return (Uint32 *)&s_fifo; }

Sint32 CDC_DataEnd(Sint32 *cnt)
{
    Sint32 e = ok(0);
    *cnt = s_get * 512;
    if (!e && s_get) {
        s_sct -= s_get;
        s_get = 0;
        host_io_end();
    }
    return e;
}
