/* menu/app/card.h -- load GAMES.DB, VRTILES.BIN, the ART.PAK key table and LIBRARY.IDX (or scan the card);
 * spec §5.6 fallbacks.
 * Buffers are injected: LWRAM/HWRAM on the Saturn (main.c), heap in host tests. BSD-3-Clause. */
#ifndef OM_CARD_H
#define OM_CARD_H
#include "artpak.h"
#include "gamesdb.h"
#include "libidx.h"
#include "scan.h"

#define OM_LIB_FROM_IDX     0
#define OM_LIB_SCAN_MISSING 1
#define OM_LIB_SCAN_STALE   2
#define OM_LIB_IDX_KEPT     3     /* stale, but the card is too big to rescan on the console: the index is kept */

typedef struct {
    uint8_t *gamesdb; uint32_t gamesdb_cap;
    uint8_t *arttab; uint32_t arttab_cap;
    uint8_t *lib; uint32_t lib_cap;
    om_scan_work *work;
    uint8_t *vrt; uint32_t vrt_cap;     /* VRTILES.BIN (OM_LW_VRTILES on the Saturn) */
} om_card_bufs;

typedef struct {
    om_gamesdb db; int db_rc;
    om_artpak art; int art_rc; uint32_t art_size;
    om_libidx lib; int idx_rc; int lib_source;
    uint32_t card_fp;
    om_scan_report scan;
    const uint8_t *vrt; uint32_t vrt_len; int vrt_rc;   /* the virtual root tiles' table, NULL if vrt_rc */
} om_card;

typedef void (*om_status_fn)(void *ctx, const char *line);
int om_card_load(om_card *c, const om_card_bufs *b, om_status_fn st, void *ctx);
int om_card_reload_lw(om_card *c, const om_card_bufs *b);
void om_card_summary(const om_card *c, char out[3][41]);
#endif
