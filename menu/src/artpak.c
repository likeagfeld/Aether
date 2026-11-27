/* menu/src/artpak.c -- see artpak.h. VDP1 limits per VDP1_Manual.txt:2572-2580 (HWF-06). The menu's hero
 * canvases are 160x160 and 128x200 only, and JP bitmaps are <= 160x32 (omlayout.h). BSD-3-Clause. */
#include <string.h>
#include "artpak.h"
#include "omlayout.h"

static void key16(const char *key, uint8_t out[16])
{
    uint32_t i;
    memset(out, 0, 16);
    for (i = 0; i < 16 && key[i]; i++)
        out[i] = (uint8_t)key[i];
}

static int cmp_row(const uint8_t *row, const uint8_t k[16], uint8_t disc)
{
    int c = memcmp(row, k, 16);
    return c ? c : (int)row[16] - (int)disc;
}

int om_artpak_open(om_artpak *a, const uint8_t *buf, uint32_t buf_len, uint32_t file_size)
{
    om_header h;
    uint32_t i;
    int rc = om_header_read(buf, buf_len, file_size, "OMSATART", &h);
    if (rc)
        return rc;
    /* compare by division, never by multiplication -- h.count is an attacker-controlled
     * u32 read straight from the file (om_fmt.c om_header_read), and `48u * h.count` wraps mod 2^32
     * for count near 89,478,486 (48*89478486 mod 2^32 == 32), which could slip a small, in-range,
     * CRC-valid table_size past this guard while a->count stays huge; the sortedness loop below would
     * then read far out of bounds. h.table_size is already bounded (<= file_size and <= buf_len, via
     * om_in_range in om_header_read), so table_size/48u cannot overflow; only the division form is safe. */
    if (h.table_size % 48u != 0 || h.table_size / 48u != h.count)
        return OM_E_TABLESIZE;
    if (h.data_off % OM_SECTOR)
        return OM_E_FIELD;
    a->table = buf + h.table_off;
    a->count = h.count;
    a->data_off = h.data_off;
    a->data_size = h.data_size;
    for (i = 1; i < h.count; i++)
        if (cmp_row(a->table + 48 * (i - 1), a->table + 48 * i, a->table[48 * i + 16]) >= 0)
            return OM_E_FIELD;    /* binary search needs strictly sorted (key, disc) */
    return OM_OK;
}

void om_artpak_entry(const om_artpak *a, uint32_t i, om_art_entry *e)
{
    const uint8_t *r = a->table + 48 * i;
    memcpy(e->key, r, 16);
    e->key[16] = 0;
    e->disc = r[0x10]; e->flags = r[0x11];
    e->thumb_w = r[0x12]; e->thumb_h = r[0x13]; e->hero_w = r[0x14]; e->hero_h = r[0x15];
    e->jp_w = om_be16(r + 0x16); e->jp_h = r[0x18];
    e->thumb_sec = om_be32(r + 0x1C); e->hero_sec = om_be32(r + 0x20); e->jp_sec = om_be32(r + 0x24);
    e->manifest_row = om_be32(r + 0x28); e->blob_crc = om_be32(r + 0x2C);
}

int om_artpak_find(const om_artpak *a, const char *key, uint8_t disc, om_art_entry *e)
{
    uint8_t k[16];
    uint32_t lo = 0, hi = a->count;
    key16(key, k);
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        int c = cmp_row(a->table + 48 * mid, k, disc);
        if (c == 0) { om_artpak_entry(a, mid, e); return 1; }
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return 0;
}

static int find_with(const om_artpak *a, const char *key, uint8_t disc, uint8_t bit, om_art_entry *e)
{
    if (disc && om_artpak_find(a, key, disc, e) && (e->flags & bit))
        return 1;                      /* spec §5.6 step 2: (key, disc) */
    return om_artpak_find(a, key, 0, e) && (e->flags & bit);   /* step 3: (key, 0) */
}

int om_artpak_cover(const om_artpak *a, const char *key, uint8_t disc, om_art_entry *e) { return find_with(a, key, disc, OM_ART_HERO, e); }
int om_artpak_jp(const om_artpak *a, const char *key, uint8_t disc, om_art_entry *e) { return find_with(a, key, disc, OM_ART_JP, e); }

/* Requires that `sector` already passed om_art_check() for its entry: om_art_check (via blob_ok)
 * is what bounds a sector number (< 0x00200000) before any multiply by OM_SECTOR is done, so this
 * function performs no bounds check of its own and must not be called on an unchecked sector. */
uint32_t om_art_blob_pos(const om_artpak *a, uint32_t sector) { return a->data_off + sector * OM_SECTOR; }

uint32_t om_art_blob_len(const om_art_entry *e, int kind)
{
    if (kind == 0) return (uint32_t)e->thumb_w * e->thumb_h * 2u;
    if (kind == 1) return (uint32_t)e->hero_w * e->hero_h * 2u;
    return (uint32_t)(e->jp_w / 2u) * e->jp_h;
}

uint32_t om_art_staged_len(const om_art_entry *e)
{
    return ((e->flags & OM_ART_THUMB) ? om_art_blob_len(e, 0) : 0) + ((e->flags & OM_ART_HERO) ? om_art_blob_len(e, 1) : 0)
         + ((e->flags & OM_ART_JP) ? om_art_blob_len(e, 2) : 0);
}

int om_art_staged_crc_ok(const om_art_entry *e, const uint8_t *staged)
{
    return om_crc32(0, staged, om_art_staged_len(e)) == e->blob_crc;
}

static int blob_ok(const om_artpak *a, uint32_t sec, uint32_t len)
{
    return sec < 0x00200000u && om_in_range(sec * OM_SECTOR, len, a->data_size);
}

int om_art_check(const om_artpak *a, const om_art_entry *e)
{
    if (e->flags & OM_ART_THUMB) {
        if (e->thumb_w % 8 || e->thumb_w < 8 || !e->thumb_h) return OM_E_FIELD;
        if (!blob_ok(a, e->thumb_sec, om_art_blob_len(e, 0))) return OM_E_RANGE;
    }
    if (e->flags & OM_ART_HERO) {
        if (!((e->hero_w == 160 && e->hero_h == 160) || (e->hero_w == 128 && e->hero_h == 200))) return OM_E_FIELD;
        if (!blob_ok(a, e->hero_sec, om_art_blob_len(e, 1))) return OM_E_RANGE;
    }
    if (e->flags & OM_ART_JP) {
        if (e->jp_w % 8 || e->jp_w < 8 || e->jp_w > OM_JP_CANVAS_W || !e->jp_h || e->jp_h > OM_JP_CANVAS_H) return OM_E_FIELD;
        if (!blob_ok(a, e->jp_sec, om_art_blob_len(e, 2))) return OM_E_RANGE;
    }
    return OM_OK;
}

void om_folder_key(const char *path, char out[10])
{
    static const char hx[] = "0123456789ABCDEF";
    uint32_t h = 0x811C9DC5u;          /* FNV-1a-32, spec §5.2 Folder art */
    int i;
    while (*path) {
        h ^= (uint8_t)*path++;
        h *= 0x01000193u;
    }
    out[0] = 'F';
    for (i = 0; i < 8; i++)
        out[1 + i] = hx[(h >> (28 - 4 * i)) & 15];
    out[9] = 0;
}
