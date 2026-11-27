/* menu/views/interstitial.h -- the start-up interstitial between the boot animation and the menu (Task 13d; Rulings
 * M3-R40, M3-R41, M3-R42; build/boot2menu_mockup_strip_v5.png, boot2menu_mockup_render.py, native 320x224):
 *  - the theme background (NBG1) and a large VDP1 Saturn logo (152x101, BIGLOGO.BIN from the menu disc in LWRAM)
 *    fade in from the animation's black (250 ms; anim= is not known yet, so always full);
 *  - if the start-up is still running ~0.5 s later, "LOADING YOUR LIBRARY" (8 px SemiBold, tracked), the live counts, a
 *    progress bar driven by the six boot steps (menu/app/boottime.h) and a comet on a faint orbit (rx 104, ry 30) that
 *    passes behind the sphere fade in; once shown they stay at least 0.4 s;
 *  - in the menu's first frames: the logo glides and shrinks into the header (300 ms ease-out), the wordmark fades in
 *    once the logo has cleared it, then the panels, then the list and the rest (no overlap). anim= is honored from
 *    the glide on (off = instant cut).
 * Presses during the interstitial are buffered and handed to the menu. Everything drawn is VDP1 (pre-blended against
 * NBG1, decision D1) plus the color offset and NBG2's color-calculation ratio. Textures live in the boot-only range
 * of VDP1 VRAM (OM_VRAM_BOOTANIM, free once the animation has ended; Task 13c borrows it later, at a launch).
 * BSD-3-Clause. */
#ifndef OM_INTERSTITIAL_H
#define OM_INTERSTITIAL_H
#include <stdint.h>
#include "sgl.h"

/* before om_istl_begin, the big logo's source: BIGLOGO.BIN (biglogo.h) as read at boot into LWRAM (file, len),
 * or read_rc != 0 when that read failed (file is then ignored). */
void om_istl_set_logo(const uint8_t *file, uint32_t len, int read_rc);
/* The logo's table for another user (the screensaver): the read rc, or the file re-opened, its CRC checked again (a
 * launch body load could in principle reach OM_LW_BIGLOGO; docs/ledger.md M6 C2). 0 and *tbl / *tlen, or the error. */
int om_istl_logo(const uint8_t **tbl, uint32_t *tlen);
/* After om_bootplay: decodes the logo (scratch: the zeroed card-scan work area, zeroed again), starts the boot timing
 * (boottime.h) with this module as its hook, and shows the first frame. Returns 0, or the logo's read/open/codec error
 * (the logo then stays blank; the start-up goes on). */
int om_istl_begin(uint8_t *scratch, uint32_t len);
void om_istl_counts(uint32_t games, uint32_t folders, uint32_t covers);
void om_istl_status(void);                   /* a frame, if a V-blank has passed since the last (blocking work) */
void om_istl_frame(void);                    /* a frame (one slSynch) */
/* After om_istl_begin, before the first SD step: frames until the logo and the background are fully up (FADE_IN,
 * 250 ms; at least two slSynch, HWF-16), so a long blocking step never holds them part-way. */
void om_istl_fade_in(void);                  /* ... and from its end to om_istl_ready, paces the loads (yield.h) */
#ifdef OMSAT_FAKECARD
void om_istl_gt_phase(int ph);               /* fake build: the boot-gap trace's phase (0x30 = after the card load) */
#endif
void om_istl_slow(uint32_t vblanks);         /* fake build (FAKESCRIPT.SLOWBOOT): hold each of steps 1-5 this long */
void om_istl_abort(void);                    /* before a fatal screen: offsets off, nothing more drawn */
/* Before the menu loop, with anim= known: lets the fade-in (and a shown loading UI's minimum time) finish. */
void om_istl_ready(uint8_t anim);
/* run once a frame while om_istl_ready waits (the art cache's prefetch); 0 = none. */
void om_istl_set_idle(void (*fn)(void));
/* The menu loop, once a frame before it draws: advances the glide and the fades, drives NBG2's ratio. */
void om_istl_menu_tick(void);
int om_istl_draw_logo(void);                 /* 1 = it drew the gliding logo (the header must not draw its own) */
enum { OM_ISTL_WORDMARK, OM_ISTL_PANELS, OM_ISTL_CONTENT };
uint16_t om_istl_in(int part);               /* 0..256; 256 once the interstitial is over */
/* The menu loop's presses: live = om_pad_poll()'s. Until the fade-in is over, presses (buffered at boot, then live) are
 * queued and handed on one a frame; one with A (launch) waits until the list is fully visible. */
Uint16 om_istl_take_press(Uint16 live);
/* the pad bit that launches (the mapped Play button; default PER_DGT_TA) is the one held back */
void om_istl_set_ok(Uint16 mask);
int om_istl_active(void);                    /* the interstitial still owns part of the screen (stages 0-1) */
int om_istl_menu_visible(void);              /* the menu's content has begun to fade in (or there was no interstitial):
                                              * the sound task's music start keys on this */
/* The V-blank handler the interstitial installs: om_v2_vblank by default, or a chain that calls it (the sound task's
 * om_audio_vblank), so the interstitial never replaces the audio handler. Call before om_istl_begin. */
void om_istl_set_vblank_chain(void (*fn)(void));
/* Before show_theme in step 3: changes = the theme is not the built-in Saturn gradient already shown. The background
 * dips to black alone (~8 V-blanks) and comes back when step 3 ends, with the theme's BG.BIN if it has one. */
void om_istl_bg_dip(int changes);
/* after show_theme, if om_istl_bg_dip dipped: the big logo is decoded again (scratch: 15,352 B, handed back
 * zeroed) and its edges re-blended with the new background. Otherwise nothing. */
void om_istl_rebind(uint8_t *scratch, uint32_t len);
#endif
