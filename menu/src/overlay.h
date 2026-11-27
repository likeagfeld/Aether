/* menu/src/overlay.h -- the Start (settings) and Z (filter) overlays as a portable state machine; the view maps
 * PER_DGT_* to OM_K_* and draws it (menu/views/ovdraw.c). BSD-3-Clause. */
#ifndef OM_OVERLAY_H
#define OM_OVERLAY_H
#include <stdint.h>
#include "remap.h"
#include "pergame.h"
#include "settings.h"
enum { OM_K_UP = 1, OM_K_DOWN = 2, OM_K_LEFT = 4, OM_K_RIGHT = 8, OM_K_A = 16, OM_K_B = 32, OM_K_Z = 64, OM_K_START = 128,
       OM_K_REP = 256 };   /* REP: the Up/Down is a held auto-repeat, not a fresh press -- the lists wrap on a fresh
                          * press only, so a held D-pad stops at the ends */
enum { OM_OV_NONE, OM_OV_SETTINGS, OM_OV_FILTER, OM_OV_GAME };   /* GAME (2026-11-23: no PGLIST) */
enum { OM_OVR_THEME, OM_OVR_CLOCK, OM_OVR_MUSIC, OM_OVR_SFX, OM_OVR_ANIM, OM_OVR_SAFE, OM_OVR_LOADER,
       OM_OVR_IGR, OM_OVR_SAVES, OM_OVR_SAVEMGR, OM_OVR_RESELECT, OM_OVR_SAVER, OM_OVR_CONTROLS,
       OM_OVR_ABOUT, OM_OVR_COUNT };   /* SAVEMGR ("Manage / Import Saves": every game's saves, and the Kai/Rings
                                        * import as its popup's first row) sits after Saves; About stays last */
/* The per-game panel's rows (A on a game). Launch is row 0, so A on a game opens straight onto "start it now"; a
 * second A there returns OM_OV_LAUNCH. Pad is a static note. Manage saves and Cheats hand off to LAUNCHER.BIN on A.
 * RESET ("Reset to defaults") asks twice, then deletes the game's section. */
enum { OM_OGR_LAUNCH, OM_OGR_LOADER, OM_OGR_SAVES, OM_OGR_SLOT, OM_OGR_SAVEMGR, OM_OGR_CHEATS, OM_OGR_IGR, OM_OGR_PAD,
       OM_OGR_RESET, OM_OGR_COUNT };
enum { OM_OVP_MAIN, OM_OVP_CONTROLS };      /* the settings overlay's two pages */
/* The Controls page: every action but OM_ACT_INFO (user decision 2026-11-23: C does nothing on the game list, so its
 * row is gone; pad_info stays in the table, which is always a permutation, and holds the spare button), then
 * "Reset all". om_ovc_act maps a page row to its action. */
#define OM_OVC_RESET  (OM_ACT_COUNT - 1)    /* the Controls page's last row, "Reset all" */
#define OM_OVC_COUNT  OM_ACT_COUNT
static inline uint8_t om_ovc_act(uint16_t row) { return (uint8_t)(row < OM_ACT_INFO ? row : row + 1u); }
#define OM_OV_VIS     9     /* rows visible at once (OV_BAR, 14 px each): the whole game panel */
#define OM_OV_CLOSED  1
#define OM_OV_CHANGED 2
#define OM_OV_THEME   4     /* theme selection moved: apply it now */
#define OM_OV_SOUND   8     /* music on/off or volume, or the sfx switch moved: apply it now */
#define OM_OV_ANIM    16    /* anim= moved (full / reduced / off): apply it now */
#define OM_OV_PAD     32    /* the button map changed: rebuild hints and badges */
#define OM_OV_SAFE    64    /* overscan changed: apply now -- M3.5's bit */
#define OM_OV_SAVER   128   /* the saver timeout changed */
#define OM_OV_PGSAVE  512   /* the game panel changed: write PERGAME.INI on close */
#define OM_OV_SAVEMGR 4096  /* Manage saves was chosen: the caller hands off to the save manager (game panel: that game) */
#define OM_OV_ABOUT   8192  /* Settings > About was chosen (A or Right): the caller opens its popup (menu/pop/pabout.h) */
#define OM_OV_LAUNCH  16384 /* the per-game panel's Launch row was chosen (A): the caller starts the game now */
#define OM_OV_CHEATS  32768 /* A on the panel's Cheats row: the caller hands off to the cheat editor */
typedef struct { uint8_t kind, rcur, dirty, page, armed, defs; uint16_t row, top, count; int16_t genre; uint16_t ngenre,
                 theme, ntheme; } om_overlay;
/* 2026-11-25 (user: "shouldn't it just be on and off?"): the game panel's Saves and Cheats rows show On or Off -- with
 * no choice of the game's own, the global setting's value, dimmed -- and Left/Right (A on Saves) flips the shown value
 * into the game's own choice. OM_PG_DEFAULT stays the stored "no choice" (PERGAME.INI unchanged); only Reset to
 * defaults goes back to it. defs: the globals, set by the caller after om_overlay_open_game. */
#define OM_OVD_SAVES  1u    /* SETTINGS.INI saves=: on */
#define OM_OVD_CHEATS 2u    /* SETTINGS.INI cheats=: on */
void om_overlay_open(om_overlay *o, uint8_t kind, uint16_t theme, uint16_t ntheme, int16_t genre_rank, uint16_t ngenre);
int om_overlay_key(om_overlay *o, uint16_t keys, om_settings *s);
/* the game panel edits the om_pergame the caller holds, and only on an explicit Left / Right / A (never on
 * a cursor move: Kai's cursor bug). B or Start closes it: OM_OV_CLOSED, plus OM_OV_PGSAVE when anything changed.
 * Reset to defaults asks twice (rcur = armed; any move disarms); the second A clears g->id[0] (delete the section) and
 * closes. */
void om_overlay_open_game(om_overlay *o, om_pergame *g);
int om_overlay_key_game(om_overlay *o, uint16_t keys, om_pergame *g);
/* the held keys (OM_K_*) each frame the panel is open: A arms only once released (hardware report on 3c64908) */
void om_overlay_game_held(om_overlay *o, uint16_t held);
#endif
