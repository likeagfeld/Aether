/* menu/views/ovdraw.h -- draws the overlays (om_overlay: Settings, Filter, a game's Launch Options) as one centered
 * panel (layout.h OVERLAY) over the dimmed menu. BSD-3-Clause. */
#ifndef OM_OVDRAW_H
#define OM_OVDRAW_H
#include "overlay.h"
#include "uitext.h"
#include "uiview.h"
void om_ovdraw_style(const om_style *s);
void om_ovdraw_alloc(void (*alloc)(om_ut *t, int cap, int h));
void om_ovdraw_prewarm(void);                  /* rasterise the fixed strings once (review-13a round 2 b) */
/* the overlays' hint lines name the mapped buttons: rasterized at boot (prewarm, default map), after
 * SETTINGS.INI is read, and whenever the map changes (OM_OV_PAD) -- never per frame */
void om_ovdraw_hints(const om_remap *m);
/* what the per-game panel shows; folders.c owns the data */
typedef struct {
    const om_pergame *g;                       /* the panel's game (edited in place by om_overlay_key_game) */
    const char *loader;                        /* its Loader value (loader.h om_loader_panel) */
    const char *pad_note;                      /* GAMES.DB's compat note (gamesdb.h om_gdb_compat), or
                                                 * NULL -- the panel's static OM_OGR_PAD row then falls back to
                                                 * "Default" (k_pg3[0], already in the UI pool: no new rodata) */
    uint8_t igr_def;                           /* om_igr_want's result for a Default In Game Reset (1 = on) */
    uint8_t cheat_jhl;                         /* the loader resolves to JHL: "Needs CWX loader" */
    uint8_t ch_ok, ch_on, ch_n;                /* 2026-11-25: the game's CHEATS.PAK record, counted (cheats.h
                                                 * om_cheats_count): ch_ok 1 = read (ch_n 0: no cheats), 0 = unknown */
} om_ovgame;
/* The game's name after "Launch Options", rasterized once when the panel opens (cut with a fade at the panel's edge) */
void om_ovdraw_game_title(const char *title);
/* kind: what to draw (the overlay may be sliding out after it closed); dx: the slide's x offset (0 = in place); vis:
 * 0..256, its fade (text LUTs); marquee: values wider than their box scroll (else they fade-clip); pg: the game panel
 * data (may be NULL for the other overlays). */
void om_ovdraw(const om_overlay *o, uint8_t kind, const om_settings *s, const char *theme_name, const char *genre_name,
               const om_ovgame *pg, int dx, uint16_t vis, uint32_t frame, uint32_t fps_x100, int marquee);
#endif
