/* menu/pop/pabout.h -- the About popup's model (Settings > About; popabi.h OM_POP_ABOUT): the credits of CREDITS.md
 * as one name line and its "project (link) | licence" line each, wrapped to the list's width at open, and the list's
 * scroll. Pure C (the width of a piece of text comes from the caller), shared by pdraw.c and test_pabout.c, which also
 * checks every credit against CREDITS.md. Its layout: the boxes below (screen pixels before the safe-area transform),
 * inside the popups' panel (pgeom.h PB_PANEL). BSD-3-Clause. */
#ifndef OM_PABOUT_H
#define OM_PABOUT_H
#include <stdint.h>

/* the header: the navy tile with the lockup (plogo.c, gen_aboutlogo.py), and the lines beside it */
#define OM_PA_HDR_Y1    86                  /* the header band: om_pd_box[PB_HDR]'s top and sides, down to here */
#define OM_PA_LOGO_W    88
#define OM_PA_LOGO_H    68
#define OM_PA_LOGO_X    16
#define OM_PA_LOGO_Y    16
#define OM_PA_TEXT_X    112                 /* the lines beside the tile, up to OM_PA_TEXT_X1 */
#define OM_PA_TEXT_X1   300
#define OM_PA_BASE_LABEL   29               /* baselines: "ABOUT", the creators, the link, the version */
#define OM_PA_BASE_BY      45
#define OM_PA_BASE_LINK    60
#define OM_PA_BASE_VER     79
/* the credits: a spaced heading, then the list */
#define OM_PA_HEAD_Y    90
#define OM_PA_LIST_X0   18
#define OM_PA_LIST_X1   299                 /* text right edge; the scroll thumb is right of it */
#define OM_PA_LIST_Y0   106
#define OM_PA_ROW_H     14                  /* the small face's full height (pgeom.h OM_PD_ROW_H) */
#define OM_PA_ROWS      6u
#define OM_PA_THUMB_X   303

#define OM_PA_BY        "Created by Farkus and Claude AI"
#define OM_PA_LINK      "https://github.com/likeagfeld/Aether/"
#define OM_PA_HEADING   "CREDITED FOR SOURCE CODE OR INSPIRATION"

extern const uint16_t om_pa_logo_lut[16];
extern const uint8_t om_pa_logo[OM_PA_LOGO_W * OM_PA_LOGO_H / 2];

/* CREDITS.md's table, row by row: who, then "project (link) | licence" */
typedef struct { const char *who, *what; } om_pa_credit;
extern const om_pa_credit om_pa_credits[];
extern const uint16_t om_pa_ncredits;

enum { OM_PA_NAME, OM_PA_WHAT };            /* a line's kind: the name (bold) or its project line */
typedef struct { const char *s; uint8_t len, kind; } om_pa_line;
#define OM_PA_LINES_MAX 96u
typedef struct {
    om_pa_line line[OM_PA_LINES_MAX];
    uint16_t n, top, rows;
} om_pa;

/* the width in pixels of s[0..len) drawn as a line of that kind */
typedef int (*om_pa_width)(int kind, const char *s, int len);
/* the lines of every credit wrapped to width px: at a space, else after a '/', '-' or '.' of a word too long for one
 * line, else where the line is full; the list at its top. rows: the lines shown at once */
void om_pa_open(om_pa *a, int width, uint16_t rows, om_pa_width wf);

/* keys (SGL PER_DGT_*, as psave.h's): Up/Down a line, Left/Right a page; REP with Up/Down: a held repeat */
#define OM_PA_K_R (1u << 15)
#define OM_PA_K_L (1u << 14)
#define OM_PA_K_D (1u << 13)
#define OM_PA_K_U (1u << 12)
#define OM_PA_K_B (1u << 8)
#define OM_PA_K_REP 1u
#define OM_PA_MOVED 1u                      /* the list scrolled */
#define OM_PA_CLOSE 2u                      /* B: back to Settings */
uint32_t om_pa_key(om_pa *a, uint16_t k);
uint16_t om_pa_max_top(const om_pa *a);     /* the last top: its last page full */
#endif
