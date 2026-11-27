/* menu/app/themes.h -- the theme list behind the settings overlay (spec §7.6). BSD-3-Clause. */
#ifndef OM_THEMES_H
#define OM_THEMES_H
#include <stdint.h>
uint16_t om_themes_scan(void);
uint16_t om_themes_count(void);
const char *om_themes_name(uint16_t i);
int om_themes_find(const char *name);
int om_themes_apply(uint16_t i, uint8_t *stage, uint32_t stage_cap, char msg[41]);
#endif
