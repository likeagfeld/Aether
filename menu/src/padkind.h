/* menu/src/padkind.h -- requirement 7: the pre-launch pad warning and the details/panel note. SGL's
 * INTBACK peripheral ID byte packs the device type in the high nibble and the data size in the low (SMPC_Manual.txt
 * Table 3.10/3.11: digital type 0H size 2 -> 0x02, analog type 1H size 5 -> 0x15 for a generic analog device). The
 * Sega 3D Control Pad does not read as a generic analog device: ids measured on hardware this ruling relays
 * (unverified, from the Task 19 dispatch ruling 2026-11-21 -- the photos are not in this repo; see docs/decisions/
 * m2-hw-facts.md HWF-77) are standard pad 0x02, 3D Control Pad digital mode 0x02 (identical to the standard pad: SGL
 * cannot tell them apart) and 3D Control Pad analog mode 0x16 (one byte past SL_DEF.H's generic PER_ID_StnAnalog
 * 0x15 -- the pad's extra axis byte, consistent with SMPC_Manual.txt Table 3.11's type nibble 1). Because digital mode
 * is indistinguishable from a standard pad, OM_PAD_3D_DIGITAL can never actually be returned by om_pad_kind; the
 * enum value is kept so the interface matches this file's brief. BSD-3-Clause. */
#ifndef OM_PADKIND_H
#define OM_PADKIND_H
#include <stdint.h>

/* review-task-19.md Minor: OM_PAD_OTHER, like OM_PAD_3D_DIGITAL above, can never actually
 * be returned by om_pad_kind either -- the function only ever yields NONE/STD/3D_ANALOG/UNKNOWN (padkind.c), and
 * the brief's own test (test_padkind.c) uses OM_PAD_UNKNOWN for "anything else". Both values are kept only so this
 * header matches the brief's enum exactly. */
enum { OM_PAD_NONE, OM_PAD_STD, OM_PAD_3D_DIGITAL, OM_PAD_3D_ANALOG, OM_PAD_OTHER, OM_PAD_UNKNOWN };

/* Step 2 (the 3D pad's analog-mode ID) is measured -- see the header comment above and HWF-77 -- so the pre-launch
 * warning (om_pad_conflict) is live. */
#define OM_PADKIND_TRUSTED 1

/* sgl_id: Smpc_Peripheral[n].id, raw. data_size is SGL's PerDigital/PerAnalog.ext (SL_DEF.H); kept for interface
 * stability (the brief's signature) but not needed for the decision: every kind this function returns is already
 * fully determined by sgl_id alone (see the citations above). */
uint8_t om_pad_kind(uint8_t sgl_id, uint8_t data_size);
/* 1 = warn before launch: a 3D Control Pad in analog mode (the only kind SGL can actually tell apart from a standard
 * pad) on a game whose GAMES.DB compat flags (gamesdb.h OM_CF_*) name OM_CF_3DPAD. Off (always 0) until
 * OM_PADKIND_TRUSTED is set; OM_PAD_UNKNOWN never blocks a launch. */
int om_pad_conflict(uint8_t kind, uint8_t compat_flags);
#endif
