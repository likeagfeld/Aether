/* menu/launcher/lgfx.h -- LAUNCHER.BIN's screens in the look of the menu's loading screen (menu/views/loading.c): the
 * animated SEGA-eye background (LOADBG.BIN, SPEC §5.11) on NBG1, and the text, the cover and the wordmark on NBG0.
 * Bare VDP2, no SGL, no VDP1 (launch step 5 puts VDP1 back as the BIOS left it):
 *  - NBG1: 512x256 8 bpp bitmap, double-buffered in VRAM B0 / B1 (HWF-28, HWF-49), palette 1 = CRAM 256-511 (HWF-30);
 *    a decoded frame (HWRAM) goes to the hidden bitmap by SCU DMA level 0, indirect (HWF-50), and the bitmap base flips
 *    in a V-blank;
 *  - NBG0: 512x256 RGB555 bitmap in A0 + A1 (bit 15 = opaque, 0 = transparent), drawn by the CPU. The text's
 *    anti-aliasing is pre-blended against the background's average under its box, as the menu does.
 * This code and its data live in High Work RAM (.lui, launcher.ld): the editors own it, a launch only until step 9
 * (CWX zeroes it, the BIOS boot loads over it). om_lg_on says whether the screens are up; om_lg_stop ends them, leaving
 * the last frame on screen. Nothing here may run after om_lg_stop. BSD-3-Clause. */
#ifndef OM_LGFX_H
#define OM_LGFX_H
#include <stdint.h>
#include "handoff.h"

/* High Work RAM while the screens run (all above the BIOS work area, the IGR guard/stub/staging and the cheat block) */
#define OM_LUI          0x06010000u     /* .lui: this code, the fonts */
#define OM_LUI_END      0x06040000u
#define OM_LG_FILE      0x06040000u     /* LOADBG.BIN as read */
#define OM_LG_FILE_MAX  0x00080000u
#define OM_LG_PIX       0x060C0000u     /* the frame on screen, 320 x 224 */
#define OM_LG_COVER     0x060D2000u     /* the cover, up to 160 x 200 RGB555 */
#define OM_LG_TBL       0x060DF000u     /* the frame's DMA row table (4,096-byte boundary, TechBulletin SCU No. 25) */
#define OM_LG_RAST      0x060E0000u     /* text raster scratch, 4 bpp */
#define OM_LG_RAST_MAX  0x2000u
#define OM_LG_FREE      0x060E2000u     /* .. OM_LG_FREE_END: the save manager's tables */
#define OM_LG_FREE_END  0x06100000u

/* colors (RGB555 | 0x8000): the theme's (from the hand-off) and the loading screen's derived ones */
typedef struct { uint16_t txt, acc, acc2, dim, faint, err, gold, ok; } om_lg_sty;
extern om_lg_sty om_lg_c;
extern volatile uint8_t om_lg_on;       /* LWRAM: the screens are up (readable after HWRAM is gone) */
/* LWRAM (lstart.c): .lui is still in High Work RAM. Cleared when a launch reaches step 9 (CWX zeroes HWRAM, the BIOS
 * boot loads over it): from then on NOTHING in .lui may be called -- not even a function that would return at once */
extern volatile int om_lui_ok;
extern volatile uint8_t om_lg_used;     /* they were: VDP2 is theirs, a text screen needs om_txt_init first */
extern volatile uint8_t om_lg_disp;     /* LWRAM: the NBG1 bitmap shown, 0 = B0, 1 = B1 */

/* text faces (menu/src/uifont.h) */
enum { OM_LGF_ITEM, OM_LGF_SEL, OM_LGF_SMALL, OM_LGF_TITLE, OM_LGF_BRAND, OM_LGF_LABEL, OM_LGF_HERO };
#define OM_LG_RIGHT  1                  /* om_lg_text: right-aligned at x */
#define OM_LG_FADE   2                  /* cut at maxw with a fade (never an ellipsis) */
#define OM_LG_SOLID  4                  /* the text's box written whole (empty pixels transparent): no clear first */

/* Start the screens. adopt: a launch -- the menu's last eye frame is still up (its loading screen hands off without a
 * fade), found by its CRC and continued; else the keyframe, shown once read. API mode must be on. Returns 0 (the
 * screens are up, with or without the background: a missing LOADBG.BIN gives a plain dark one). */
int om_lg_start(const om_handoff *h, int adopt);
/* The animation and the file's reading: call often (every V-blank at least). may_read: an SD read is allowed now
 * (no other file is being written). Returns quickly. */
void om_lg_pump(int may_read);
/* The launch's pump (hardware 2026-11-27): called between two whole Satiator calls -- its yield hook (yield.h), the
 * swap's write gaps, its steps -- however far apart; reads while the eye is short of frames, within its share of the
 * card's time (lgread.h OM_LGR_SHARE). That includes the gap between two chunks of a file the swap writes (each
 * chunk already written and synced, sdwrite.h): a read of LOADBG.BIN's own handle there, before the V-blank rest. Never
 * inside a directory listing (yield.h om_yield_draw: om_lg_pump(0) there) */
void om_lg_pace(void);
void om_lg_wait(uint32_t vblanks);      /* V-blanks, pumping (reads allowed) */
void om_lg_quiet(void);                 /* no more reading (a file is about to be written); playback goes on
                                         * with what is in */
void om_lg_stop(void);                  /* DMA idle, no more decoding; the last frame stays up */

/* drawing on NBG0 (screen coordinates 320 x 224) */
void om_lg_clear(int x0, int y0, int x1, int y1);              /* transparent; x1, y1 inclusive */
void om_lg_fill(int x0, int y0, int x1, int y1, uint16_t c);
void om_lg_hbar(int x0, int y0, int x1, int y1, uint16_t c0, uint16_t c1);   /* horizontal gradient */
uint16_t om_lg_under(int x0, int y0, int x1, int y1);          /* the background's average there */
/* text with its baseline at `base`; bg: the color its edges blend into (0: the background under it) */
int om_lg_text(int face, int x, int base, int maxw, const char *s, uint16_t fg, uint16_t bg, int flags);
int om_lg_width(int face, const char *s);
/* a name wider than boxw scrolls (off px, growing with time); one that fits is drawn as it is */
int om_lg_marquee(int face, int x, int base, int boxw, const char *s, uint16_t fg, uint16_t bg, int32_t off);
/* a highlight bar (gradient c0 -> c1, a 2 px edge) with a (scrolled) name on it, every pixel written once */
void om_lg_bartext(int x0, int y0, int x1, int y1, uint16_t c0, uint16_t c1, uint16_t edge, int face, int tx, int base,
                   int boxw, const char *s, uint16_t fg, int32_t off);
int om_lg_tracked(int x, int base, const char *s, uint16_t fg);   /* the label face, letter-spaced as "NOW LOADING" */
void om_lg_mono(int x, int y, const char *s, uint16_t fg, uint16_t bg);   /* 8x8 font; bg 0 = transparent */
/* the game's cover on the right (or the NO ART slab); compact: smaller and higher, for the editors' hint lines */
void om_lg_cover(int dim, int compact);
void om_lg_brand(void);                 /* the footer's logo and "Aether" */
uint16_t om_lg_mix(uint16_t a, uint16_t b, uint32_t t256);
#endif
