/* menu/views/loading.h -- the loading screen (M3 Task 13c, the approved mockup build/loading_mockup_x3.png and motion
 * preview v2 from 15 s): the animated SEGA-eye background on NBG1 (LOADBG.BIN, menu/src/loadbg.c), the tilted cover
 * with its drop shadow and reflection, "NOW LOADING", the title and meta lines, the three-step checklist whose dots
 * fill on REAL step completion, the progress bar, the cancel hint and the footer wordmark. Every position comes from
 * the layout table (menu/src/layout.h, the LD_ boxes). BSD-3-Clause. */
#ifndef OM_LOADING_H
#define OM_LOADING_H
#include <stdint.h>
#include "launch.h"
#include "uitext.h"
#include "uiview.h"

enum { OM_LOAD_HANDOFF, OM_LOAD_CANCEL, OM_LOAD_FAILED };

typedef struct {
    char title[160], meta1[200], meta2[200];
    int art;                            /* 1: the selected game's cover is the one on screen (not still loading) */
    int square;                         /* the cover's shape (art: from ART.PAK; no art: the region, as the menu) */
    const char *path;                   /* the image, relative to the card root (LIBRARY.IDX) */
    uint8_t area;
    uint32_t flags;                     /* hand-off flags (handoff.h): OM_HO_NO_IGR mirrors opts.igr */
    om_launch_opts opts;                /* the resolved loader and the per-game options (folders.c) */
    const char *label;                  /* NULL: "NOW LOADING". A hand-off that boots no game (OM_HO_CHEATS, OM_HO_SAVEMGR,
                                         * OM_HO_IMPORT) names its tool here: the same eye, cover and title, without the
                                         * checklist and the scream, handed off as soon as LAUNCHER.BIN is in (the tool
                                         * continues the eye's last frame) */
} om_load_game;

typedef struct {
    const om_style *sty;                /* the theme colors the menu uses */
    uint8_t anim;                       /* anim= (fades, slides) */
    uint32_t fps;                       /* 5994 or 5000 */
    /* at the commit point -- LAUNCHER.BIN loaded and checked, the scream over, just before the final fade, after which
     * there is no cancel -- the menu writes RECENT.TXT (spec §7.1/§7.6). A non-empty msg is shown
     * for a second, as before; the launch goes on. A canceled launch never enters Recently Played. */
    void (*loaded)(void *ctx, char *msg, uint32_t cap);
    void *ctx;
} om_load_env;

typedef struct {
    int result;                         /* OM_LOAD_* */
    int rc;                             /* the failure's rc */
    char msg[64];                       /* the failure, named, with its rc (om_msg_rc) */
    om_launch_info li;                  /* li.clobbered: GAMES.DB / ART.PAK must be reloaded if the menu comes back */
    uint32_t frames, misses, bg_frames, bg_wraps, bg_held;   /* diagnostics (report, fake build) */
    uint32_t draw_us, frame_us;         /* worst frame: decode + upload + drawing, and with the SD work after it */
    uint32_t crc_misses;                /* fake build: V-blanks lost to its one CRC-32 check of the loop's wrap */
    int bg_rc;                          /* LOADBG.BIN's rc (0 = it played) */
    uint32_t bg_late, pre_vbl;          /* V-blanks the background ran late; V-blanks the preload took (0: not done) */
} om_load_out;

/* The UI slots, from the menu's pool (menu/views/folders.c alloc_slots). */
void om_loading_alloc(void (*alloc)(om_ut *t, int cap, int h));
/* Starts reading /aether/LOADBG.BIN into arena (the card-scan work area, free once the card has loaded) and returns
 * at once; om_loading_preload_tick reads on until until_us after the frame began (the menu's fade to black calls it
 * every frame after drawing, so the read is hidden); the loading screen finishes it if the fade did not. */
void om_loading_preload_begin(uint8_t *arena, uint32_t cap);
void om_loading_preload_tick(uint16_t t0, int32_t until_us);   /* reads until until_us after t0 (om_ut_frc) */
/* the static per-game panel's read-ahead: whole steps until nvbl V-blanks after the iteration began
 * at t0 (om_ut_frc) and V-blank vb0 (om_v2_vblanks) */
void om_loading_preload_tick_vb(uint16_t t0, uint32_t vb0, uint32_t nvbl);
/* user directive 2026-11-20: the per-game panel's time on screen reads LOADBG.BIN ahead of a launch, so the
 * background plays at full speed from its first frame. Early mode reads only the file's HWRAM part (the arena): no
 * sound RAM, so the music plays on. _early starts it, or resumes a paused early read in the same arena; _pause closes
 * the file (the two-files rule, before a synchronous SD write and while the art job runs) and keeps the bytes;
 * om_loading_preload_begin at the launch adopts a paused early read in the same arena instead of starting over;
 * _drop forgets it (the arena is about to be used by something else: the diagnostics screen). */
void om_loading_preload_early(uint8_t *arena, uint32_t cap);
void om_loading_preload_pause(void);
void om_loading_preload_drop(void);
uint32_t om_loading_preload_have(void);                        /* bytes read so far (diagnostics) */
/* an early read also fills 92 KB of OM_LW_STAGE (loadbg.h's LW region), checked by a hash when it is
 * resumed or adopted. _lw_held: it holds LW bytes (the art prefetch waits); _lw_drop: give them up (the stage is
 * needed: the selection moved). */
int om_loading_preload_lw_held(void);
void om_loading_preload_lw_drop(void);
/* The screen, from black (the menu has faded out) to black (fade-out done): returns when the game is about to be handed
 * off (OM_LOAD_HANDOFF: the hand-off block is written, LAUNCHER.BIN is loaded and checked, RECENT.TXT written), when
 * A+B+C+Start was held (OM_LOAD_CANCEL), or after a failure was shown and a button pressed (OM_LOAD_FAILED). The VDP2
 * state it changed (NBG1 bitmap, zoom and palette, NBG2 rectangles) is the caller's to restore. */
void om_loading_run(const om_load_game *g, const om_launch_bufs *lb, const om_load_env *env, om_load_out *out);
#endif
