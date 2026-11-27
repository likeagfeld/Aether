/* menu/launcher/lorb.h -- the menu's orbiting Saturn logo (menu/views/interstitial.c: the big logo, its faint orbit and
 * a comet going round once every 64 V-blanks) drawn by the CPU on lgfx's NBG0 overlay, over the eye frame on NBG1, and
 * the text of the pages shown with it. lorb.c is LWRAM code: it also draws the page of a CWX launch that fails at or
 * after step 9, when .lui is gone, from what om_orbp_stash left in VRAM. lorbpage.c (.lui) decodes the logo, makes the
 * stash and lays out the import's pages. BSD-3-Clause. */
#ifndef OM_LORB_H
#define OM_LORB_H
#include <stdint.h>
#include "uifont.h"

/* NBG0 is 512 x 256 RGB555 (lgfx.h); only x < 320, y < 224 is on screen. The rest holds the logo and the stash. */
#define OM_ORB_LOGO_X   336             /* the 152 x 101 logo, blended, in the hidden columns */
#define OM_ORB_STASH    0x38000u        /* rows 224-255 (32 KB): the half-size logo, three faces, a CRC */

#define OM_ORB_DIM      64              /* the eye frame behind is dimmed by this (VDP2 colour offset A, 8-bit units) */

enum { OM_ORB_FULL, OM_ORB_COMPACT };   /* the menu's size (centre 160, 96), or a header (half the logo, centre 38) */
enum { OM_ORB_LABEL, OM_ORB_ITEM, OM_ORB_SMALL, OM_ORB_NFACE };

/* text flags: centred in its box, right-aligned to its box, letter-spaced (the label face, as "NOW LOADING"), and
 * solid: the box's rows written whole in one pass (a line drawn again never blinks; its box must not overlap others) */
#define OM_ORB_CENTER 1
#define OM_ORB_RIGHT  2
#define OM_ORB_TRACK  4
#define OM_ORB_SOLID  8

/* NBG1 dimmed, NBG0 cleared, then the orbit and the logo (its blended copy: the hidden columns, or the stash for compact) */
void om_orb_scene(int compact, uint16_t bg);
void om_orb_tick(void);                 /* the comet moved on by the time since the scene began (lgap.h's clock) */
/* the text in the box x0..x1, left, centred or right in it, faded at the edge when too wide */
void om_orb_text(const om_face *f, int x0, int x1, int base, const char *s, uint16_t fg, int flags);
void om_orb_mono(int x, int y, const char *s, uint16_t fg);   /* 8x8 font, clipped at the right edge */
void om_orb_clear(int x0, int y0, int x1, int y1);

/* step 9: 1 = a CWX launch's stash was made (lseq.c clears it at every launch's start) */
extern volatile uint8_t om_orb_armed;
int om_orb_stash_ok(void);              /* the stash's magic and CRC */
uint16_t om_orb_stash_bg(void);         /* the background the stash's logo was blended over */
const om_face *om_orb_face(int i);      /* a face in the stash (OM_ORB_LABEL ..) */
#endif
