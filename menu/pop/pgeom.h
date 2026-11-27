/* menu/pop/pgeom.h -- the popups' layout and the view's fixed strings (pure C, shared by pdraw.c and
 * test_popgeom.c): the boxes (screen pixels before the safe-area transform), every footer's hints, and the helpers that
 * format a save's size and date. test_popgeom.c checks that no two text boxes overlap, that each box lies in the panel,
 * and that every fixed string fits the box it is drawn in. BSD-3-Clause. */
#ifndef OM_PGEOM_H
#define OM_PGEOM_H
#include <stdint.h>
#include "svmgr.h"
#include "uifont.h"

enum { PB_PANEL, PB_HDR, PB_COVER, PB_LABEL, PB_TITLE, PB_METER_T, PB_METER_B, PB_TABS, PB_LIST_S, PB_LIST_C,
       PB_SIDE_S, PB_SIDE_C, PB_FOOT, PB_N };
typedef struct { int16_t x0, y0, x1, y1; } om_pbox;
extern const om_pbox om_pd_box[PB_N];
#define OM_PD_ROW_H 14                     /* the Launch Options panel's row (layout.h OM_OV_ROW_H): the small face is 14 px */
#define OM_PD_SAVE_ROWS 9u                 /* list rows shown (Manage saves) */
#define OM_PD_CHEAT_ROWS 10u               /* list rows shown (Cheats) */
#define OM_PD_TRACK8 8                     /* the spaced label's letter spacing (1/8 px) */
#define OM_PD_SIDE_PAD 9                   /* a side line: 4 px in, 5 px before the edge */

/* a footer: hints from the left (a badge icon or none, its label), one right-aligned */
typedef struct { const om_icon *ic; const char *label; } om_pd_hint;
typedef struct { const om_pd_hint *left; uint8_t n; om_pd_hint right; } om_pd_footer;
enum { PF_CHEAT_LIST, PF_CHEAT_LIST_SAVE, PF_CHEAT_CODES, PF_CHEAT_NAME, PF_CHEAT_CODE, PF_SAVE_LIST, PF_SAVE_LIST_B,
       PF_SAVE_SETS, PF_SAVE_SETS_B, PF_SAVE_ACT, PF_SAVE_GAMES, PF_ABOUT, PF_N };
extern const om_pd_footer om_pd_footers[PF_N];
/* the pixels a footer takes: badges (w + 3), labels, 9 px between hints, 4 px margins, the right one included */
int om_pd_footer_w(const om_pd_footer *f);

void om_pd_bup_date(uint32_t minutes, char out[12]);      /* BUP date (minutes since 1980-01-01) -> "11/20 21:04" */
void om_pd_size(const om_sv_item *x, char out[12]);       /* "12 blk" / "3 KB" */
#endif
