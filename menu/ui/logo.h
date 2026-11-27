/* menu/ui/logo.h -- the Sega Saturn logo in the menu header (replaces openMenu's Dreamcast swirl).
 * BSD-3-Clause. */
#ifndef OM_UI_LOGO_H
#define OM_UI_LOGO_H
int om_logo_init(void);
void om_logo_rebind(void);            /* re-blend the header logo against the current backdrop (theme change) */
void om_logo_draw(void);
void om_logo_draw_footer(int x, int y, int z);   /* the loading screen's own 22 x 15 footer logo, 1:1 */
#endif
