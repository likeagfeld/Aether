/* menu/host-tests/test_vrtile.c -- the virtual root folders' art tiles (final M3 fix round; M6 C3 moved them to the
 * card file /aether/VRTILES.BIN, read into LWRAM): the committed menu/app/assets/VRTILES.BIN opens through its
 * common header (SPEC §5.18) and decodes in place, through the theme roles, for all five roots. BSD-3-Clause. */
#include "th.h"
#include "om_fmt.h"
#include "omlayout.h"
#include "vrtile.h"

#define BASE   0x1084u
#define ACC    0x7A07u
#define HI     0x7F53u
#define GOLD   0x231Eu
#define WHITE  0x7BDEu

int main(void)
{
    uint32_t flen, len, i, k;
    uint8_t *file = th_load("../app/assets/VRTILES.BIN", &flen), *blob;
    const uint8_t *tiles = NULL;
    static uint16_t out[OM_VRT_W * OM_VRT_H] __attribute__((aligned(4)));
    const uint16_t roles[OM_VRT_ROLES] = { BASE, ACC, HI, GOLD, WHITE };
    CHECK(flen + 4u <= OM_LW_VRTILES_MAX);                             /* fits its LWRAM region (sd.c read slack) */
    CHECK_INT(om_vrtile_open(file, flen, &tiles, &len), OM_OK);
    CHECK(tiles == file + 64 && len == flen - 64 && len % 4 == 0);
    {   /* the header is checked: magic, CRC, a table past the end, a tile count that disagrees with the table */
        const uint8_t *t2; uint32_t l2;
        file[3] ^= 1;                                                  /* magic */
        CHECK_INT(om_vrtile_open(file, flen, &t2, &l2), OM_E_MAGIC);
        file[3] ^= 1;
        file[64 + 100] ^= 1;                                           /* a tile byte: the CRC */
        CHECK_INT(om_vrtile_open(file, flen, &t2, &l2), OM_E_CRC);
        file[64 + 100] ^= 1;
        CHECK(om_vrtile_open(file, flen - 4, &t2, &l2) < 0);           /* truncated file */
        CHECK(om_vrtile_open(file, 40, &t2, &l2) < 0);                 /* shorter than the header */
        file[0x0F] ^= 1;                                               /* entry count 5 -> 4 */
        CHECK_INT(om_vrtile_open(file, flen, &t2, &l2), OM_E_FIELD);
        file[0x0F] ^= 1;
        CHECK_INT(om_vrtile_open(file, flen, &t2, &l2), OM_OK);
    }
    blob = (uint8_t *)tiles;
    for (i = 0; i < 5; i++) {
        uint32_t nb = 0, set = 1;
        memset(out, 0xEE, sizeof out);
        CHECK_INT(om_vrtile_decode(blob, len, i, roles, out), OM_OK);
        for (k = 0; k < OM_VRT_W * OM_VRT_H; k++) {
            set &= (out[k] >> 15) & 1u;
            nb += (out[k] & 0x7FFFu) != BASE;
        }
        CHECK(set);                                                    /* every pixel opaque RGB (HWF-01) */
        CHECK(nb > 2000 && nb < OM_VRT_W * OM_VRT_H);                  /* a drawn motif on the base */
        CHECK_INT(out[0], BASE | 0x8000u);                             /* corner: the base */
    }
    CHECK_INT(om_vrtile_decode(blob, len, 0, roles, out), OM_OK);
    CHECK_INT(out[84 * OM_VRT_W + 70], GOLD | 0x8000u);                /* Favorites: the star is gold */
    CHECK_INT(om_vrtile_decode(blob, len, 4, roles, out), OM_OK);
    {   /* Region: the globe's rim, white at level 14/15 */
        uint16_t px = out[80 * OM_VRT_W + 32];
        CHECK((px & 31u) > 24u && ((px >> 5) & 31u) > 24u && ((px >> 10) & 31u) > 24u);
    }
    CHECK(om_vrtile_decode(blob, len, 5, roles, out) < 0);             /* no sixth tile */
    CHECK(om_vrtile_decode(blob, 40, 4, roles, out) < 0);              /* truncated */
    blob[1] ^= 1;
    CHECK(om_vrtile_decode(blob, len, 0, roles, out) < 0);             /* wrong width */
    free(file);
    return th_done("test_vrtile");
}
