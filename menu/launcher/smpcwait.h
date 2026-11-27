/* menu/launcher/smpcwait.h -- INTBACK issue window. Per SMPC_Manual.txt:885-905 and
 * Figure 2.5 (HWF-14), the SMPC runs internal tasks from V-BLANK-IN, so INTBACK must be issued after 300 us
 * following V-BLANK-IN and before V-BLANK-OUT. The launcher has no timer it owns, so it measures 300 us in video
 * lines: after the V-BLANK-IN edge it counts OM_INTBACK_HBLANKS HBLANK-IN edges (TVSTAT bit2, HWF-10). The first
 * edge may follow V-BLANK-IN immediately, so the guaranteed delay is (OM_INTBACK_HBLANKS - 1) whole lines.
 *
 * Line period, derived (HWF-14 timing clause): in 320-dot mode one line is 426 (0x1AA) SCU Timer-1 counts
 * (TechBulletins.txt:1130-1131, "If one line is 320 dots: 1 to 1AAH"); Timer 1 counts at "7 MHz or about 1/4 the
 * system clock" (SCU_Manual.txt:1590); the 320-mode system clock is 26.8741 MHz NTSC / 26.6875 MHz PAL
 * (SMPC_Manual.txt:416-418). 426 x 4 / 26.8741 MHz = 63.41 us (NTSC), 63.85 us (PAL). OM_LINE_NS_MIN rounds down
 * to 63 us: 6 whole lines = 378 us >= 300 us, and 7 lines are far inside V-BLANK (263 - 224 = 39 lines in NTSC
 * 224-line mode, TechBulletins.txt:1095). BSD-3-Clause. */
#ifndef OM_SMPCWAIT_H
#define OM_SMPCWAIT_H
#include <stdint.h>
#define OM_INTBACK_HBLANKS 7          /* HBLANK-IN edges counted after V-BLANK-IN */
#define OM_LINE_NS_MIN     63000u     /* shortest line period assumed, ns (derivation above) */
#define OM_SMPCWAIT_POLLS  2000000u   /* poll cap for each phase */
#define OM_SMPC_E_VBLANK   -60        /* no V-BLANK-IN edge within the cap */
#define OM_SMPC_E_DELAY    -61        /* HBLANK edges did not arrive within the cap */
#define OM_SMPC_E_WINDOW   -62        /* V-BLANK-OUT came before the delay ended: skip this field */
#define OM_TV_VBLANK       0x0008u    /* TVSTAT bit3 (HWF-10) */
#define OM_TV_HBLANK       0x0004u    /* TVSTAT bit2 (ST-058-R2 VDP2_Manual.txt:1505-1510) */

/* Returns 0 when INTBACK may be issued now, else one of the codes above. tvstat() returns the TVSTAT word. */
int om_intback_wait(uint16_t (*tvstat)(void *), void *ctx);

/* fallback timing for the case where TVSTAT's HBLANK bit does not toggle during V-BLANK (not stated
 * either way in VDP2_Manual.txt:1469-1510). With fb_polls != 0 the wait after V-BLANK-IN also ends once fb_polls
 * TVSTAT polls have passed; it still refuses (OM_SMPC_E_WINDOW) if V-BLANK-OUT comes first. *via says which ended
 * it. The primary condition is tested first on every poll, so the primary timing keeps being retried every field. */
#define OM_VIA_PRIMARY  1
#define OM_VIA_FALLBACK 2
int om_intback_wait2(uint16_t (*tvstat)(void *), void *ctx, uint32_t fb_polls, int *via);

/* Polls per video line, measured during active display (V-BLANK clear) over OM_CAL_LINES HBLANK-IN edges, from the
 * start of active display. 0 if the calibration failed (no HBLANK edges, or V-BLANK came first). The calibration
 * loop and the wait loop both make one tvstat() call per poll, so the counts are comparable. */
#define OM_CAL_LINES 16
uint32_t om_poll_calibrate(uint16_t (*tvstat)(void *), void *ctx);

#define OM_PAD_FALLBACK_AFTER 8       /* consecutive failing fields before the fallback engages */
#define OM_FALLBACK_LINES     8       /* fallback delay in calibrated lines: 8 x 63.41 us = 507 us >= 300 us, and
                                         > the primary's 7 edges, so a working primary always ends first */
/* Used only when the calibration failed. Lower bound only: each poll executes at least
 * OM_POLL_MIN_INSNS instructions (counted in the disassembly of om_intback_wait2's loop) and the SH-2 runs at most
 * one instruction per state (SH7604_Hardware.txt:609) at 26.8741 MHz (SMPC_Manual.txt:416-418), so 1,500 polls
 * >= 1,500 x 8 / 26.8741 MHz = 447 us >= 300 us (the measured fix-round-2 loop is 15+ instructions plus the
 * tvstat() call, so 8 is conservative). The upper bound -- whether it ends inside V-BLANK (~39 lines, ~2.47 ms) --
 * is UNVERIFIED: it depends on TVSTAT read wait states, never measured; it fits if a poll takes under ~44 states.
 * If it is too long the wait refuses (V-BLANK-OUT first), it never issues early. */
#define OM_POLL_MIN_INSNS        8u
#define OM_FALLBACK_POLLS_FIXED  1500u

typedef struct { uint32_t ppl; int fails, fallback, last_rc; } om_padtime;
typedef struct { uint16_t (*tvstat)(void *); int (*intback)(void *, uint16_t *pressed); void *ctx; } om_padio;
void om_padtime_init(om_padtime *pt, uint32_t ppl);
uint32_t om_pad_fallback_polls(const om_padtime *pt);
/* One field: wait for the INTBACK window (primary, or the fallback once engaged), then io->intback. Returns the
 * pressed-button mask, 0 if no window or no pad. pt->fallback / pt->last_rc drive the "PAD: NO DATA" line. */
uint16_t om_pad_read(om_padtime *pt, const om_padio *io);
#endif
