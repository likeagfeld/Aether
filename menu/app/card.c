/* menu/app/card.c -- see card.h. A missing or corrupt GAMES.DB or ART.PAK costs metadata or art, never listing;
 * a missing, corrupt or stale LIBRARY.IDX means an on-console scan (spec §5.6, v1 clarification 6) -- unless the card is
 * too big to scan on the console (OM_E_FULL), when a stale index is kept and named, and a LIBRARY.IDX over the menu's
 * buffer is OM_E_FULL at once (2026-11-23 hardware report). BSD-3-Clause. */
#include <string.h>
#include "card.h"
#include "satfs.h"
#include "satiator.h"
#include "sd.h"
#include "vrtile.h"

static int load_db(om_card *c, const om_card_bufs *b)
{
    uint32_t len;
    int rc = om_sd_read_all(OM_PATH_GAMESDB, b->gamesdb, b->gamesdb_cap, &len);
    return rc ? rc : om_gamesdb_open(&c->db, b->gamesdb, len);
}

static int load_vrt(om_card *c, const om_card_bufs *b)
{   /* the virtual root folders' tiles (SPEC §5.18); without them those covers show NO ART and a named message */
    uint32_t len = 0;
    int rc = om_sd_read_all(OM_PATH_VRTILES, b->vrt, b->vrt_cap, &len);
    c->vrt = NULL;
    c->vrt_len = 0;
    return rc ? rc : om_vrtile_open(b->vrt, len, &c->vrt, &c->vrt_len);
}

static int load_art(om_card *c, const om_card_bufs *b)
{
    uint32_t size, need;
    int fd, rc;
    if ((rc = om_sd_size(OM_PATH_ARTPAK, &size)) < 0) return rc;
    if (size < OM_HEADER_SIZE) return OM_E_SHORT;
    if ((fd = om_sd_open(OM_PATH_ARTPAK)) < 0) return fd;
    rc = om_sd_read_at(fd, 0, b->arttab, OM_HEADER_SIZE);
    if (!rc) {
        uint32_t toff = om_be32(b->arttab + 0x10), tsize = om_be32(b->arttab + 0x14);
        need = toff + tsize;
        if (need < toff || need < OM_HEADER_SIZE || need + 4 > b->arttab_cap) rc = OM_E_FULL;
        else rc = om_sd_read_at(fd, 0, b->arttab, need);
    }
    s_close(fd);
    if (rc) return rc;
    c->art_size = size;
    return om_artpak_open(&c->art, b->arttab, need, size);
}

int om_card_reload_lw(om_card *c, const om_card_bufs *b)
{
    c->db_rc = load_db(c, b);
    c->vrt_rc = load_vrt(c, b);
    c->art_rc = load_art(c, b);
    return OM_OK;
}

int om_card_load(om_card *c, const om_card_bufs *b, om_status_fn st, void *ctx)
{
    uint32_t len;
    int rc;
    memset(c, 0, sizeof *c);
    st(ctx, "Loading GAMES.DB");
    c->db_rc = load_db(c, b);
    st(ctx, "Loading VRTILES.BIN");
    c->vrt_rc = load_vrt(c, b);
    st(ctx, "Loading ART.PAK key table");
    c->art_rc = load_art(c, b);
    st(ctx, "Reading the card root");
    if ((rc = om_satfs_root_fp(b->work, &c->card_fp)) < 0)
        return rc;
    st(ctx, "Loading LIBRARY.IDX");
    c->idx_rc = om_sd_read_all(OM_PATH_LIBIDX, b->lib, b->lib_cap, &len);
    if (c->idx_rc == OM_E_FULL)
        return OM_E_FULL;                   /* larger than the menu's buffer: a scan's own index could not fit either */
    if (!c->idx_rc)
        c->idx_rc = om_libidx_open(&c->lib, b->lib, len);
    if (!c->idx_rc && c->lib.fingerprint == c->card_fp) {
        c->lib_source = OM_LIB_FROM_IDX;
        return OM_OK;
    }
    c->lib_source = c->idx_rc ? OM_LIB_SCAN_MISSING : OM_LIB_SCAN_STALE;
    st(ctx, c->idx_rc ? "LIBRARY.IDX missing or bad: scanning card" : "LIBRARY.IDX stale: scanning card");
    rc = om_scan(om_satfs(), c->db_rc ? NULL : &c->db, b->work, 0, b->lib, b->lib_cap, &len, &c->scan);
    if (rc == OM_E_FULL && !c->idx_rc) {
        /* 2026-11-23: the card is too big for an on-console scan (a folder over the scan's listing, or the entry, path
         * or index caps). A stale index still lists the card: kept, and named in the menu's status line. The scan may
         * have written over b->lib, so it is read again. */
        c->lib_source = OM_LIB_IDX_KEPT;
        st(ctx, "Too big to rescan: stale LIBRARY.IDX");
        if ((rc = om_sd_read_all(OM_PATH_LIBIDX, b->lib, b->lib_cap, &len)) != 0)
            return rc;
        return om_libidx_open(&c->lib, b->lib, len);
    }
    if (rc)
        return rc;
    return om_libidx_open(&c->lib, b->lib, len);
}

static int app(char *out, int o, const char *s)
{
    while (*s && o < 40) out[o++] = *s++;
    out[o] = 0;
    return o;
}

static int appn(char *out, int o, int v)
{
    char t[12], r[13];
    int n = 0, i = 0;
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) r[i++] = '-';
    while (n) r[i++] = t[--n];
    r[i] = 0;
    return app(out, o, r);
}

void om_card_summary(const om_card *c, char out[3][41])
{
    int o = app(out[0], 0, "GAMES.DB: ");
    if (c->db_rc) { o = app(out[0], o, om_strerror(c->db_rc)); o = app(out[0], o, " rc "); appn(out[0], o, c->db_rc); }
    else { o = appn(out[0], o, (int)c->db.count); app(out[0], o, " records"); }
    o = app(out[1], 0, "ART.PAK: ");
    if (c->art_rc) { o = app(out[1], o, om_strerror(c->art_rc)); o = app(out[1], o, " rc "); appn(out[1], o, c->art_rc); }
    else { o = appn(out[1], o, (int)c->art.count); app(out[1], o, " entries"); }
    if (c->lib_source == OM_LIB_IDX_KEPT) {     /* the menu's status line: what to do, in 40 columns */
        app(out[2], 0, "STALE LIBRARY.IDX: REBUILD IT ON THE PC");
        return;
    }
    o = app(out[2], 0, c->lib_source == OM_LIB_FROM_IDX ? "LIBRARY.IDX: " :
                       c->lib_source == OM_LIB_SCAN_STALE ? "SCAN (index stale): " : "SCAN (no index): ");
    o = appn(out[2], o, (int)c->lib.count);
    o = app(out[2], o, " games, ");
    o = appn(out[2], o, (int)c->lib.nfolders);
    app(out[2], o, " folders");
}
