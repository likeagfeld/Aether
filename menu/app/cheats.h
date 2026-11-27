/* menu/app/cheats.h -- one game's record from /aether/CHEATS.PAK (contract/SPEC.md §5.15).
 * BSD-3-Clause. */
#ifndef OM_CHEATS_H
#define OM_CHEATS_H
#include <stdint.h>
#include "cheatpak.h"

/* key: the game's GAMES.DB product key. stage: 4-aligned, >= OM_CP_STAGE bytes, live only during the call (a launch-
 * time caller keeps it below OM_LW_STAGE + 0x8800, docs/ledger.md Task 18b). Opens CHEATS.PAK (or, when it is absent,
 * CHEATS.PAK.NEW: sdwrite.h's recovery rule), reads the header and the whole index in OM_CP_PIECE pieces (its CRC is
 * checked, so a damaged index is refused even when the key's row is intact), then the one record. Returns 1 with *f
 * filled, 0 when the key has no record, OM_SD_NOFILE when there is no file, or an error (OM_E_* or an SD code). */
int om_cheats_load(const char *key, uint8_t *stage, uint32_t cap, om_ch_file *f);
/* 2026-11-25: the same lookup, the record counted instead (cheatpak.h om_cp_count): 1 with *on and *n, 0 when the
 * key has no record, OM_SD_NOFILE when there is no file, or an error. */
int om_cheats_count(const char *key, uint8_t *stage, uint32_t cap, uint32_t *on, uint32_t *n);
#endif
