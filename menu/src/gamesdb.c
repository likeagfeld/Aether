/* menu/src/gamesdb.c -- see gamesdb.h. Rejects, never crashes on, any out-of-range offset (spec §5.1). BSD-3-Clause. */
#include <string.h>
#include "gamesdb.h"
#include "yield.h"

static void key16(const char *key, uint8_t out[16])
{
    uint32_t i;
    memset(out, 0, 16);
    for (i = 0; i < 16 && key[i]; i++)
        out[i] = (uint8_t)key[i];
}

int om_gamesdb_open(om_gamesdb *db, const uint8_t *buf, uint32_t len)
{
    om_header h;
    uint32_t i, p;
    int rc = om_header_read(buf, len, len, "OMSATGDB", &h);
    if (rc)
        return rc;
    /* compare by division, never by multiplication -- see menu/src/artpak.c's identical
     * fix for the overflow this avoids (h.count is attacker-controlled; `64u * h.count` wraps mod 2^32
     * for count near 67,108,868). h.table_size is already bounded by om_header_read's om_in_range
     * checks, so table_size/64u cannot overflow. */
    if (h.table_size % 64u != 0 || h.table_size / 64u != h.count)
        return OM_E_TABLESIZE;
    if (!h.sec_size[2] || buf[h.sec_off[2] + h.sec_size[2] - 1] != 0 || buf[h.sec_off[2]] != 0)
        return OM_E_FIELD;                  /* pool: byte 0 NUL (clarification 3) and NUL-terminated */
    if (h.sec_size[0] < 2 || h.sec_size[1] % 32)
        return OM_E_FIELD;
    db->table = buf + h.table_off;
    db->count = h.count;
    db->pool = (const char *)buf + h.sec_off[2];
    db->pool_size = h.sec_size[2];
    db->alias = buf + h.sec_off[1];
    db->nalias = h.sec_size[1] / 32;
    db->ngenre = om_be16(buf + h.sec_off[0]);
    if (db->ngenre > 255)
        return OM_E_FIELD;
    for (i = 0, p = 2; i < db->ngenre; i++) {
        db->genre[i] = (const char *)buf + h.sec_off[0] + p;
        while (p < h.sec_size[0] && buf[h.sec_off[0] + p]) p++;
        if (p >= h.sec_size[0])
            return OM_E_FIELD;
        p++;
    }
    for (i = 0; i < db->count; i++) {
        const uint8_t *r = db->table + 64 * i;
        if (!(i & 63u)) om_yield();          /* ~2,000 records in LWRAM: the start-up screen's frames (yield.h) */
        if (i && memcmp(r - 64, r, 16) >= 0) return OM_E_FIELD;
        if (r[0x11] != 0xFF && r[0x11] >= db->ngenre) return OM_E_FIELD;
        if (om_be32(r + 0x1C) >= db->pool_size || om_be32(r + 0x20) >= db->pool_size || om_be32(r + 0x24) >= db->pool_size)
            return OM_E_FIELD;
        if (om_be32(r + 0x30) >= db->pool_size)           /* compat note offset, same rule as above */
            return OM_E_FIELD;
    }
    for (i = 1; i < db->nalias; i++)
        if (memcmp(db->alias + 32 * (i - 1), db->alias + 32 * i, 16) >= 0) return OM_E_FIELD;
    return OM_OK;
}

void om_gamesdb_at(const om_gamesdb *db, uint32_t i, om_game *g)
{
    const uint8_t *r = db->table + 64 * i;
    memcpy(g->key, r, 16);
    g->key[16] = 0;
    g->disc_count = r[0x10]; g->genre = r[0x11]; g->region = r[0x12]; g->players = r[0x13]; g->user = r[0x35];
    g->periph = om_be16(r + 0x14); g->year = om_be16(r + 0x16); g->date = om_be32(r + 0x18);
    g->title_en = db->pool + om_be32(r + 0x1C);
    g->title_rom = db->pool + om_be32(r + 0x20);
    g->maker = db->pool + om_be32(r + 0x24);
}

int om_gamesdb_rec(const om_gamesdb *db, const char *key, uint32_t *rec)
{
    uint8_t k[16];
    uint32_t lo = 0, hi = db->count;
    key16(key, k);
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int c = memcmp(db->table + 64 * mid, k, 16);
        if (c == 0) { *rec = mid; return 1; }
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return 0;
}

int om_gamesdb_find(const om_gamesdb *db, const char *key, om_game *g)
{
    uint32_t rec;
    if (!om_gamesdb_rec(db, key, &rec))
        return 0;
    om_gamesdb_at(db, rec, g);
    return 1;
}

const char *om_gamesdb_genre(const om_gamesdb *db, uint8_t id)
{
    return id < db->ngenre ? db->genre[id] : NULL;
}

int om_gamesdb_alias(const om_gamesdb *db, const char *alias, char canon[17])
{
    uint8_t k[16];
    uint32_t lo = 0, hi = db->nalias;
    key16(alias, k);
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int c = memcmp(db->alias + 32 * mid, k, 16);
        if (c == 0) { memcpy(canon, db->alias + 32 * mid + 16, 16); canon[16] = 0; return 1; }
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return 0;
}

int om_gdb_compat(const om_gamesdb *db, uint32_t rec, uint8_t disc_region_bits, om_compat *out)
{
    const uint8_t *r = db->table + 64 * rec;
    uint8_t hint = r[0x2E], flags = r[0x2F], regions = r[0x34];
    uint32_t note_off = om_be32(r + 0x30);
    if (!hint && !flags && !note_off)
        return 0;                                  /* no compat data at all */
    if (regions && !(regions & disc_region_bits))
        return 0;                                  /* a region mask that excludes this disc */
    out->hint = hint;
    out->flags = flags;
    out->regions = regions;
    out->note = note_off ? db->pool + note_off : NULL;   /* note_off is bounds-checked at om_gamesdb_open */
    return 1;
}
