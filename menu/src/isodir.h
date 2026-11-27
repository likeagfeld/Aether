/* menu/src/isodir.h -- find a file in an ISO 9660 root directory (ECMA-119), for BOOTANIM.BIN on the menu disc.
 * Pure C: the caller reads the sectors (menu/launcher/cdblk.c). BSD-3-Clause. */
#ifndef OM_ISODIR_H
#define OM_ISODIR_H
#include <stdint.h>
#define OM_ISO_PVD_LBA 16u                   /* ECMA-119 §6.2.1 / §8.4: the Primary Volume Descriptor at sector 16 */
#define OM_ISO_E_PVD   -71                   /* sector 16 is not "\1CD001" */
#define OM_ISO_E_ROOT  -72                   /* the root directory record is malformed */
#define OM_ISO_E_NONE  -73                   /* no such file in the directory */
/* The root directory's extent (LBA, bytes) from a PVD sector (2,048 B). */
int om_iso_root(const uint8_t *pvd, uint32_t *lba, uint32_t *size);
/* Looks `name` (upper case, e.g. "BOOTANIM.BIN") up in len bytes of directory records; ";1" version suffixes and a
 * trailing '.' are ignored. Records do not cross 2,048-byte sector boundaries (ECMA-119 §6.8.1.1). */
int om_iso_find(const uint8_t *dir, uint32_t len, const char *name, uint32_t *lba, uint32_t *size);
#endif
