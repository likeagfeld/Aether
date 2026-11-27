/* menu/ui/text.h -- VDP2 text through SGL slPrint (NBG0 ASCII cells, 40x28 at 320x224; HWF-24). BSD-3-Clause. */
#ifndef OM_TEXT_H
#define OM_TEXT_H
#include <stdint.h>
void om_text_put(int x, int y, const char *s, int width);
void om_text_clear(void);
char *om_itoa(char *buf, int v);
int om_tv_is_pal(void);
/* om_text_put's cells move in by `cells` (0..3) from every edge, so 40 - 2 * cells columns and
 * 28 - 2 * cells rows remain (the diagnostics pages follow the Safe area; the fatal screens keep 0, their fixed
 * 40-column layout). */
void om_text_set_inset(int cells);
uint32_t om_text_cut_count(void);   /* characters cut by the cap (or on a row below the area) since boot */
#endif
