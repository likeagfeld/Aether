/* menu/src/slotpick.h -- the per-game save slot picker's model (a game whose Slot row is "Ask" asks at
 * launch, before the loading screen: "Save slot: < n >", A or Start launches, B goes back) and the details card's
 * "Saves in console" line. Pure, host-tested (test_slotpick.c); the view is folders.c slot_pick. Slots 0-9 are Kai's
 * layout (SPEC §5.16/§5.17: slot 0 in the game's folder, slot n in sub-folder 0n). BSD-3-Clause. */
#ifndef OM_SLOTPICK_H
#define OM_SLOTPICK_H
#include <stdint.h>
#include "gameid.h"

#define OM_SP_LAUNCH 1
#define OM_SP_CANCEL 2
/* keys: overlay.h OM_K_*. Left / Right walk 0..9 and wrap; A or Start: OM_SP_LAUNCH; B: OM_SP_CANCEL; else 0 */
int om_slot_key(uint8_t *slot, uint16_t keys);
/* ACTIVE.TXT (SPEC §5.17, "<gid> <slot> <nonce> <fp>"): its first two fields. 0, or OM_E_FIELD (gid "" then) */
int om_active_parse(const char *t, uint32_t n, char gid[OM_GID_MAX], uint8_t *slot);
/* the picker's first slot: the one the console holds for this game (ACTIVE.TXT's), else 0 */
uint8_t om_slot_first(const char *gid, const char *active_gid, uint8_t active_slot);
/* "SLOT 3: EMPTY", "SLOT 3: 1 SAVE", "SLOT 3: 2 SAVES, IN THE CONSOLE", "SLOT 3: CARD UNREADABLE" (saves < 0);
 * cut to cap, always terminated; at most 39 characters (the picker's 34-column row cuts the longest) */
void om_slot_line(char *out, uint32_t cap, uint8_t slot, int saves, int in_console);
/* the details card's meta line for the game whose saves the console holds: "Saves in console: slot n" */
void om_saves_held_line(char *out, uint32_t cap, uint8_t slot);
#endif
