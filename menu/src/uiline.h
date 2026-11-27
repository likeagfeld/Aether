/* menu/src/uiline.h -- the Folders view's text lines in the approved mockup's wording: the details meta
 * line "Genre  ·  Year  ·  N player(s)  ·  Disc n/m  ·  every area in words", the breadcrumb "Games  ›  RPG", the
 * "N games" count,
 * and the split of a status line into its reason and its rc. Portable C, host-tested (test_uiline.c). BSD-3-Clause. */
#ifndef OM_UILINE_H
#define OM_UILINE_H
#include <stdint.h>
#include "gamesdb.h"
#include "libidx.h"
#include "sysid.h"
#include "uifont.h"

#define OM_SEP_DOT "  \xC2\xB7  "            /* "  ·  " */
#define OM_SEP_CRUMB "  \xE2\x80\xBA  "      /* "  ›  " */

void om_meta_line(const om_lib_entry *e, const om_game *g, const char *genre, const om_sysid *ip, char *out,
                  uint32_t cap);
/* A game's area codes in IP.BIN order J T U B K A E L (addendum 6): IP.BIN's area string when known, else the region
 * bits. The list's flag is om_primary_area, not out[0]; om_area_name spells one out ("Japan", "USA", ...). */
void om_area_codes(const om_lib_entry *e, const om_game *g, const om_sysid *ip, char out[9]);
/* The one flag a game's row shows: its region sort group's area (libsort.h om_region_bits), so the list never shows a
 * flag the Region sort does not group it by; IP.BIN's area string only when the game has no region bits. 0 = none. */
char om_primary_area(const om_lib_entry *e, const om_game *g, const om_sysid *ip);
const char *om_area_name(char code);
/* path is om_browse_path's "/A/B" ("/" at the root) -> "Games  ›  A  ›  B", whole (the view marquees it). */
void om_crumb(const char *path, char *out, uint32_t cap);
/* "1 game", "12 games"; with no games, "N items" (folders only) or "empty". */
void om_count_line(uint32_t games, uint32_t items, char *out, uint32_t cap);
/* Where om_msg_rc's " rc <n>" suffix starts in a status message, or NULL. The view keeps that suffix fixed and whole at
 * the right of the strip and marquees only the reason before it when the line is too wide (addendum 3). */
const char *om_split_rc(const char *msg);

/* Task 13c, the loading screen (loading_mockup_render.py): line 1 "Developer  ·  Year  ·  Genre", line 2
 * "Disc n of m  ·  every area in words  ·  N player(s)"; parts that are unknown are left out (an empty line if
 * none). */
void om_load_meta(const om_lib_entry *e, const om_game *g, const char *genre, const om_sysid *ip, char *l1,
                  uint32_t cap1, char *l2, uint32_t cap2);
/* The title on at most two lines of box_w px: line 1 takes the most whole words that fit, line 2 the rest (the view
 * marquees it when it is wider). One line when it all fits, or when its first word alone is wider than the box (that
 * line then marquees). Returns the number of lines; never abbreviates. */
int om_title_wrap(const om_face *f, const char *s, int box_w, char *l1, uint32_t cap1, char *l2, uint32_t cap2);
#endif
