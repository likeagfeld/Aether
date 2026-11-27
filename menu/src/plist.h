/* menu/src/plist.h -- FAVS.TXT / RECENT.TXT (contract/SPEC.md §5.5): game paths, one per line, matched against
 * LIBRARY.IDX through a path index. Lines whose path is not on the card are dropped and counted (plan Q3). BSD-3-Clause. */
#ifndef OM_PLIST_H
#define OM_PLIST_H
#include <stdint.h>
#include "libidx.h"
#define OM_FAVS_MAX   256u
#define OM_RECENT_MAX 20u
typedef struct { uint16_t idx[OM_FAVS_MAX]; uint16_t n, cap, dropped; } om_plist;
typedef struct { const om_libidx *lib; uint16_t *order; } om_pathidx;
void om_pathidx_build(om_pathidx *px, const om_libidx *l, uint16_t *order, uint16_t *tmp);
int om_pathidx_find(const om_pathidx *px, const char *path);
void om_plist_init(om_plist *p, uint16_t cap);      /* cap is clamped to 1..OM_FAVS_MAX (0 is unsafe) */
int om_plist_parse(om_plist *p, const om_pathidx *px, const char *text, uint32_t len);
int om_plist_find(const om_plist *p, uint16_t e);
int om_plist_toggle(om_plist *p, uint16_t e);
void om_plist_push_front(om_plist *p, uint16_t e);
uint32_t om_plist_emit(const om_plist *p, const om_libidx *l, uint32_t *next, char *out, uint32_t cap);
#endif
