#include "th.h"
#include "artpak.h"

static uint8_t *stage(const uint8_t *file, const om_artpak *a, const om_art_entry *e, uint32_t *len)
{
    uint8_t *s = (uint8_t *)malloc(om_art_staged_len(e) + 1);
    uint32_t o = 0;
    int k;
    const uint32_t sec[3] = { e->thumb_sec, e->hero_sec, e->jp_sec };
    const uint8_t bit[3] = { OM_ART_THUMB, OM_ART_HERO, OM_ART_JP };
    for (k = 0; k < 3; k++)
        if (e->flags & bit[k]) {
            memcpy(s + o, file + om_art_blob_pos(a, sec[k]), om_art_blob_len(e, k));
            o += om_art_blob_len(e, k);
        }
    *len = o;
    return s;
}

int main(void)
{
    uint32_t n, sl, i;
    uint8_t *f = th_load(FIX("tiny.art.pak"), &n), *m, *s;
    om_artpak a;
    om_art_entry e;
    char fk[10];
    CHECK_INT(om_artpak_open(&a, f, n, n), OM_OK);
    CHECK_INT(a.count, 5);
    CHECK_INT(a.data_off, 2048);
    CHECK_INT(om_artpak_find(&a, "GS-9001", 0, &e), 1);
    CHECK_INT(e.hero_w, 160); CHECK_INT(e.hero_h, 160); CHECK_INT(e.manifest_row, 1);
    CHECK_INT(om_artpak_find(&a, "GS-9001", 1, &e), 0);
    CHECK_INT(om_artpak_cover(&a, "GS-9001", 1, &e), 1);                /* (key,1) absent -> (key,0) */
    CHECK_INT(e.disc, 0);
    CHECK_INT(om_artpak_cover(&a, "T-4507G", 1, &e), 0);                /* disc-1 entry has no hero, no (key,0) */
    CHECK_INT(om_artpak_jp(&a, "T-4507G", 1, &e), 1);
    CHECK_INT(e.jp_w, 16); CHECK_INT(e.jp_h, 16);
    CHECK_INT(om_artpak_cover(&a, "T-8113H", 0, &e), 1);
    CHECK_INT(e.hero_w, 128); CHECK_INT(e.hero_h, 200); CHECK(!(e.flags & OM_ART_SQUARE));
    om_folder_key("RPG", fk);
    CHECK_STR(fk, "FD6B34428");                                          /* = omsat.artpak.folder_art_key("RPG") */
    om_folder_key("", fk);
    CHECK_STR(fk, "F811C9DC5");
    CHECK_INT(om_artpak_find(&a, "FD6B34428", 0, &e), 1);
    CHECK_INT(e.flags & OM_ART_HERO, OM_ART_HERO);
    for (i = 0; i < a.count; i++) {                                      /* every entry passes, every blob CRC verifies */
        om_artpak_entry(&a, i, &e);
        CHECK_INT(om_art_check(&a, &e), OM_OK);
        s = stage(f, &a, &e, &sl);
        CHECK_INT(sl, om_art_staged_len(&e));
        CHECK_INT(om_art_staged_crc_ok(&e, s), 1);
        s[sl / 2] ^= 0x40;                                               /* mutation: one blob byte */
        CHECK_INT(om_art_staged_crc_ok(&e, s), 0);
        free(s);
    }
    printf("test_artpak: blob CRC coverage %u of %u entries\n", (unsigned)i, (unsigned)a.count);
    m = th_dup(f, n); m[64 + 3] ^= 1;                                    /* table byte */
    CHECK_INT(om_artpak_open(&a, m, n, n), OM_E_CRC); free(m);
    m = th_dup(f, n);                                                    /* swap rows 0 and 1, fix CRC: unsorted */
    { uint8_t t[48]; memcpy(t, m + 64, 48); memcpy(m + 64, m + 112, 48); memcpy(m + 112, t, 48); }
    om_put32(m + 0x24, om_crc32(0, m + 64, 48 * 5));
    CHECK_INT(om_artpak_open(&a, m, n, n), OM_E_FIELD); free(m);
    CHECK_INT(om_artpak_open(&a, f, n, n), OM_OK);
    om_artpak_find(&a, "GS-9001", 0, &e);
    e.hero_w = 150;                                                      /* not a hero canvas */
    CHECK_INT(om_art_check(&a, &e), OM_E_FIELD);
    om_artpak_find(&a, "GS-9001", 0, &e);
    e.hero_sec = 1000;
    CHECK_INT(om_art_check(&a, &e), OM_E_RANGE);
    free(f);
    {   /* fix round 1 regression: count x 48 overflow (coordinator-reported). 48u * 89478486u wraps mod 2^32
         * to 32, matching this crafted 32-byte CRC-valid table -- `h.table_size != 48u * h.count` used to
         * pass, after which the sortedness loop (`a->table + 48 * i` for i up to 89,478,485) read far out
         * of bounds. The fixed guard (`table_size % 48u != 0 || table_size / 48u != h.count`) rejects this
         * before the loop ever runs, with no crash. */
        uint8_t ov[96] = { 0 };
        om_artpak a2;
        memcpy(ov, "OMSATART", 8);
        om_put16(ov + 0x08, 1);
        om_put16(ov + 0x0A, 64);
        om_put32(ov + 0x0C, 89478486u);  /* wraps: 48u * 89478486u == 32 (mod 2^32) */
        om_put32(ov + 0x10, 64);         /* table_off */
        om_put32(ov + 0x14, 32);         /* table_size: the wrap target, not a multiple of 48 */
        om_put32(ov + 0x18, 0);          /* data_off */
        om_put32(ov + 0x1C, 0);          /* data_size */
        om_put32(ov + 0x20, 20261117u);  /* build_date */
        om_put32(ov + 0x24, om_crc32(0, ov + 64, 32));
        CHECK_INT(om_artpak_open(&a2, ov, sizeof ov, sizeof ov), OM_E_TABLESIZE);
    }
    return th_done("test_artpak");
}
