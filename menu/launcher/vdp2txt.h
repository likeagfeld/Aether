/* menu/launcher/vdp2txt.h -- bare-metal VDP2 NBG0 text screen for the launcher (no SGL: HWRAM is forfeit). */
#ifndef OM_VDP2TXT_H
#define OM_VDP2TXT_H
#include "vbwait.h"                  /* om_txt_wait_vblank (split out so the menu can link cdblk.c) */
#define OM_TXT_BACK_OFF 0x7FFFEu
void om_txt_init(void);
extern int om_txt_ready;                 /* text is drawn only after om_txt_init */
void om_txt_clear(void);
void om_txt_display(int on);
void om_txt_puts(int x, int y, const char *s, int pal);
void om_txt_putn(int x, int y, int v, int pal);
#endif
