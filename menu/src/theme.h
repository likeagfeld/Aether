/* menu/src/theme.h -- THEME.INI (contract/SPEC.md §5.5) and the two compiled-in themes (spec §7.6). Colours are
 * VDP2 CRAM mode-0 RGB555, bit15 clear (HWF-30). panel_alpha is panel opacity in percent (plan Q7). BSD-3-Clause. */
#ifndef OM_THEME_H
#define OM_THEME_H
#include <stdint.h>
enum { OM_THEME_SATURN, OM_THEME_OPENMENU, OM_THEME_BUILTINS };
typedef struct {
    char name[32], author[32], bg[32];
    uint16_t text, text_hi, panel, cursor, bg_top, bg_bottom;
    uint8_t panel_alpha, builtin;
} om_theme;
void om_theme_builtin(om_theme *t, int which);
const char *om_theme_builtin_name(int which);
int om_theme_parse(om_theme *t, const char *text, uint32_t len);
uint16_t om_rgb555(uint32_t rrggbb);
int om_hex_rgb555(const char *s, uint16_t *out);
uint8_t om_cc_ratio(uint8_t opacity_pct);
#endif
