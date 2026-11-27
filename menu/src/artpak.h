/* menu/src/artpak.h -- ART.PAK reader (contract/SPEC.md §5.2). BSD-3-Clause. */
#ifndef ARTPAK_H
#define ARTPAK_H
#include <stdint.h>
#include "om_fmt.h"

#define OM_ART_THUMB  1u
#define OM_ART_HERO   2u
#define OM_ART_JP     4u
#define OM_ART_SQUARE 8u

typedef struct {
    char key[17];
    uint8_t disc, flags, thumb_w, thumb_h, hero_w, hero_h;
    uint16_t jp_w;
    uint8_t jp_h;
    uint32_t thumb_sec, hero_sec, jp_sec, manifest_row, blob_crc;
} om_art_entry;

typedef struct { const uint8_t *table; uint32_t count, data_off, data_size; } om_artpak;

int om_artpak_open(om_artpak *a, const uint8_t *buf, uint32_t buf_len, uint32_t file_size);
void om_artpak_entry(const om_artpak *a, uint32_t i, om_art_entry *e);
int om_artpak_find(const om_artpak *a, const char *key, uint8_t disc, om_art_entry *e);
int om_artpak_cover(const om_artpak *a, const char *key, uint8_t disc, om_art_entry *e);
int om_artpak_jp(const om_artpak *a, const char *key, uint8_t disc, om_art_entry *e);
int om_art_check(const om_artpak *a, const om_art_entry *e);
uint32_t om_art_blob_pos(const om_artpak *a, uint32_t sector);
uint32_t om_art_blob_len(const om_art_entry *e, int kind);
uint32_t om_art_staged_len(const om_art_entry *e);
int om_art_staged_crc_ok(const om_art_entry *e, const uint8_t *staged);
void om_folder_key(const char *path, char out[10]);
#endif
