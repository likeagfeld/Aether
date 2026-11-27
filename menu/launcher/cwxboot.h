/* menu/launcher/cwxboot.h -- the CWX boot (M6 Task 17; ruling D-CWX, docs/research/m6-research.md §2.1, §2.5-§2.6):
 * Pseudo Saturn Kai's self-loader (cafe-alpha, GPL-2.0; CyberWarriorX's original Pseudo Saturn loader), re-implemented
 * on our CD-block layer. No bios_loadcd_boot: the launcher reads the IP and the 1st Read File itself, arms the
 * resident chain, resets the video hardware and enters the IP at 0x06002100. The BIOS's header, security-code and
 * region checks never run, so no region patch is applied. "JHL" (jhboot.h) stays the BIOS boot, unchanged.
 * Called at launch step 9 in CD-ROM mode, display off. BSD-3-Clause. */
#ifndef OM_CWXBOOT_H
#define OM_CWXBOOT_H
#include <stdint.h>
#include "jhboot.h"
#include "chkai.h"

/* Boots the disc; returns only on failure, before anything irreversible beyond the High Work RAM clear (which a
 * failed launch forfeits anyway, lmain.c). tr: as om_boot_disc's -- head = the IP's first 32 bytes, init_rc =
 * om_cd_reset, auth, read_rc = the IP read, put_rc = the 1st Read File read, boot_rc = the result. igr: arm the
 * in-game reset (igr.h; tr->igr says whether it was). */
int om_cwx_boot(om_boot_trace *tr, int igr, om_chk_plan *chk);   /* chk: NULL, or Kai's cheats */
const om_boot_trace *om_cwx_last(void);
const char *om_cwx_boot_strerror(int rc);   /* "CWX: ..." */

/* The resident chain (cwxtail.c's hook): `len` bytes (a multiple of 4) staged in the tail and copied to `dst` from
 * inside the IP's security code, after the SEGA logo is unpacked; `execdmp`, if non-zero, then becomes SYS_EXECDMP.
 * om_cwx_resident is the one place that decides what is armed -- the IGR stub today (dst 0x06002800, igr.h); Task
 * 27a prepends its cheat block (0x06002580-0x060027FF, contiguous with the stub) here. Layout shared with the asm. */
typedef struct { uint32_t dst, len, sum, execdmp; } om_cwx_res;
#endif
