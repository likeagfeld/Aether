/* menu/launcher/vbwait.c -- see vbwait.h. Moved unchanged from vdp2txt.c. BSD-3-Clause. */
#include <stdint.h>
#include "vbwait.h"

#define REG(o) (*(volatile uint16_t *)(0x25F80000u + (o)))       /* HWF-09 */

void om_txt_wait_vblank(void)
{
    uint32_t t;
    for (t = 0; (REG(0x004) & 0x0008) && t < 2000000u; t++) ;     /* TVSTAT bit3 VBLANK (HWF-10) */
    for (t = 0; !(REG(0x004) & 0x0008) && t < 2000000u; t++) ;
}
