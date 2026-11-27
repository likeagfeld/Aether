/* menu/src/vrtile.h -- the virtual root folders' built-in art tiles (/aether/VRTILES.BIN, committed as
 * menu/app/assets/VRTILES.BIN, tools/omsat/gen_vrtiles.py; final M3 fix round; a card file read into LWRAM, no
 * longer HWRAM .rodata). 160 x 160, the square hero size; palette entries are theme roles
 * (role << 8 | level), mapped here to RGB555 as mix(base, role color, level). Pure C (host-tested). BSD-3-Clause. */
#ifndef OM_VRTILE_H
#define OM_VRTILE_H
#include <stdint.h>
#define OM_VRT_W 160u
#define OM_VRT_H 160u
#define OM_VRT_ROLES 5            /* 0 base, 1 accent, 2 highlight, 3 gold, 4 white */
/* VRTILES.BIN (SPEC §5.18) in file[0, len): checks the common header (magic OMSATVRT, version, CRC-32 of the table,
 * the table inside the file) and that its entry count is the table's tile count; *tiles / *tiles_len = the table, the
 * blob om_vrtile_decode reads. 0 or an OM_E_* code (*tiles untouched then). */
int om_vrtile_open(const uint8_t *file, uint32_t len, const uint8_t **tiles, uint32_t *tiles_len);
/* Tile i of blob into out (OM_VRT_W * OM_VRT_H RGB555 words, bit 15 set, 4-aligned): the indices are decoded into the
 * upper half of out and expanded in place, so no other buffer is used. roles[] are RGB555. 0 or an OM_E_* code. */
int om_vrtile_decode(const uint8_t *blob, uint32_t len, unsigned i, const uint16_t roles[OM_VRT_ROLES], uint16_t *out);
#endif
