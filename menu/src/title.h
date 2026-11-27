/* menu/src/title.h -- display title (spec §5.6 title order) and the three detail lines (spec §7.4). BSD-3-Clause. */
#ifndef TITLE_H
#define TITLE_H
#include <stdint.h>
#include "gamesdb.h"
#include "libidx.h"
#include "sysid.h"

/* The name the list shows for a game (spec §5.6; 2026-11-23, Satiator Rings' model): the LIBRARY.IDX title override,
 * else GAMES.DB's title_en when the user set it (OM_GU_TITLE), else -- its image in a single-game folder (its directory
 * is not its LIBRARY.IDX folder row) -- that folder's name, a span of e->path ending at its '/', else its exact file
 * name, extension included. l may be NULL (no folder table: the file name). Per frame (folders.c row labels): HOT. */
const char *om_entry_name(const om_libidx *l, const om_lib_entry *e, const om_game *g, uint32_t *len);
/* rules 3 and 4 only: the folder or file name, whatever the titles (2026-11-27, the user's rule "games are listed by
 * their exact file name"): every list row and the A-Z order use it; the info line keeps om_title */
const char *om_entry_raw(const om_libidx *l, const om_lib_entry *e, uint32_t *len);
void om_title_raw(const om_libidx *l, const om_lib_entry *e, char *out, uint32_t cap);
/* om_entry_name, plus " (Disc n)" after a user title of a multi-disc game (its discs share the title) */
void om_title(const om_libidx *l, const om_lib_entry *e, const om_game *g, char *out, uint32_t cap);
void om_details(const om_lib_entry *e, const om_game *g, const char *genre, const om_sysid *ip, char out[3][23]);
void om_region_letters(uint8_t flags, char out[6]);
#endif
