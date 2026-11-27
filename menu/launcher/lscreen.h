/* menu/launcher/lscreen.h -- the launcher's pages on the loading screen's layout (menu/src/layout.h LD_*): a label
 * with its underline, a title, meta lines, body lines, a bar, a status/hint line, the wordmark, the cover on the right.
 * Every function is a no-op unless the screens are up (lgfx.h om_lg_on); a caller falls back to the text screen
 * (vdp2txt.h) then. In .lui. BSD-3-Clause. */
#ifndef OM_LSCREEN_H
#define OM_LSCREEN_H
#include <stdint.h>

#define OM_LS_X0     16                 /* the text column (LD_TEXT) */
#define OM_LS_X1     181
#define OM_LS_W      (OM_LS_X1 - OM_LS_X0 + 1)
#define OM_LS_LINES  4                  /* body lines, 11 px apart from y 142 */

/* the whole page: left column cleared, label (letter-spaced, with its underline), title (two lines at most), the
 * wordmark and the cover (dimmed when dim) */
void om_ls_page(const char *label, const char *title, int cover_dim);
void om_ls_title(const char *title);                     /* the title again (one or two lines) */
void om_ls_meta(int line, const char *s, uint16_t c);   /* line 0 or 1, under the title */
void om_ls_line(int i, const char *s, uint16_t c);      /* body line 0..OM_LS_LINES-1 */
void om_ls_bar(uint32_t num, uint32_t den);             /* den 0: no bar */
void om_ls_status(const char *s, uint16_t c);           /* the hint row */
/* 2026-11-25 (hardware photo, Manage saves: "Left/Right console, cartridge, card   A what to do" -- its "A" an
 * unreadable squiggle): one line of hints, its parts split at runs of three spaces. A part "K: what" with K one button
 * letter (A B C X Y Z L R) is the menu's badge -- a lit disc with the letter, then the words; every part is its own
 * text box, so its edges blend into the background under it (one box over the eye's bright and dark parts blended
 * into one average and lost a letter). Up to x1. */
void om_ls_hints(int x, int base, int x1, const char *s, uint16_t c);
#endif
