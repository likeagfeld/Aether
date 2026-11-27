/* menu/app/sd.h -- SD-card access through libsatiator (spec §2: <= 2 KB per call; HWF-22). Portable: host tests run
 * it over menu/host-tests/fake_satiator.c. BSD-3-Clause. */
#ifndef OM_SD_H
#define OM_SD_H
#include <stdint.h>
#include "sdwrite.h"
#define OM_PATH_GAMESDB  "/aether/GAMES.DB"
#define OM_PATH_ARTPAK   "/aether/ART.PAK"
#define OM_PATH_LIBIDX   "/aether/LIBRARY.IDX"
#define OM_PATH_LAUNCHER "/aether/LAUNCHER.BIN"
#define OM_PATH_LOADBG   "/aether/LOADBG.BIN"     /* the loading screen's background (SPEC §5.11) */
#define OM_PATH_VRTILES  "/aether/VRTILES.BIN"    /* the virtual root folders' tiles (SPEC §5.18) */
#define OM_PATH_CHEATPAK "/aether/CHEATS.PAK"     /* every game's cheats (SPEC §5.15) */
/* om_sd_err(SAT_ETIMEDOUT) == -100 + (-20) == -120. SAT_ETIMEDOUT is
 * menu/lib/satiator/satiator.h's new (non-upstream) sentinel for exec_cmd's bounded-wait cap expiring;
 * this is the value an om_sd_* call (and om_satfs_root_fp/om_card_load) returns for that case. Kept as
 * a literal here (not #include "satiator.h") to match this header's existing style of mapping FRESULT
 * codes by literal range (see menu/src/om_fmt.c's om_strerror). */
#define OM_SD_TIMEOUT (-120)
const char *om_sd_path(const char *p);
int om_sd_err(int fr);   /* libsatiator negative FRESULT -> -100 - FR_xxx (never collides with OM_E_*) */
int om_sd_size(const char *path, uint32_t *size);
int om_sd_open(const char *path);
int om_sd_read_at(int fd, uint32_t off, void *dst, uint32_t len);
int om_sd_read_all(const char *path, void *dst, uint32_t cap, uint32_t *len);
#define OM_PATH_FAVS     "/aether/FAVS.TXT"
#define OM_PATH_RECENT   "/aether/RECENT.TXT"
#define OM_PATH_SETTINGS "/aether/SETTINGS.INI"
#define OM_PATH_ACTIVE   "/aether/SAVES/ACTIVE.TXT"  /* which game's saves BRAM holds (SPEC §5.17) */
#define OM_PATH_PERGAME  "/aether/PERGAME.INI"    /* per-game choices (SPEC §5.14) */
#define OM_PATH_PERGAME_BAK "/aether/PERGAME.BAK" /* a corrupt PERGAME.INI, kept before it is rewritten */
#define OM_SD_NOFILE     (-104)          /* om_sd_err(-FR_NO_FILE): a file that is simply absent */
/* writes go through menu/src/sdwrite.c (sdwrite.h: 2,044 B synced chunks, a gap between them, one retry
 * per chunk) with sd.c's 4-aligned 2 KB buffer as scratch and om_sd_gap between chunks. src fills dst with the next
 * bytes and returns their length, 0 at the end (or, only while cap < OM_SDW_PIECE_MAX, "no room for my next piece"),
 * < 0 on error. om_sd_write_file/_buf create or truncate in place (FA_CREATE_ALWAYS; not power-cut safe);
 * om_sd_replace is sdwrite.h's write-then-replace, read back with om_sdw_read_file (its recovery rule). */
typedef om_sdw_src_fn om_sd_src_fn;
extern om_sdw_gap_fn om_sd_gap;
extern uint32_t om_sd_gap_late;
int om_sd_write_file(const char *path, om_sd_src_fn src, void *ctx);
int om_sd_write_buf(const char *path, const void *data, uint32_t len);
int om_sd_replace(const char *path, const void *data, uint32_t len);
/* om_sd_replace into a folder that may not exist yet (/aether/SAVES on a card no game has launched from): dir is made
 * first, one level */
int om_sd_replace_in(const char *dir, const char *path, const void *data, uint32_t len);
#endif
