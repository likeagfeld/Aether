/* menu/host-tests/fake_bram.c -- host stand-in for menu/launcher/bram.c: the built-in backup RAM as an
 * in-memory record table with the library's contracts (bram.h; ST-162-R1 §2.4 as HWF-72 records them): BUP_Write with
 * owsw OFF overwrites, a full device returns OM_BUP_NOMEM, BUP_Read/BUP_Delete of an absent name OM_BUP_NOTFOUND.
 * Geometry as Mednafen reported it in Task 22 (512 blocks of 64 B; 2 taken by the system: 509 free with a 1-block
 * record). A record's block count here is ceil((datasize + 4) / 58) + fake_bram_extra -- deliberately NOT the planner's
 * estimate, so CHECK_FITS is tested against the device's own BUP_Dir counts.
 * Faults: every write/delete calls fake_satiator.c's fake_mutate (one counter with the SD calls): an injected rc is
 * returned without effect; a power cut tears the call (a write stores flipped data and marks the record broken, a
 * delete leaves it broken) and then every call fails until fake_reboot. BSD-3-Clause. */
#include <stdlib.h>
#include <string.h>
#include "bram.h"
#include "om_fmt.h"

#define MAXREC 600
#define TOTAL  512u
#define SYSBL  2u
typedef struct { om_bup_dir d; uint8_t *data; int broken; } rec;
static rec s_r[MAXREC], s_other[MAXREC];   /* s_other: the device not selected (om_bram_use) */
static int s_n, s_init, s_nother, s_dev;
int fake_bram_cart;             /* a cartridge is plugged in (device 1) */
int fake_bram_inits, fake_bram_calls, fake_bram_extra, fake_bram_unformat;
int fake_bram_lists, fake_bram_reads;   /* the save swap's cost (test_pgscost.c): listings and record reads */
int fake_bram_writes;           /* BUP_Write + BUP_Delete calls that reached the device (the backup's gate) */
extern int fake_dead;
int fake_mutate(void);

static uint32_t blocks_of(uint32_t size) { return (size + 4u + 57u) / 58u + (uint32_t)fake_bram_extra; }

static uint32_t used(void)
{
    uint32_t u = SYSBL;
    int i;
    for (i = 0; i < s_n; i++) u += s_r[i].d.blocks;
    return u;
}

static int find(const char name[12])
{
    char a[12], b[12];
    int i;
    if (!om_bup_name_norm(a, name)) return -2;
    for (i = 0; i < s_n; i++)
        if (om_bup_name_norm(b, s_r[i].d.name) && !memcmp(a, b, 12)) return i;
    return -1;
}

void fake_bram_reset(void)
{
    int i;
    for (i = 0; i < s_n; i++) free(s_r[i].data);
    for (i = 0; i < s_nother; i++) free(s_other[i].data);
    s_n = s_nother = s_dev = 0;
    fake_bram_cart = 0;
    s_init = 0;
    fake_bram_inits = fake_bram_calls = fake_bram_extra = fake_bram_unformat = fake_bram_writes = 0;
}

/* test setup: a record put straight in (no fault counting) */
void fake_bram_put(const char *name, const uint8_t *data, uint32_t size, uint32_t date)
{
    rec *r = &s_r[s_n++];
    memset(r, 0, sizeof *r);
    strncpy(r->d.name, name, 11);
    memcpy(r->d.comment, "TEST", 5);
    r->d.language = 1;
    r->d.date = date;
    r->d.datasize = size;
    r->d.blocks = (uint16_t)blocks_of(size);
    r->data = (uint8_t *)malloc(size ? size : 1);
    memcpy(r->data, data, size);
}

/* test setup: a record removed straight away (another menu's session; no fault counting) */
void fake_bram_del(const char *name)
{
    char n[12];
    int k;
    memset(n, 0, 12);
    strncpy(n, name, 11);
    if ((k = find(n)) < 0) return;
    free(s_r[k].data);
    memmove(&s_r[k], &s_r[k + 1], (size_t)(s_n - k - 1) * sizeof s_r[0]);
    s_n--;
}

int fake_bram_count(void) { return s_n; }
const om_bup_dir *fake_bram_dir(int i) { return &s_r[i].d; }
const uint8_t *fake_bram_data(int i, int *broken) { *broken = s_r[i].broken; return s_r[i].data; }
int fake_bram_find(const char *name)
{
    char n[12];
    memset(n, 0, 12);
    strncpy(n, name, 11);
    return find(n);
}

int om_bram_use(uint32_t dev)
{
    static rec t[MAXREC];
    int k;
    fake_bram_calls++;
    if (!s_init) return OM_BRAM_E_NOINIT;
    if (dev > 1u || (dev && !fake_bram_cart)) return OM_BRAM_E_NOUNIT;
    if ((int)dev != s_dev) {                    /* swap the two record tables */
        memcpy(t, s_r, sizeof t); memcpy(s_r, s_other, sizeof t); memcpy(s_other, t, sizeof t);
        k = s_n; s_n = s_nother; s_nother = k;
        s_dev = (int)dev;
    }
    return 0;
}

int om_bram_init(uint8_t *lib, uint8_t *work)
{
    if (s_dev) om_bram_use(0);
    (void)lib; (void)work;
    fake_bram_inits++;
    fake_bram_calls++;
    if (fake_dead) return OM_BRAM_E_SMPC;
    s_init = 1;
    return fake_bram_unformat ? OM_BUP_UNFORMAT : 0;
}

int om_bram_info_get(uint32_t datasize, om_bram_info *i)
{
    fake_bram_calls++;
    if (!s_init) return OM_BRAM_E_NOINIT;
    if (fake_dead) return OM_BUP_NON;
    memset(i, 0, sizeof *i);
    i->totalblock = TOTAL; i->blocksize = 64; i->totalsize = TOTAL * 64u;
    i->freeblock = TOTAL - used(); i->freesize = i->freeblock * 64u;
    i->datanum = datasize ? i->freeblock / blocks_of(datasize) : 0;
    return 0;
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
    int i;
    fake_bram_calls++;
    fake_bram_lists++;
    *n = 0;
    if (!s_init) return OM_BRAM_E_NOINIT;
    if (fake_dead) return OM_BUP_NON;
    for (i = 0; i < s_n && (uint32_t)i < cap; i++) out[i] = s_r[i].d;
    *n = (uint32_t)s_n;
    return (uint32_t)s_n > cap ? OM_E_TABLESIZE : 0;
}

int om_bram_read(const char name[12], uint8_t *buf, uint32_t cap, om_bup_dir *d)
{
    int k;
    fake_bram_calls++;
    fake_bram_reads++;
    if (!s_init) return OM_BRAM_E_NOINIT;
    if (fake_dead) return OM_BUP_NON;
    if ((k = find(name)) == -2) return OM_E_FIELD;
    if (k < 0) return OM_BUP_NOTFOUND;
    *d = s_r[k].d;
    if (cap < d->datasize) return OM_E_FULL;
    if (s_r[k].broken) return OM_BUP_BROKEN;
    memcpy(buf, s_r[k].data, d->datasize);
    return 0;
}

int om_bram_write(const om_bup_dir *d, const uint8_t *data)
{
    int k, m;
    uint32_t need = blocks_of(d->datasize), have;
    rec *r;
    fake_bram_calls++;
    if (!s_init) return OM_BRAM_E_NOINIT;
    if (fake_dead) return OM_BUP_NON;
    if ((k = find(d->name)) == -2) return OM_E_FIELD;
    have = TOTAL - used() + (k >= 0 ? s_r[k].d.blocks : 0u);
    if (need > have) return OM_BUP_NOMEM;
    fake_bram_writes++;
    if ((m = fake_mutate()) < 0) return m;
    if (k < 0) { k = s_n++; memset(&s_r[k], 0, sizeof s_r[k]); }
    r = &s_r[k];
    free(r->data);
    r->d = *d;
    om_bup_name_norm(r->d.name, d->name);
    r->d.blocks = (uint16_t)need;
    r->data = (uint8_t *)malloc(d->datasize ? d->datasize : 1);
    memcpy(r->data, data, d->datasize);
    r->broken = 0;
    if (m) {                                   /* the power went during the write: a torn record */
        if (d->datasize) r->data[0] ^= 0x5A;
        r->broken = 1;
        fake_dead = 1;
        return OM_BUP_NON;
    }
    return 0;
}

int om_bram_delete(const char name[12])
{
    int k, m;
    fake_bram_calls++;
    if (!s_init) return OM_BRAM_E_NOINIT;
    if (fake_dead) return OM_BUP_NON;
    if ((k = find(name)) == -2) return OM_E_FIELD;
    if (k < 0) return OM_BUP_NOTFOUND;
    fake_bram_writes++;
    if ((m = fake_mutate()) < 0) return m;
    if (m) { s_r[k].broken = 1; fake_dead = 1; return OM_BUP_NON; }
    free(s_r[k].data);
    memmove(&s_r[k], &s_r[k + 1], (size_t)(s_n - k - 1) * sizeof s_r[0]);
    s_n--;
    return 0;
}

const om_bram_unit *om_bram_units(void) { static om_bram_unit u[3] = { { 1, 1 } }; return u; }
uint32_t om_bram_lib_entry(void) { return 0x7D600u; }
int om_bram_reset_mode(void) { return 1; }

/* a record torn in place (as a power cut during the game's own save), for the backup's tests */
void fake_bram_tear(const char *name)
{
    int k = fake_bram_find(name);
    if (k >= 0) s_r[k].broken = 1;
}
