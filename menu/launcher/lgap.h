/* menu/launcher/lgap.h -- the launcher's SD write gap: the pause sdwrite.c takes between 2 KB chunks
 * (sdwrite.h; Rings' V-blank wait, Kai's freeze on back-to-back SD writes). It waits for the next rising TVSTAT VBLANK
 * edge (HWF-10), bounded by menu/src/frcgap.h on the SH7604 FRC -- never om_txt_wait_vblank, whose 2,000,000-poll cap
 * is seconds long when the display is off (HWF-76 (c)). BSD-3-Clause. */
#ifndef OM_LGAP_H
#define OM_LGAP_H
void om_lgap(void);
#include <stdint.h>
uint32_t om_lnonce(void);         /* the marker's session nonce */
/* the save swap's clock (pgsrun.h env.now_us): microseconds, correct while calls come less than one FRC wrap apart;
 * om_lclock_slow(1) sets the FRC to clock/128 (a wrap every ~312 ms) for the swap, (0) puts the old rate back */
void om_lclock_slow(int on);
uint32_t om_lclock_us(void);
#endif
