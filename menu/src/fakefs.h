/* menu/src/fakefs.h -- OMFAKEFS, the test-only card image the --app-fake build embeds (plan D6): big-endian header
 * "OMFAKEFS", u32 count, u32 0, then count x { char path[256]; u32 offset; u32 size }, then data. BSD-3-Clause. */
#ifndef OM_FAKEFS_H
#define OM_FAKEFS_H
#include <stdint.h>
#define OM_FAKEFS_ENT 264u
typedef struct { const uint8_t *base; uint32_t len, count; } om_fakefs;
int om_fakefs_open(om_fakefs *f, const uint8_t *blob, uint32_t len);
int om_fakefs_find(const om_fakefs *f, const char *path, uint32_t *off, uint32_t *size);
int om_fakefs_child(const om_fakefs *f, const char *dir, uint32_t i, char name[256], int *is_dir);
#endif
