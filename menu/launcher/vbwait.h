/* menu/launcher/vbwait.h -- a bounded TVSTAT V-blank wait with no SGL and no VDP2 setup. Split out of vdp2txt.c in Task
 * 13b so cdblk.c links into the menu without the launcher's bare-metal text screen. BSD-3-Clause. */
#ifndef OM_VBWAIT_H
#define OM_VBWAIT_H
void om_txt_wait_vblank(void);
#endif
