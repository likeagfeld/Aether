/* menu/src/layout.h -- the ONE layout table (controller addendum 4): every screen region of the Folders view as a named
 * box at native 320x224, inclusive pixel coordinates, from the approved mockup's layout spec
 * (.superpowers/sdd/2026-11-18-m3-library-themes-music/mockup_render.py). Views place things only through this
 * table and the few spacing constants below it; menu/host-tests/test_layout.c checks, for each screen state, that every
 * child lies inside its parent minus the parent's padding, that siblings do not overlap unless declared layered, that
 * everything lies in the 8 px overscan-safe area, and that every text either fits its box or is a marquee/fade element.
 *
 * Kinds: REGION (no panel of its own), PANEL (a drawn card: children keep pad_x/pad_y), TEXT (a text element's box;
 * vertical extents are line slots, the texture's empty ascender rows may stand above), DECOR (a bar, a frame, a shadow,
 * a hairline: inside its parent minus the parent's decor padding). BSD-3-Clause. */
#ifndef OM_LAYOUT_H
#define OM_LAYOUT_H
#include <stdint.h>
#include "remap.h"
#include "uifont.h"

enum { OM_BK_REGION, OM_BK_PANEL, OM_BK_TEXT, OM_BK_DECOR };
/* OM_S_LOADING: the loading screen, a screen of its own (loading_mockup_render.py); OM_S_MENU = the Folders
 * view in any of its states. */
/* OM_S_BOOT: the boot screens (the boot animation's "FARKUS 0.1" tag, the start-up interstitial). They show
 * before SETTINGS.INI is read, so they sit inside the largest safe area (OM_SAFE_BOOT, 24 px) at every overscan=
 * level. */
enum { OM_S_LIST = 1, OM_S_SETTINGS = 2, OM_S_FILTER = 4, OM_S_OVERLAY = 6, OM_S_MENU = 7, OM_S_LOADING = 8,
       OM_S_BOOT = 16, OM_S_ALL = 31 };

/*  X(name,         x0,  y0,  x1,  y1, parent,   kind,         pad_x, pad_y, decor_pad, states) */
#define OM_BOXES(X) \
    X(SCREEN,        0,   0, 319, 223, NONE,     OM_BK_REGION,  8, 8, 8, OM_S_ALL) \
    X(HEADER,        8,   8, 311,  33, SCREEN,   OM_BK_REGION,  0, 0, 0, OM_S_MENU) \
    X(LOGO,         10,   8,  49,  32, HEADER,   OM_BK_DECOR,   0, 0, 0, OM_S_MENU) \
    X(BRAND,        57,  10, 236,  29, HEADER,   OM_BK_TEXT,    0, 0, 0, OM_S_MENU) \
    X(CLOCK,       240,  10, 310,  29, HEADER,   OM_BK_TEXT,    0, 0, 0, OM_S_MENU) \
    X(HAIR,         10,  33, 310,  33, HEADER,   OM_BK_DECOR,   0, 0, 0, OM_S_MENU) \
    X(CRUMB,         8,  34, 182,  47, SCREEN,   OM_BK_REGION,  0, 0, 0, OM_S_MENU) \
    X(CRUMB_PATH,   12,  34, 124,  47, CRUMB,    OM_BK_TEXT,    0, 0, 0, OM_S_MENU) \
    X(CRUMB_COUNT, 128,  34, 180,  47, CRUMB,    OM_BK_TEXT,    0, 0, 0, OM_S_MENU) \
    X(LIST,          8,  48, 182, 166, SCREEN,   OM_BK_PANEL,   6, 2, 1, OM_S_MENU) \
    X(ROWS,         14,  50, 176, 161, LIST,     OM_BK_REGION,  0, 0, 0, OM_S_MENU) \
    X(ROW_TITLE,    16,  50, 145, 161, ROWS,     OM_BK_TEXT,    0, 0, 0, OM_S_MENU) \
    X(ROW_ICON,     16,  50,  24, 161, ROWS,     OM_BK_DECOR,   0, 0, 0, OM_S_MENU) \
    X(ROW_FTITLE,   30,  50, 174, 161, ROWS,     OM_BK_TEXT,    0, 0, 0, OM_S_MENU) \
    X(ROW_STAR,    149,  50, 156, 161, ROWS,     OM_BK_DECOR,   0, 0, 0, OM_S_MENU) \
    X(ROW_FLAG,    157,  50, 172, 161, ROWS,     OM_BK_DECOR,   0, 0, 0, OM_S_MENU) \
    X(BAR,           9,  50, 181, 161, LIST,     OM_BK_DECOR,   0, 0, 0, OM_S_MENU) \
    X(SCROLL,      179,  49, 180, 165, LIST,     OM_BK_DECOR,   0, 0, 0, OM_S_MENU) \
    X(DETAILS,       8, 170, 182, 200, SCREEN,   OM_BK_PANEL,   6, 1, 1, OM_S_MENU) \
    X(DET_TITLE,    14, 171, 176, 185, DETAILS,  OM_BK_TEXT,    0, 0, 0, OM_S_MENU) \
    X(DET_META,     14, 186, 176, 199, DETAILS,  OM_BK_TEXT,    0, 0, 0, OM_S_MENU) \
    X(COVER,       199,  43, 300, 200, SCREEN,   OM_BK_PANEL,   1, 1, 1, OM_S_LIST) \
    X(COVER_TALL,  200,  44, 299, 199, COVER,    OM_BK_DECOR,   0, 0, 0, OM_S_LIST) \
    X(COVER_SQ,    200,  72, 299, 171, COVER,    OM_BK_DECOR,   0, 0, 0, OM_S_LIST) \
    X(SHADOW,      201,  43, 304, 202, SCREEN,   OM_BK_DECOR,   0, 0, 0, OM_S_LIST) \
    X(OVERLAY,      50,  28, 269, 197, SCREEN,   OM_BK_PANEL,   8, 3, 1, OM_S_OVERLAY) \
    X(OV_TITLE,     58,  31, 261,  48, OVERLAY,  OM_BK_TEXT,    0, 0, 0, OM_S_OVERLAY) \
    X(OV_COVER,     58,  56, 101, 117, OVERLAY,  OM_BK_DECOR,   0, 0, 0, OM_S_SETTINGS) \
    X(OV_BAR,       54,  54, 265, 179, OVERLAY,  OM_BK_DECOR,   0, 0, 0, OM_S_OVERLAY) \
    X(OV_GBAR,     110,  54, 265, 179, OVERLAY,  OM_BK_DECOR,   0, 0, 0, OM_S_SETTINGS) \
    X(OV_LABEL,     58,  54, 141, 179, OVERLAY,  OM_BK_TEXT,    0, 0, 0, OM_S_OVERLAY) \
    X(OV_VALUE,    146,  54, 261, 179, OVERLAY,  OM_BK_TEXT,    0, 0, 0, OM_S_OVERLAY) \
    X(OV_RFLAG,     58,  68,  69, 137, OVERLAY,  OM_BK_DECOR,   0, 0, 0, OM_S_FILTER) \
    X(OV_RNAME,     74,  68, 170, 137, OVERLAY,  OM_BK_TEXT,    0, 0, 0, OM_S_FILTER) \
    X(OV_HINT,      58, 182, 261, 194, OVERLAY,  OM_BK_TEXT,    0, 0, 0, OM_S_OVERLAY) \
    X(FOOTER,       10, 203, 310, 215, SCREEN,   OM_BK_REGION,  0, 0, 0, OM_S_MENU) \
    X(HINTS,        10, 203, 310, 215, FOOTER,   OM_BK_TEXT,    0, 0, 0, OM_S_MENU) \
    X(TOAST,        10, 203, 310, 215, FOOTER,   OM_BK_TEXT,    0, 0, 0, OM_S_MENU) \
    X(LD_TEXT,      16,  44, 181, 215, SCREEN,   OM_BK_REGION,  0, 0, 0, OM_S_LOADING) \
    X(LD_LABEL,     16,  46, 140,  57, LD_TEXT,  OM_BK_TEXT,    0, 0, 0, OM_S_LOADING) \
    X(LD_ULINE,     16,  58,  46,  59, LD_TEXT,  OM_BK_DECOR,   0, 0, 0, OM_S_LOADING) \
    X(LD_TITLE,     16,  64, 181, 107, LD_TEXT,  OM_BK_TEXT,    0, 0, 0, OM_S_LOADING) \
    X(LD_META1,     16, 110, 181, 122, LD_TEXT,  OM_BK_TEXT,    0, 0, 0, OM_S_LOADING) \
    X(LD_META2,     16, 123, 181, 135, LD_TEXT,  OM_BK_TEXT,    0, 0, 0, OM_S_LOADING) \
    X(LD_DOTS,      16, 142,  21, 174, LD_TEXT,  OM_BK_DECOR,   0, 0, 0, OM_S_LOADING) \
    X(LD_STEPS,     26, 142, 181, 177, LD_TEXT,  OM_BK_TEXT,    0, 0, 0, OM_S_LOADING) \
    X(LD_BAR,       16, 182, 166, 186, LD_TEXT,  OM_BK_DECOR,   0, 0, 0, OM_S_LOADING) \
    X(LD_HINT,      16, 188, 181, 199, LD_TEXT,  OM_BK_TEXT,    0, 0, 0, OM_S_LOADING) \
    X(LD_STATUS,    16, 188, 181, 199, LD_TEXT,  OM_BK_TEXT,    0, 0, 0, OM_S_LOADING) \
    X(LD_LOGO,      16, 200,  37, 214, LD_TEXT,  OM_BK_DECOR,   0, 0, 0, OM_S_LOADING) \
    X(LD_BRAND,     42, 201, 181, 215, LD_TEXT,  OM_BK_TEXT,    0, 0, 0, OM_S_LOADING) \
    X(LD_ART,      186,  18, 308, 215, SCREEN,   OM_BK_REGION,  0, 0, 0, OM_S_LOADING) \
    X(LD_COVER_TALL,190, 22, 298, 200, LD_ART,   OM_BK_DECOR,   0, 0, 0, OM_S_LOADING) \
    X(LD_COVER_SQ, 190,  57, 298, 165, LD_ART,   OM_BK_DECOR,   0, 0, 0, OM_S_LOADING) \
    X(LD_SHADOW,   192,  26, 304, 208, LD_ART,   OM_BK_DECOR,   0, 0, 0, OM_S_LOADING) \
    X(LD_REFL,     190, 197, 298, 215, LD_ART,   OM_BK_DECOR,   0, 0, 0, OM_S_LOADING) \
    X(VERTAG,      248, 187, 295, 199, SCREEN,   OM_BK_TEXT,    0, 0, 0, OM_S_BOOT) \
    X(ISTL_ART,     56,  46, 264, 146, SCREEN,   OM_BK_DECOR,   0, 0, 0, OM_S_BOOT) \
    X(ISTL_TEXT,    65, 150, 254, 188, SCREEN,   OM_BK_TEXT,    0, 0, 0, OM_S_BOOT)

#define OM_BOX_ENUM(n, ...) OM_B_##n,
enum { OM_BOXES(OM_BOX_ENUM) OM_B_COUNT, OM_B_NONE = -1 };

typedef struct {
    const char *name;
    int16_t x0, y0, x1, y1;
    int8_t parent;
    uint8_t kind, pad_x, pad_y, decor_pad, states;
} om_box;
extern const om_box om_boxes[OM_B_COUNT];
#define OM_BX(n) (&om_boxes[OM_B_##n])

/* Boxes that may overlap on purpose (both orders are checked). */
typedef struct { int8_t a, b; } om_box_pair;
extern const om_box_pair om_box_layered[];
extern const uint32_t om_box_layered_count;

/* Spacing inside the boxes (mockup) */
#define OM_ROW_H         14          /* 8 rows of 14 px from ROWS.y0 */
#define OM_HDR_MID       20          /* middle lines ("lm" anchor) of the one-line texts */
#define OM_CRUMB_MID     41
#define OM_DET_TITLE_MID 178
#define OM_DET_META_MID  192
#define OM_FOOT_MID      208         /* small-face descenders end at 214, inside FOOTER (203-215) */
#define OM_HAIR_PEAK     110         /* the hairline's alpha peaks here */
#define OM_OV_TITLE_MID  40          /* the overlays: one centered panel (Settings, Filter, Launch Options) */
#define OM_OV_ROW0_MID   61          /* overlay rows: 9 rows of 14 px from OV_BAR.y0 */
#define OM_OV_ROW_H      14
#define OM_OV_RROW0      68          /* filter: the five region toggles, 14 px rows from here (addendum 6) */
#define OM_OV_RROW_H     14
#define OM_OV_HINT_MID   188         /* one hint line: a pair of hints side by side */
#define OM_SHADOW_DX     3           /* the soft shadow's offset from the art (2 px soft edge outside) */
#define OM_SHADOW_DY     1           /* the footer starts at y 203, so the shadow ends by 202 */

/* The loading screen (Task 13c, loading_mockup_render.py at native 320x224; deviations forced by the 8 px safe area are
 * in task-13c-report.md): baselines and line pitches inside the LD_ boxes. */
#define OM_LD_LABEL_BASE  55         /* "NOW LOADING", label face, letter-spaced by OM_LD_TRACK8 / 8 px */
#define OM_LD_TRACK8      13         /* mockup: 1.6 px between letters */
#define OM_LD_TITLE_BASE  82         /* hero face: line 1 baseline (mockup cap top 70), line 2 OM_LD_TITLE_LH below */
#define OM_LD_TITLE_LH    20
#define OM_LD_META_MID    116        /* small face, "lm" middles 116 and 129 */
#define OM_LD_META_LH     13
#define OM_LD_STEP_MID    148        /* the three steps: middles 148, 159, 170 (dot 6 x 6 from y mid - 3) */
#define OM_LD_STEP_H      11
#define OM_LD_HINT_MID    194        /* mockup 197: moved up 3 px so the footer ends inside the safe area */
#define OM_LD_BRAND_MID   207        /* mockup 211, logo 22 x 15 at y 204: both moved up 4 px (safe area ends at 215) */
#define OM_LD_SLIDE       30         /* the cover slides in from 30 px to the right while its tilt settles */

/* Text elements: FIT = always fits (asserted), MARQUEE = scrolls inside its box when wider, FADE = cut with a 12 px
 * fade-out at the box's right edge (non-selected list rows; everything under anim=off). Never an ellipsis (addendum 3). */
enum { OM_T_FIT, OM_T_MARQUEE, OM_T_FADE };
typedef struct { const char *s; uint8_t face, box, mode; } om_ui_text;
extern const om_ui_text om_ui_fixed[];          /* every fixed string the views draw */
extern const uint32_t om_ui_fixed_count;

/* Footer hints (mockup): badge (0 A, 1 B, 2 X, 3 Y, 4 Z, 5 ST) and label. */
#define OM_NHINTS 6
extern const char *const om_ui_hint_label[OM_NHINTS];
/* Lays the footer out like the mockup from HINTS.x0: badge x and label x per hint; returns the right edge (exclusive). */
int om_ui_hint_layout(int16_t badge_x[OM_NHINTS], int16_t label_x[OM_NHINTS]);
/* the footer hint i's action (remap.h), so its badge follows the button map; hint 5 (Settings) is Start,
 * fixed (-> OM_ACT_COUNT). */
extern const uint8_t om_ui_hint_act[OM_NHINTS];
/* the overlays' hints with the mapped letters (0/1 Settings and the game panel, 2/3 Filter), into out (at
 * least OM_OVHINT_MAX bytes); each pair is drawn as one line. test_ovpages checks every pair, with every letter, fits
 * OV_HINT. */
#define OM_OVHINT_MAX 32
void om_ovhint_text(char *out, uint8_t which, const om_remap *m);
int om_box_w(int box);
#endif
