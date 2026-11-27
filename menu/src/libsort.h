/* menu/src/libsort.h -- spec §7.6 sorting (A-Z, genre, region, year) and combinable genre/region filters over
 * LIBRARY.IDX entries, keyed once per card load. BSD-3-Clause. */
#ifndef OM_LIBSORT_H
#define OM_LIBSORT_H
#include <stdint.h>
#include "gamesdb.h"
#include "libidx.h"
#define OM_SORT_MAX   2048u          /* = OM_MAX_ENTRIES (tree.h) */
#define OM_FILTER_ANY 0xFFFFu
/* title: the shown name (title.h om_entry_name); for a single-game folder's game a span of the entry's path, ending at
 * the folder's '/', and so flagged in om_sortctx.span (one bit an entry, not a byte a key: 2,048 keys). */
typedef struct { const char *title; uint16_t year; uint8_t genre, region; } om_sortkey;
typedef struct {
    om_sortkey key[OM_SORT_MAX];
    uint8_t span[OM_SORT_MAX / 8];
    uint8_t genre_rank[256], genre_by_rank[256];
    uint16_t ngenre;
    uint32_t n;
} om_sortctx;
void om_sortctx_build(om_sortctx *c, const om_libidx *l, const om_gamesdb *db);
void om_sort_entries(const om_sortctx *c, uint8_t mode, uint16_t *idx, uint32_t n, uint16_t *tmp);
/* the two-entry comparison om_sort_entries' om_msort callback wraps -- exposed so browse.c can order a mix of
 * loose games and single-game folders (each resolved to the game whose key it sorts by) with the same rule. */
int om_sort_cmp(const om_sortctx *c, uint8_t mode, uint16_t a, uint16_t b);
int om_entry_passes(const om_sortctx *c, uint16_t e, uint16_t genre, uint8_t region_mask);
int om_title_cmp(const char *a, const char *b);
/* names compared as the list sorts them (case-insensitive, then bytes); sa / sb: the name is a span ending at '/' */
int om_name_cmp(const char *a, int sa, const char *b, int sb);
/* 2026-11-23 (Satiator Rings' model): entry e is a single-game folder's game (named and listed as the folder) */
int om_sort_is_folder(const om_sortctx *c, uint16_t e);
/* key e's name, NUL-terminated, into out (host tests; the menu draws om_title's) */
void om_sortkey_name(const om_sortctx *c, uint16_t e, char *out, uint32_t cap);
/* The one rule for a game's region (2026-11-20 hardware report: the list's flag and the Region sort disagreed): the
 * region bits (J0 T1 U2 E3 B4) are GAMES.DB's, else LIBRARY.IDX's IP.BIN area bits; the primary area is the first
 * set bit in J T U B E order (uiline.c's canonical area order), returned as its bit index, 5 when none is set. The
 * sort groups by it and uiline.c om_primary_area draws its flag. g may be NULL (no GAMES.DB record). */
uint8_t om_region_bits(const om_lib_entry *e, const om_game *g);
uint8_t om_region_primary(uint8_t bits);
/* The year a GAMES.DB record sorts and shows by: year, else date / 10000 (YYYYMMDD); 0 when unknown. */
uint16_t om_game_year(const om_game *g);
uint8_t om_title_letter(const char *title);
#endif
