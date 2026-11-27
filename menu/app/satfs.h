/* menu/app/satfs.h -- the om_scan_fs callback table over libsatiator, plus System ID reads for images. BSD-3-Clause. */
#ifndef OM_SATFS_H
#define OM_SATFS_H
#include "scan.h"
const om_scan_fs *om_satfs(void);
int om_satfs_sysid(const char *path, om_sysid *out);
int om_satfs_root_fp(om_scan_work *w, uint32_t *fp);
/* the Aether rename: set by om_satfs_root_fp when the root still holds the pre-rename /vibemenu folder (both folders on
 * one card: Aether Card Manager's Update card has not moved it). While set the menu does no save swap, import or save
 * management -- the console's saves would be split between the two folders -- and says why. */
extern int om_card_old_folder;
#define OM_OLD_FOLDER_MSG "Old /vibemenu folder: saves off. Card Manager > Update card"
void om_satfs_set_progress(void (*fn)(void *ctx, uint32_t dirs, uint32_t images));   /* om_scan's progress hook */
#endif
