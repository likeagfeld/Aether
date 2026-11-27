/* menu/ui/pad.h -- pad 1 through SGL's per-frame peripheral data (spec §7.5). BSD-3-Clause. */
#ifndef OM_PAD_H
#define OM_PAD_H
#include "sgl.h"
#define OM_PAD_REPEAT_DELAY 18   /* frames before auto-repeat */
#define OM_PAD_REPEAT_RATE  4
Uint16 om_pad_poll(void);
Uint16 om_pad_peek(void);   /* new presses since the last poll/peek, latched for the next om_pad_poll */
Uint16 om_pad_held(void);
/* The keys in the last om_pad_poll's result that were only an auto-repeat of a held key, not a fresh press (the
 * lists wrap past their ends on a fresh press only) */
Uint16 om_pad_repeated(void);
/* Takes what is held now as already seen, so a button held across a screen change (the boot animation's skip) is not
 * a fresh press on the next screen's first om_pad_poll. Does not advance the fake pad script. */
void om_pad_prime(void);
int om_pad_soft_reset(Uint16 held);
/* the buttons that auto-repeat when held (default Up, Down, L, R: the D-pad and the paging buttons); the
 * menu sets Up/Down plus wherever the button map put Page up / Page down, so a held button that now means Play or
 * Back never repeats. */
void om_pad_set_repeat(Uint16 mask);
/* SGL's INTBACK peripheral-ID byte for port 1 (Smpc_Peripheral[0].id), for padkind.h's om_pad_kind. The
 * fake build overrides it for a scripted capture (menu/app/fake/fakepad.c); nothing else reads Smpc_Peripheral
 * directly here, unlike diag.c's own live display. */
Uint8 om_pad_port1_id(void);
#endif
