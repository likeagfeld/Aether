/* menu/src/discset.h -- multi-disc sets at launch (contract/SPEC.md §5.4 flags bits 2 and 4-15, written by the PC card
 * builders: tools/omsat/discsets.py). The menu reads them only when a game is launched: no per-frame cost and no table
 * kept in HWRAM -- one pass over the loaded LIBRARY.IDX. BSD-3-Clause. */
#ifndef OM_DISCSET_H
#define OM_DISCSET_H
#include <stdint.h>
#include "libidx.h"
#include "handoff.h"

#define OM_LIB_ALT        4u                               /* an alternate image of its disc number */
#define OM_LIB_SET(flags) ((uint16_t)((flags) >> 4))       /* the set id, 0 = none */

/* The set's lead image, whose IP.BIN names the game (its saves, PERGAME.INI section, cheats): the lowest disc number
 * of idx's set -- idx itself when idx has that number, else that disc's primary. idx when idx is in no set. */
uint32_t om_discset_lead(const om_libidx *l, uint32_t idx);

/* Puts idx's set into a filled, valid block h (om_handoff_fill2): for each disc number 1..9 present, idx when it has
 * that number, else the primary; dir becomes their common parent folder ("/" + the longest common folder prefix),
 * image idx's path from it, discs[] every disc's path from it, cur idx's place; the CRC is redone. Returns the disc
 * count (2..9), or 0 when idx is in no set of 2 or more, or the paths do not fit (h is then unchanged: one disc). */
uint32_t om_discset_fill(om_handoff *h, const om_libidx *l, uint32_t idx);
#endif
