/* menu/ui/art.h -- VDP1 art panel: hero cover / folder art / placeholder and the JP-title bitmap. BSD-3-Clause. */
#ifndef OM_ART_H
#define OM_ART_H
#include <stdint.h>
#include "browse.h"
#include "card.h"
#include "tree.h"
int om_art_init(void);
void om_art_tick(const om_card *c, const om_tree *t, const om_item *sel);
/* the view draws the art (menu/views/folders.c). The shown hero's VRAM address and texture size; tall = a
 * 128x200 cover (shown 100x156), otherwise 160x160 (shown 100x100: square art, folder art). Returns 0 when there is no
 * art (the view draws its NO ART card). */
int om_art_hero(uint32_t *tex, int *w, int *h, int *tall);
int om_art_jp(uint32_t *tex, int *w, int *h);           /* the JP-title bitmap (4 bpp LUT at OM_VRAM_LUT), 0 if none */
void om_art_jp_lut(uint16_t fg, uint16_t bg);           /* RGB555 text colour and the backdrop under it */
int om_art_loading(void);                               /* a selection's art is not on screen yet (shimmer) */
uint32_t om_art_serial(void);                           /* changes whenever the shown image changes (fade-in) */
const char *om_art_message(void);
/* coordinator fix round 1, Task 15, point 2: cancel any in-flight art job and force a fresh
 * re-request of the current selection. Call this when something else may have overwritten
 * OM_LW_STAGE underneath the job (e.g. a failed LAUNCHER.BIN prepare). */
void om_art_invalidate(void);
/* a launch: the job and an unfinished upload are dropped, and the decode canvas (53,760 B, 4-aligned) is the caller's
 * scratch until the next om_art_tick; the cache's finished slots stay */
uint8_t *om_art_lend(void);
#define OM_ART_LEND_BYTES 53760u
/* Ruling M3-R52, the cover cache (art.c's header): */
int om_art_cache_claim(void);          /* after the UI pool is laid out: C2 joins if the pool's top is free (1) */
int om_art_cache_slots(void);          /* VRAM cover slots in use by the cache (4 or 5) */
uint32_t om_art_cache_hits(void);      /* selections shown straight from the cache */
void om_art_theme_changed(void);       /* the tiles follow the theme: re-drawn */
/* Idle frames (the cursor at rest, nothing loading): prefetch cur, +1, -1, +2, -2 of the view's items. */
void om_art_prefetch(const om_card *c, const om_tree *t, const om_item *items, uint16_t n, uint16_t cur);
#endif
