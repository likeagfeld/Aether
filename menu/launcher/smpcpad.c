/* menu/launcher/smpcpad.c -- see smpcpad.h. Register map, INTBACK parameters, the port-status / peripheral-ID bytes,
 * the button byte order and the "continue/break" rule are all HWF-14 (per ST-169-R1 / SMPC_Manual.txt; a CORRECTED
 * row overrides this file). Buttons arrive active-low in OREG2:OREG3, in the same bit order as SGL's PER_DGT_*
 * (KR=15 ... TL=3). INTBACK is issued only inside its window -- after 300 us following V-BLANK-IN and before
 * V-BLANK-OUT (SMPC_Manual.txt:885-905, Figure 2.5; HWF-14) -- via om_pad_read (smpcwait.c), which also paces the
 * caller at one read per field and engages a calibrated fixed-delay fallback after 8 failing fields in a row.
 * Every wait is bounded. BSD-3-Clause. */
#include "smpcpad.h"
#include "smpcwait.h"

#define SMPC(o) (*(volatile uint8_t *)(0x20100000u + (o)))
#define IREG(n) SMPC(0x01 + 2 * (n))
#define OREG(n) SMPC(0x21 + 2 * (n))
#define COMREG  SMPC(0x1F)
#define SR      SMPC(0x61)
#define SF      SMPC(0x63)
#define SF_SPINS 1000000u
#define SYSRES_SPINS 8000000u           /* >= 8 M SF reads, well past SYSRES's "100 msec +" (SMPC_Manual.txt:1224-1243) */

static uint8_t s_read_ok;           /* the last om_smpc_pad() read the pad inside the INTBACK window */

static uint16_t tvstat(void *ctx)
{
    (void)ctx;
    return *(volatile uint16_t *)0x25F80004u;                       /* TVSTAT (HWF-10) */
}

static int hw_intback(void *ctx, uint16_t *pressed)
{
    uint32_t t;
    uint16_t raw;
    (void)ctx;
    if (SF & 1) return -1;          /* SMPC still busy: never wait here, it would eat the window */
    SF = 1;
    IREG(0) = 0x00;                 /* no SMPC status */
    IREG(1) = 0x08;                 /* peripheral data enabled, port 1 and 2 in 15-byte mode */
    IREG(2) = 0xF0;
    COMREG = 0x10;                  /* INTBACK */
    for (t = 0; (SF & 1) && t < SF_SPINS; t++) ;
    if (SF & 1) return -1;
    if (SR & 0x20) {                /* more data pending: break (IREG0 bit 6) */
        IREG(0) = 0x40;
        for (t = 0; (SF & 1) && t < SF_SPINS; t++) ;
    }
    if (OREG(0) != 0xF1 || OREG(1) != 0x02)
        return -1;                  /* not one directly connected standard digital pad */
    raw = (uint16_t)((OREG(2) << 8) | OREG(3));
    *pressed = (uint16_t)~raw;
    s_read_ok = 1;
    return 0;
}

static om_padtime s_pt;
static const om_padio s_io = { tvstat, hw_intback, 0 };

void om_smpc_pad_init(void) { om_padtime_init(&s_pt, om_poll_calibrate(tvstat, 0)); }

uint16_t om_smpc_pad(void)
{
    s_read_ok = 0;
    return om_pad_read(&s_pt, &s_io);
}

int om_smpc_pad_ok(void) { return s_read_ok; }

int om_smpc_pad_fallback(int *last_rc)
{
    *last_rc = s_pt.last_rc;
    return s_pt.fallback;
}

uint32_t om_smpc_pad_ppl(void) { return s_pt.ppl; }

int om_smpc_sysres(void)
{   /* SMPC_Manual.txt:1224-1243: SYSRES, command 0DH, "Resets the entire SATURN system ... starts up the boot ROM";
     * a Type A command (:796): wait for SF clear, set SF, write COMREG, then wait for the reset (Figure 2.1) */
    uint32_t t;
    for (t = 0; (SF & 1) && t < SF_SPINS; t++) ;
    if (SF & 1) return OM_SMPC_E_BUSY;
    SF = 1;
    COMREG = 0x0D;                  /* SYSRES */
    for (t = 0; t < SYSRES_SPINS; t++)
        (void)SF;
    return OM_SMPC_E_NORESET;
}

static int hw_status(void *ctx, uint16_t *pressed)
{
    uint32_t t;
    if (SF & 1) return -1;
    SF = 1;
    IREG(0) = 0x01;                 /* SMPC status: time, cartridge, area, system, SMEM, reset button mode */
    IREG(1) = 0x00;                 /* PEN = 0: no peripheral data */
    IREG(2) = 0xF0;                 /* "be sure to set F0H in IREG2" (SMPC_Manual.txt:1520) */
    COMREG = 0x10;                  /* INTBACK */
    for (t = 0; (SF & 1) && t < SF_SPINS; t++) ;
    if (SF & 1) return -1;
    *(int *)ctx = (OREG(0) >> 6) & 1;   /* RESD */
    *pressed = 0;
    return 0;
}

int om_smpc_resd(void)
{
    om_padtime pt = s_pt;           /* a copy: the status read must not move the pad's fallback state */
    int resd = -1, t;
    om_padio io = { tvstat, hw_status, 0 };
    io.ctx = &resd;
    for (t = 0; t < 3 && resd < 0; t++)
        (void)om_pad_read(&pt, &io);
    return resd < 0 ? OM_SMPC_E_BUSY : resd;
}

/* the console clock for the BRAM backup's file name, from the same status-only INTBACK (IREG0 01H, PEN 0)
 * in the same window: OREG0 bit 7 STE ("SETTIME is done after SMPC cold reset"), OREG1-OREG2 the year in BCD, OREG3
 * low nibble the month (hex 1-C), OREG4-OREG7 day, hours, minutes, seconds in BCD (SMPC_Manual.txt:1536-1580). */
typedef struct { int got; uint8_t o[8]; } rtc_ctx;
static int hw_rtc(void *ctx, uint16_t *pressed)
{
    rtc_ctx *c = (rtc_ctx *)ctx;
    uint32_t t, i;
    if (SF & 1) return -1;
    SF = 1;
    IREG(0) = 0x01;
    IREG(1) = 0x00;
    IREG(2) = 0xF0;
    COMREG = 0x10;                  /* INTBACK */
    for (t = 0; (SF & 1) && t < SF_SPINS; t++) ;
    if (SF & 1) return -1;
    for (i = 0; i < 8; i++) c->o[i] = OREG(i);
    c->got = 1;
    *pressed = 0;
    return 0;
}

int om_smpc_rtc(char out[16])
{
    om_padtime pt = s_pt;
    rtc_ctx c;
    om_padio io = { tvstat, hw_rtc, 0 };
    int t;
    out[0] = 0;
    c.got = 0;
    io.ctx = &c;
    for (t = 0; t < 3 && !c.got; t++)
        (void)om_pad_read(&pt, &io);
    if (!c.got) return OM_SMPC_E_BUSY;
    return om_rtc_fmt(c.o, out);
}

int om_smpc_cmd_b(uint8_t cmd)
{
    uint32_t t;
    for (t = 0; (SF & 1) && t < SF_SPINS; t++) ;
    if (SF & 1) return OM_SMPC_E_BUSY;
    SF = 1;
    COMREG = cmd;
    for (t = 0; (SF & 1) && t < SF_SPINS; t++) ;
    return (SF & 1) ? OM_SMPC_E_BUSY : 0;
}
