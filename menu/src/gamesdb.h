/* menu/src/gamesdb.h -- GAMES.DB reader (contract/SPEC.md §5.3 + v1 clarifications 1, 3). BSD-3-Clause. */
#ifndef GAMESDB_H
#define GAMESDB_H
#include <stdint.h>
#include "om_fmt.h"

typedef struct {
    char key[17];
    uint8_t disc_count, genre, region, players, user;   /* user: byte 0x35, OM_GU_* */
    uint16_t periph, year;
    uint32_t date;
    const char *title_en, *title_rom, *maker;
} om_game;

typedef struct {
    const uint8_t *table;
    uint32_t count;
    const char *pool;
    uint32_t pool_size;
    const uint8_t *alias;
    uint32_t nalias;
    uint16_t ngenre;
    const char *genre[255];
} om_gamesdb;

/* SPEC §5.3 amendment, record bytes 0x2E-0x3F: loader hint and compat flags. */
enum { OM_LH_NONE, OM_LH_JHL, OM_LH_CWX, OM_LH_BAD };
#define OM_CF_3DPAD    0x01u
#define OM_CF_STDPAD   0x02u
#define OM_CF_MULTITAP 0x04u
#define OM_CF_STICK    0x08u
#define OM_CF_NETLINK  0x10u
#define OM_CF_NOIGR    0x20u
#define OM_CF_NOCHEAT  0x40u
#define OM_CF_SAVETOOL 0x80u     /* a save tool (save_tools.json): per-game saves paused */
/* 2026-11-23 (SPEC §5.3 byte 0x35): title_en is the user's own title (Card Manager's USEREDIT.INI), so the list shows
 * it instead of the game's folder or file name (title.h om_entry_name). */
#define OM_GU_TITLE    0x01u
typedef struct { uint8_t hint, flags, regions; const char *note; } om_compat;   /* note NULL = none */

int om_gamesdb_open(om_gamesdb *db, const uint8_t *buf, uint32_t len);
int om_gamesdb_find(const om_gamesdb *db, const char *key, om_game *g);
/* the 0-based record index of key (what om_gdb_compat takes); 1 found, 0 not. */
int om_gamesdb_rec(const om_gamesdb *db, const char *key, uint32_t *rec);
void om_gamesdb_at(const om_gamesdb *db, uint32_t i, om_game *g);
const char *om_gamesdb_genre(const om_gamesdb *db, uint8_t id);
int om_gamesdb_alias(const om_gamesdb *db, const char *alias, char canon[17]);
/* Bytes 0x2E/0x2F/0x30/0x34 of record `rec` (0-based, as om_gamesdb_at indexes). 1 = applies: out is filled and
 * either the mask at 0x34 is 0 (every region) or it intersects disc_region_bits. 0 = no compat data at all
 * (hint, flags and note offset all zero), or a non-zero mask that does not intersect disc_region_bits; out is
 * then left unset. */
int om_gdb_compat(const om_gamesdb *db, uint32_t rec, uint8_t disc_region_bits, om_compat *out);
#endif
