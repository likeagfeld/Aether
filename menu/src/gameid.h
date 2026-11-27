/* menu/src/gameid.h -- the game id: IP.BIN product number + area symbols -> the PERGAME.INI section name and the
 * per-game SAVES folder name (contract/SPEC.md §5.14). The rule is Pseudo Saturn Kai's save-folder rule (cafe-alpha,
 * GPL-2.0; pseudo_bram.c, re-implemented, not copied; docs/research/m6-research.md §1.2), so Kai saves carry over:
 * - product: the 10 bytes at IP 0x20 up to the first byte <= ' '; a-z upper-cased; anything outside 0-9 A-Z _ -
 *   becomes '_'. Empty: an error (Kai would use "_UNKNOWN").
 * - area: every A-Z of the 10 bytes at IP 0x40 (ST-040-R4 §4.2: 10 characters), in field order, stopping at a NUL,
 *   at most 8 (Kai's game_info.area[8]).
 * ini id "MK-81214@UT", folder "MK-81214[UT]"; no area letters: "MK-81214@", folder "MK-81214". BSD-3-Clause. */
#ifndef OM_GAMEID_H
#define OM_GAMEID_H
#include <stdint.h>
#define OM_GID_PRODUCT 10u
#define OM_GID_AREAS    8u
#define OM_GID_MAX     20                     /* "<10>@<8>" + NUL */
int om_gameid_ini(const uint8_t product[10], const uint8_t areas[10], char out[OM_GID_MAX]);   /* "MK-81045@U" */
int om_gameid_dir(const char *ini_id, char out[OM_GID_MAX + 1]);                                /* "MK-81045[U]" */
int om_gameid_check(const char *ini_id);      /* OM_OK when ini_id is a well-formed id, else OM_E_FIELD */
#endif
