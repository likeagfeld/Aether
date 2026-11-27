/* menu/app/popup.h -- the menu's side of the in-menu popups (popabi.h): POPUP.BIN read from the menu's own disc
 * image into OM_LW_LBG, checked against the CRC-32 tools/sbuild.py patched into om_pop_crc, then called through its
 * header. The caller (folders.c) lends the card-scan work area and keeps OM_LW_STAGE untouched while it is open (no
 * art job, no LOADBG preload, no theme or settings write). Any failure leaves the popup closed: the caller falls back
 * to LAUNCHER.BIN's editor and save manager (the hand-off). BSD-3-Clause. */
#ifndef OM_POPUP_H
#define OM_POPUP_H
#include <stdint.h>
#include "popabi.h"

#define OM_POP_E_NONE  -170                 /* no POPUP.BIN in the menu's disc image (or no image) */
#define OM_POP_E_BUILD -171                 /* POPUP.BIN is another build's (its CRC is not this menu's) */

#define OM_POP_CRC_UNSET 0x504F5021u      /* "POP!": om_pop_crc before tools/sbuild.py patches it */
extern volatile uint32_t om_pop_crc;        /* patched by tools/sbuild.py: CRC-32 of this build's POPUP.BIN */

/* scratch: 4-aligned, >= 8 KB (the disc's directory; the caller's arena). 0 = open, < 0 = closed (why: a line) */
int om_pop_open(const om_pop_req *r, uint8_t *scratch, char *why, uint32_t why_cap);
int om_pop_active(void);
/* the console setup file (/aether/DIAG.TXT, psetup.c) through POPUP.BIN, not while a popup is open: loaded (scratch
 * the arena) unless still intact, then run at once. abort is asked between the load's sector reads: 1 stops it
 * (OM_POP_E_ABORT; the caller tries again later). OM_LW_LBG is overwritten: the caller re-requests its art. */
#define OM_POP_E_ABORT -172
int om_pop_setup(uint8_t *arena, uint32_t cap, uint32_t games, uint8_t loader, int (*abort)(void));
uint32_t om_pop_key(uint16_t press, uint16_t rep);   /* OM_POP_*; OM_POP_CLOSED: closed (the stage is free again) */
void om_pop_draw(uint16_t vis, uint32_t frame);
void om_pop_abort(void);                    /* closed without its key (the soft reset): the working folder back to "/" */
#endif
