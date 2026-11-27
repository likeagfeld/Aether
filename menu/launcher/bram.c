/* menu/launcher/bram.c -- see bram.h (M6 Task 22). Every call goes through the BIOS backup library's own entry points,
 * read at run time from the BIOS's pointers (SEGA_BUP.H:25-26 and 98-136: BUP_Init at *(0x06000358), the others at
 * *(0x06000354) + 4n; HWF-70, HWF-71). The call contracts and codes are HWF-72 (ST-162-R1 Backup Library §2.4):
 * BUP_Write's owsw OFF "writes on file if file of same name exists" (printed p.45), BUP_Dir's forward name search
 * returns the hit count, negated when the table is too small (p.47). Reset is disabled around Init/Write/Delete as
 * ST-162-R1 §1.2.5 requires and SMPBUP1.C:780-791 / :825-850 do (HWF-72). Names go to the library from a 4-aligned
 * 12-byte copy, as the samples pass arrays. BSD-3-Clause. */
#include <stddef.h>
#include <string.h>
#include "sega_bup.h"
#include "bram.h"
#include "om_fmt.h"
#include "smpcpad.h"

/* om_bup_dir is passed straight to BUP_Dir/BUP_Write: its layout must be BupDir's */
_Static_assert(sizeof(om_bup_dir) == sizeof(BupDir), "om_bup_dir size != BupDir");
_Static_assert(offsetof(om_bup_dir, comment) == offsetof(BupDir, comment), "comment");
_Static_assert(offsetof(om_bup_dir, language) == offsetof(BupDir, language), "language");
_Static_assert(offsetof(om_bup_dir, date) == offsetof(BupDir, date), "date");
_Static_assert(offsetof(om_bup_dir, datasize) == offsetof(BupDir, datasize), "datasize");
_Static_assert(offsetof(om_bup_dir, blocks) == offsetof(BupDir, blocksize), "blocks");
_Static_assert(sizeof(om_bram_info) == sizeof(BupStat), "om_bram_info != BupStat");
_Static_assert(sizeof(om_bram_unit) == sizeof(BupConfig), "om_bram_unit != BupConfig");

#define GUARD_WORD 0xCAFE5A5Au
#define DEV_BUILTIN 0u                 /* device 0: built-in memory (ST-162-R1 BUP_Init table) */
#define FIND_MAX 8u                    /* BUP_Dir hits kept when looking one name up (prefix matches) */

static BupConfig s_conf[3];
static uint32_t s_entry, s_ok, s_dev;    /* s_dev: the device the calls below use (om_bram_use) */
static uint32_t s_name[3];             /* 12 B, 4-aligned */
static BupDir s_find[FIND_MAX];
static BupStat s_stat;

static const uint8_t *nm(const char name[12])
{   /* the name, NUL-padded, in a 4-aligned copy; NULL for a bad name (om_bup_name_norm) */
    return om_bup_name_norm((char *)s_name, name) ? (const uint8_t *)s_name : 0;
}

/* the reset button's mode is read once, before the first RESDISA (om_smpc_resd: SMPC status
 * OREG0 bit 6, SMPC_Manual.txt:1536-1550), and restored after every guarded call: RESENAB only if it was enabled. If
 * it could not be read, it stays disabled -- the power-on default (SMPC_Manual.txt:1339-1340, :1361-1363) and the
 * state the library requires while it writes (ST-162-R1 §1.2.5), so an unreadable SMPC never enables a reset NMI the
 * game did not ask for. */
static int s_resd = -1;              /* 0 enabled, 1 disabled, < 0 not read */
static int reset_off(void) { return om_smpc_cmd_b(OM_SMPC_RESDISA) ? OM_BRAM_E_SMPC : 0; }
static int reset_on(int rc)
{
    int r = s_resd == 0 ? om_smpc_cmd_b(OM_SMPC_RESENAB) : 0;
    return rc ? rc : r ? OM_BRAM_E_SMPC : 0;
}

int om_bram_reset_mode(void) { return s_resd; }

const om_bram_unit *om_bram_units(void) { return (const om_bram_unit *)s_conf; }
uint32_t om_bram_lib_entry(void) { return s_entry; }

int om_bram_init(uint8_t *lib, uint8_t *work)
{
    volatile uint32_t *lg = (volatile uint32_t *)(lib + OM_BRAM_LIB_BYTES);
    volatile uint32_t *wg = (volatile uint32_t *)(work + OM_BRAM_WORK_BYTES);
    int rc;
    s_ok = 0;
    s_dev = DEV_BUILTIN;
    s_entry = BUP_LIB_ADDRESS;
    if (!s_entry || (s_entry & 1u))
        return OM_BRAM_E_NOLIB;
    memset(lib, 0, OM_BRAM_LIB_BYTES);                 /* as Kai's buplib_init does before BUP_Init */
    memset(work, 0, OM_BRAM_WORK_BYTES);
    memset(s_conf, 0, sizeof s_conf);
    *lg = GUARD_WORD;
    *wg = GUARD_WORD;
    s_resd = om_smpc_resd();
    if ((rc = reset_off()) != 0)
        return rc;
    BUP_Init((volatile Uint32 *)lib, (Uint32 *)work, s_conf);
    if ((rc = reset_on(0)) != 0)
        return rc;
    if (*lg != GUARD_WORD || *wg != GUARD_WORD)
        return OM_BRAM_E_OVERRUN;
    if (s_conf[0].unit_id != BUP_MAIN_UNIT)
        return OM_BRAM_E_NOUNIT;
    s_ok = 1;
    return BUP_Stat(DEV_BUILTIN, 0, &s_stat);
}

int om_bram_use(uint32_t dev)
{   /* BUP_Init's BupConfig: unit 0 the built-in memory, 1 a cartridge, 2 the serial (floppy) port; 0 = no unit there */
    if (!s_ok) return OM_BRAM_E_NOINIT;
    if (dev > 2u || (dev && !s_conf[dev].unit_id)) return OM_BRAM_E_NOUNIT;
    s_dev = dev;
    return BUP_Stat(s_dev, 0, &s_stat);
}

int om_bram_info_get(uint32_t datasize, om_bram_info *i)
{
    if (!s_ok) return OM_BRAM_E_NOINIT;
    return BUP_Stat(s_dev, datasize, (BupStat *)i);
}

int om_bram_stat(uint32_t *free_blocks, uint32_t *block_size)
{
    om_bram_info i;
    int rc = om_bram_info_get(0, &i);
    if (rc) return rc;
    *free_blocks = i.freeblock;
    *block_size = i.blocksize;
    return 0;
}

int om_bram_list(om_bup_dir *out, uint32_t cap, uint32_t *n)
{
    Sint32 r;
    *n = 0;
    if (!s_ok) return OM_BRAM_E_NOINIT;
    if (cap > 0xFFFFu) cap = 0xFFFFu;
    memset(s_name, 0, sizeof s_name);                  /* "": every record */
    r = BUP_Dir(s_dev, (Uint8 *)s_name, (Uint16)cap, (BupDir *)out);
    if (r < 0) {
        *n = (uint32_t)-r;
        return OM_E_TABLESIZE;
    }
    *n = (uint32_t)r;
    return 0;
}

static int find(const uint8_t *name, om_bup_dir *d)
{   /* BUP_Dir matches forward (a prefix), so pick the exact name among the hits */
    Sint32 r = BUP_Dir(s_dev, (Uint8 *)name, (Uint16)FIND_MAX, s_find), k, over = r < 0;
    char got[12];
    if (over) r = FIND_MAX;
    for (k = 0; k < r; k++)
        if (om_bup_name_norm(got, (const char *)s_find[k].filename) && !memcmp(got, name, 12)) {
            memcpy(d, &s_find[k], sizeof *d);
            memcpy(d->name, got, 12);
            return 0;
        }
    return over ? OM_E_TABLESIZE : OM_BUP_NOTFOUND;       /* more prefix hits than kept is not "absent" */
}

int om_bram_read(const char name[12], uint8_t *buf, uint32_t cap, om_bup_dir *d)
{
    const uint8_t *n;
    int rc;
    if (!s_ok) return OM_BRAM_E_NOINIT;
    if (!(n = nm(name))) return OM_E_FIELD;
    if ((rc = find(n, d)) != 0) return rc;
    if (cap < d->datasize) return OM_E_FULL;
    if (cap > d->datasize) buf[d->datasize] = 0xA5;
    rc = BUP_Read(s_dev, (Uint8 *)n, buf);
    if (!rc && cap > d->datasize && buf[d->datasize] != 0xA5)
        return OM_BRAM_E_OVERRUN;
    return rc;
}

int om_bram_write(const om_bup_dir *d, const uint8_t *data)
{
    const uint8_t *n;
    int rc;
    BupDir w;
    if (!s_ok) return OM_BRAM_E_NOINIT;
    if (!(n = nm(d->name))) return OM_E_FIELD;
    memcpy(&w, d, sizeof w);                           /* BUP_Write takes all but blocksize as input (HWF-72) */
    memcpy(w.filename, n, 12);
    if ((rc = reset_off()) != 0) return rc;
    rc = reset_on(BUP_Write(s_dev, &w, (volatile Uint8 *)data, OFF));
    if (rc) return rc;
    return BUP_Verify(s_dev, (Uint8 *)n, (volatile Uint8 *)data);
}

int om_bram_delete(const char name[12])
{
    const uint8_t *n;
    int rc;
    if (!s_ok) return OM_BRAM_E_NOINIT;
    if (!(n = nm(name))) return OM_E_FIELD;
    if ((rc = reset_off()) != 0) return rc;
    return reset_on(BUP_Delete(s_dev, (Uint8 *)n));
}

#ifdef OM_L1_HARNESS
int om_bram_format(void)
{   /* bramtest.c calls this only under the L1 harness's flag, on an emulator's unformatted backup RAM */
    int rc;
    if (!s_ok) return OM_BRAM_E_NOINIT;
    if ((rc = reset_off()) != 0) return rc;
    rc = reset_on(BUP_Format(s_dev));
    if (rc) return rc;
    return BUP_Stat(s_dev, 0, &s_stat);
}
#endif
