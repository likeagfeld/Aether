/* menu/app/info.h -- IP.BIN System ID cache for the highlighted game (spec §7.4 item details, §5.6 title step 3). */
#ifndef OM_INFO_H
#define OM_INFO_H
#include "libidx.h"
#include "sysid.h"
#define OM_INFO_SLOTS 32
void om_info_reset(void);
int om_info_known(uint16_t entry);
const om_sysid *om_info_find(uint16_t entry);
int om_info_load(const om_lib_entry *e, uint16_t entry);
#endif
