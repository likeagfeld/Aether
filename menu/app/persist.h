/* menu/app/persist.h -- SETTINGS.INI, FAVS.TXT, RECENT.TXT on the SD card: loaded once, written when they change
 * (spec §7.6). BSD-3-Clause. */
#ifndef OM_PERSIST_H
#define OM_PERSIST_H
#include "plist.h"
#include "settings.h"
typedef struct {
    om_settings set;
    om_plist favs, recent;
    int set_rc, favs_rc, recent_rc;     /* last load result: OM_OK (also when absent), OM_E_*, or an SD code */
} om_persist;
/* order/tmp: >= l->count uint16 each (only used during the call). stage: 4-aligned scratch for FAVS/RECENT text. */
void om_persist_load(om_persist *p, const om_libidx *l, uint16_t *order, uint16_t *tmp, uint8_t *stage, uint32_t stage_cap);
/* in: OM_SETTINGS_IN bytes, 4-aligned, for the file as it is on the card; out: OM_SETTINGS_MAX bytes for the merged
 * file; both live only during the call (the menu lends two OM_LW_STAGE slots, folders.c SET_IN and SET_OUT) */
#define OM_SETTINGS_IN (OM_SETTINGS_MAX + 4u)       /* om_sd_read_all wants size + 4 */
int om_persist_save_settings(om_persist *p, uint8_t *in, char *out);
int om_persist_save_list(const om_plist *pl, const om_libidx *l, const char *path);
#endif
