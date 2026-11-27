/* menu/pop/pdraw.h -- the popups' view (mockup 2026-11-25, popups.html; About 2026-11-27, after the Card Manager's
 * About window): one large panel over the dimmed menu, in front of everything the menu draws -- a header (the game's
 * cover, a spaced label, the title, a faint tilted Aether ring with its comet, a meter; About: the logo on its navy
 * tile and the lines beside it), Manage saves' tabs, a list and a side panel, a footer of button hints that a message
 * replaces. The boxes and fixed strings are pgeom.h's (checked by test_popgeom.c; About's pabout.h, test_pabout.c).
 * Text textures live in VDP1 VRAM's saver range (omlayout.h OM_VRAM_SAVER), idle while a popup is open (the menu holds
 * its saver off). BSD-3-Clause. */
#ifndef OM_PDRAW_H
#define OM_PDRAW_H
#include <stdint.h>
#include "pabout.h"
#include "pcheat.h"
#include "pgeom.h"
#include "popabi.h"
#include "psave.h"

#define OM_POP_FOLDERS  200u               /* Settings > Manage saves: the save folders listed */
#define OM_POP_BIGMIN   0x10000u           /* the smallest record buffer the saves popup opens with (64 KB) */

typedef struct {
    om_pop_req req;
    uint8_t kind, chdir, saving, save_failed;
    int open_rc;
    uint32_t frames;
    om_pc pc;                               /* Cheats */
    om_sv sv;                               /* Manage saves */
    om_ps ps;
    om_pa pa;                               /* About */
    char msg[80];                           /* the last save's result (Cheats) */
} om_pop_state;

int om_pd_open(om_pop_state *g);            /* the text slots (About: and its lines, its logo); 0 or OM_E_FULL */
void om_pd_draw(om_pop_state *g, uint16_t vis, uint32_t frame);
#endif
