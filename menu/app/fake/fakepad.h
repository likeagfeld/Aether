/* menu/app/fake/fakepad.h -- --app-fake only: a frame-scripted pad that tours the Folders view. BSD-3-Clause. */
#ifndef OM_FAKEPAD_H
#define OM_FAKEPAD_H
#include "sgl.h"
void om_fakepad_tick(void);          /* once per frame, from om_pad_poll */
Uint16 om_fakepad_held(void);        /* pressed = 1, PER_DGT_* layout (HWF-14) */
Uint16 om_fakepad_peek(void);        /* the keys now, between polls (V-blank-timed scripts; else held) */
const char *om_fakepad_label(void);  /* "NN WHAT" of the step on screen, "" before the first */
/* port 1's real Smpc_Peripheral[0].id, unless aether/FAKESCRIPT.PAD3D is on the card -- then the 3D
 * Control Pad's analog-mode id (0x16, HWF-77), so the pre-launch warning can be captured on the emulator, which has
 * no way to present that peripheral itself. */
Uint8 om_fakepad_port1_id(void);
#endif
