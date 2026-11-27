/* menu/src/loader.h -- which loader boots a game (requirement 1; docs/research/m6-research.md §2.5 D-CWX):
 * the per-game choice (PERGAME.INI, pergame.h) > the global Settings > Loader row when it is forced (CWX or JHL) >
 * the GAMES.DB compat hint (Kai's JHL-only list, gamesdb.h OM_LH_*) > CWX, the user's default. "CWX" is Pseudo Saturn
 * Kai's self-loader (menu/launcher/cwxboot.h), "JHL" today's BIOS boot (menu/launcher/jhboot.h). The result is what
 * the hand-off block carries (handoff.h `loader`: resolved, never AUTO). BSD-3-Clause. */
#ifndef OM_LOADER_H
#define OM_LOADER_H
#include <stdint.h>
#include "gamesdb.h"

#define OM_CWX_AVAILABLE 1   /* the launcher has the CWX boot (D-CWX: Kai's self-loader, no S5 dependency) */

enum { OM_LWHY_USER, OM_LWHY_GLOBAL, OM_LWHY_DB, OM_LWHY_DEFAULT, OM_LWHY_NOCWX };
typedef struct { uint8_t loader, why, bad; } om_loader_choice;   /* bad: the database says both loaders fail */
/* per_game / global: OM_LDR_* (settings.h), OM_LDR_AUTO = no choice; db_hint: OM_LH_* */
om_loader_choice om_loader_pick(uint8_t per_game, uint8_t global, uint8_t db_hint, int cwx_available);
const char *om_loader_why(const om_loader_choice *c);   /* "CWX (auto)", "JHL (database)", "JHL (your choice)" ... */
/* the per-game panel's Loader value into buf (cut to cap, terminated): a per-game choice as itself ("CWX",
 * "JHL"); Auto as what auto_pick (om_loader_pick with per_game Auto) resolves to, and why unless it is the plain
 * default: "Auto (CWX)", "Auto (JHL, database)", "Auto (CWX, settings)", "Auto (CWX, database: may fail)". A per-game CWX when auto_pick says CWX is not available reads
 * "JHL (CWX not available)", as the launch would pick. */
const char *om_loader_panel(uint8_t per_game, const om_loader_choice *auto_pick, char *buf, uint32_t cap);
/* The compat hint for a LIBRARY.IDX entry's matched key on a disc with these GAMES.DB region bits (sysid.h
 * om_area_flags): OM_LH_NONE when GAMES.DB did not load (db_ok 0), the key is empty or unknown, or the record has no
 * compat row for this disc's regions (om_gdb_compat). */
uint8_t om_loader_db_hint(const om_gamesdb *db, int db_ok, const char *key, uint8_t disc_region_bits);
/* whether to arm the in-game reset for this launch. In order: OM_CF_NOIGR (gamesdb.h compat flags)
 * forces it off; otherwise an explicit per-game choice (per_game OM_PG_ON/OM_PG_OFF, pergame.h -- out-of-range bytes
 * read as OM_PG_DEFAULT) wins; otherwise the global setting (global_on: settings.h s_ps.set.igr). Returns 1 (arm
 * it) or 0. per_game is pergame.h's own OM_PG_* encoding, passed through unchanged (the same convention om_loader_
 * pick uses for OM_LDR_*): translating it at the call site is exactly the kind of drift this function exists to
 * prevent. */
int om_igr_want(int global_on, uint8_t per_game, uint8_t compat_flags);
/* the per-game panel's Soft reset value into buf (cut to cap, terminated, as om_loader_panel): a
 * per-game choice as itself ("On", "Off"); Default as what om_igr_want resolves to there, e.g. "Default (On)",
 * "Default (Off)" -- including when OM_CF_NOIGR forces it off under Default. */
const char *om_igr_panel(uint8_t per_game, int global_on, uint8_t compat_flags, char *buf, uint32_t cap);
/* per-game saves for this launch -- on when the global setting is on and the game's Saves row is not Off,
 * or the row is On; never for a save tool (Kai's rule, pseudo_bram.c:1203-1223: the tool must see the console's own
 * saves). per_game: pergame.h OM_PG_*, out-of-range bytes read as Default. 1 or 0. */
int om_saves_want(int global_on, uint8_t per_game, int savetool);
/* a disc that manipulates backup RAM (tools/data/compat/save_tools.json, kept in sync with this file by
 * tools/tests/test_save_tools.py): its IP product number is one of the tools' (exactly), or its IP title contains one
 * of their title substrings (ASCII case-insensitive). For tools GAMES.DB does not know; the card's own matches carry
 * OM_CF_SAVETOOL. NULLs read as "". 1 or 0. */
int om_savetool_ip(const char *product, const char *title);
#endif
