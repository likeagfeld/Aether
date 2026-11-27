/* menu/src/frcgap.h -- the time bound of the SD write gap (menu/app/main.c sd_gap; M6 Task 21, fix round 1; Task 21
 * re-review N1 and N2, folded into M6 Task 22). The gap waits for one V-blank; this bounds it at OM_FRCGAP_MAX_US
 * measured on the SH7604 FRT's 16-bit FRC, which the menu only reads (SH7604_Hardware.txt ch. 11, lines 12689-12779;
 * menu/ui/uitext.c om_ut_frc / om_ut_frc_us). Each poll adds the raw tick difference since the last poll (a 16-bit
 * difference, so it cannot wrap between two polls); a difference over OM_FRCGAP_STEP_US is dropped (the FRC stepped
 * back: measured on Mednafen, docs/ledger.md M6 Task 21). The sum is converted to microseconds once, at the
 * comparison (N2: converting each short difference floored it -- a 3-tick poll at clock/8 added 0 us -- so the bound
 * stretched, and a fast enough loop never reached it). OM_FRCGAP_POLLS ends the gap whatever the FRC does (N1: a
 * frozen FRC with a dead V-blank hook was an endless loop). Portable, host-tested (test_frcgap.c). BSD-3-Clause. */
#ifndef OM_FRCGAP_H
#define OM_FRCGAP_H
#include <stdint.h>
#define OM_FRCGAP_MAX_US  40000u     /* two PAL frames, a little over two NTSC ones */
#define OM_FRCGAP_STEP_US 2000u      /* one poll takes microseconds: a bigger step is the FRC restarting */
#define OM_FRCGAP_POLLS   200000u    /* ~0.6 s at ~3 us per poll (Task 21 re-review's estimate) */
typedef struct { uint32_t ticks, us10k, polls; uint16_t last; } om_frcgap;
/* us10k: microseconds per 10,000 FRC ticks at the current divider (om_ut_frc_us(10000): 2,976 at clock/8, 47,629 at
 * clock/128, so every product below stays inside 32 bits) */
void om_frcgap_start(om_frcgap *g, uint16_t frc, uint32_t us10k);
int om_frcgap_step(om_frcgap *g, uint16_t frc);   /* 1 = the gap ran out (time or poll cap) */
#endif
