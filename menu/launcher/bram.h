/* menu/launcher/bram.h -- the console's internal backup RAM through the BIOS backup library (M6 Task 22; LAUNCHER.BIN
 * only, nothing in the menu links it). The library is the only sanctioned way in: "When accessing the backup RAM,
 * always use the BACKUPRAM BIOS" (ST-203 Backup System Production Standard §2.0 1)); "This library must be used when
 * accessing the internal backup RAM" (BootROM_Manual.txt:339-345). It lives compressed in the boot ROM and BUP_Init
 * expands it into a caller buffer (HWF-70); no SBL object is linked (HWF-71). Device 0 = the built-in memory, 32 KB
 * (ST-162-R1 §1.2.3; HWF-72), partition 0 (selected at start, ST-162-R1 BUP_SelPart). Cartridge backup RAM (device 1)
 * is not used: Init reports it, nothing here touches it.
 *
 * Every rc is 0 or one of the library's codes (bupfile.h OM_BUP_*, HWF-72) or an OM_BRAM_E_* below; om_bup_msg turns
 * any of them into the launcher's error-screen line. The reset button is disabled (SMPC RESDISA) around Init, Write
 * and Delete, as ST-162-R1 §1.2.5 requires (HWF-72), and afterwards put back as Init found it (fix round 1, review
 * I3): RESENAB only if the SMPC status said reset was enabled; unread -> left disabled, the power-on default.
 *
 * Review M9: BUP_Init leaves the BIOS's BUP vector table (*(0x06000354)) pointing into the caller's library buffer.
 * Once that buffer is freed, no BUP call may run until om_bram_init runs again (games run their own BUP_Init).
 * BSD-3-Clause. */
#ifndef OM_BRAM_H
#define OM_BRAM_H
#include <stdint.h>
#include "bupfile.h"

#define OM_BRAM_LIB_BYTES  16384u     /* "The size of the library is 16 Kbytes" (HWF-70) */
#define OM_BRAM_WORK_BYTES 8192u      /* "A work area size of 8192 bytes is required" (HWF-70) */
#define OM_BRAM_GUARD      4u         /* a canary word after each buffer, checked after BUP_Init */
/* the caller's buffers: 4-aligned, OM_BRAM_LIB_BYTES + OM_BRAM_GUARD and OM_BRAM_WORK_BYTES + OM_BRAM_GUARD */
#define OM_BRAM_BUF_BYTES  (OM_BRAM_LIB_BYTES + OM_BRAM_WORK_BYTES + 2u * OM_BRAM_GUARD)

#define OM_BRAM_E_NOLIB    -140        /* the BIOS's BUP_Init pointer (0x06000358) is 0 or odd */
#define OM_BRAM_E_OVERRUN  -141        /* the library wrote past a buffer (a guard word changed) */
#define OM_BRAM_E_NOINIT   -142        /* a call before a successful om_bram_init */
#define OM_BRAM_E_NOUNIT   -143        /* BupConfig[0] does not report the built-in memory (unit_id 1) */
#define OM_BRAM_E_SMPC     -144        /* the SMPC never took RESDISA/RESENAB (SF stayed set) */

typedef struct { uint32_t totalsize, totalblock, blocksize, freesize, freeblock, datanum; } om_bram_info;   /* BupStat */
typedef struct { uint16_t unit_id, partition; } om_bram_unit;                                               /* BupConfig */

int om_bram_init(uint8_t *lib, uint8_t *work);                 /* BUP_Init, then BUP_Stat(0): 0, OM_BUP_UNFORMAT, ... */
/* the device every call below uses, until the next om_bram_init (which selects 0): 0 built-in, 1 cartridge. 0 or
 * OM_BUP_UNFORMAT etc. (BUP_Stat), OM_BRAM_E_NOUNIT when nothing is there. The save manager only. */
int om_bram_use(uint32_t dev);
int om_bram_list(om_bup_dir *out, uint32_t cap, uint32_t *n);  /* BUP_Dir(0, "", cap): OM_E_TABLESIZE when over cap,
                                                                  *n = the full count, cap entries filled */
int om_bram_stat(uint32_t *free_blocks, uint32_t *block_size);
int om_bram_info_get(uint32_t datasize, om_bram_info *i);      /* the whole BupStat; datanum for datasize */
/* d: the record's entry (BUP_Dir). OM_E_FULL when cap < datasize; with cap > datasize the byte after the data is a
 * guard (OM_BRAM_E_OVERRUN if the library wrote it) */
int om_bram_read(const char name[12], uint8_t *buf, uint32_t cap, om_bup_dir *d);
int om_bram_write(const om_bup_dir *d, const uint8_t *data);   /* BUP_Write (owsw OFF: overwrite), then BUP_Verify */
int om_bram_delete(const char name[12]);
#ifdef OM_L1_HARNESS
int om_bram_format(void);                                      /* BUP_Format(0): erases every record. The L1 harness's
                                                                  launcher only; not in LAUNCHER.BIN */
#endif
const om_bram_unit *om_bram_units(void);                       /* BupConfig[3] from the last Init */
uint32_t om_bram_lib_entry(void);                              /* *(0x06000358) as read by the last Init */
int om_bram_reset_mode(void);                                  /* RESD read by the last Init: 0 enabled, 1 disabled,
                                                                  < 0 unread */
#endif
