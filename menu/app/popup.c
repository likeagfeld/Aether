/* menu/app/popup.c -- see popup.h. POPUP.BIN is a file of the menu's own disc (ISO 9660 root, isodir.h): on the card
 * the menu reads it out of /autoboot.iso (API mode, 2 KB reads, the music refilled between them); the fake-card build
 * reads it from the CD with cdblk.c, as it reads LOADBG.BIN. A copy still intact in OM_LW_LBG (its CRC matches) is used
 * as it is. BSD-3-Clause. */
#include <string.h>
#include "sgl.h"
#include "audio.h"
#include "isodir.h"
#include "omlayout.h"
#include "om_fmt.h"
#include "popup.h"
#ifdef OMSAT_FAKECARD
#include "cdblk.h"
#else
#include "satiator.h"
#include "sd.h"
#endif

/* .data (a non-zero initializer: a zero one would go to .bss, outside 0.BIN): sbuild finds the word by its symbol and
 * writes this build's POPUP.BIN CRC there; the placeholder itself never matches (img_ok) */
volatile uint32_t om_pop_crc __attribute__((used)) = OM_POP_CRC_UNSET;

#define IMG     ((uint8_t *)OM_LW_LBG)
#define HDR     ((const om_pop_hdr *)OM_LW_LBG)
#define DIR_MAX 8192u
static uint8_t s_is_open;
static int (*s_abort)(void);               /* om_pop_setup only */

static int img_ok(void)
{
    const om_pop_hdr *h = HDR;
    uint32_t want = om_pop_crc;
    if (want == OM_POP_CRC_UNSET || memcmp(h->magic, OM_POP_MAGIC, 8) || h->version != OM_POP_VERSION) return 0;
    if (h->img_len < sizeof *h || h->img_len > OM_LW_LBG_MAX || h->bss_start < OM_LW_LBG + h->img_len ||
        h->bss_end < h->bss_start || h->bss_end > OM_LW_LBG + OM_LW_LBG_MAX)
        return 0;
    return om_crc32(0, (const void *)OM_CT(OM_LW_LBG), h->img_len) == want;
}

/* n sectors of the menu's disc from lba into dst (4-aligned) */
#ifdef OMSAT_FAKECARD
typedef int disc;
static int disc_open(disc *d) { int r4[4]; *d = 0; om_cd_spin_cap(100000u); om_cd_reset(r4); return 0; }
static int disc_read(disc *d, uint32_t lba, uint8_t *dst, uint32_t n)
{
    (void)d;
    while (n) {
        uint32_t k = n > 32u ? 32u : n;
        int rc = om_cd_read(dst, (int32_t)(lba + 150u), (int32_t)k);
        if (rc) return rc;
        om_audio_tick();
        if (s_abort && s_abort()) return OM_POP_E_ABORT;   /* the console setup run: a key was pressed */
        lba += k; dst += k * OM_SECTOR; n -= k;
    }
    return 0;
}
static void disc_close(disc *d) { (void)d; }
#else
typedef int disc;
static int disc_open(disc *d)
{
    *d = om_sd_open(OM_POP_ISO_PATH);
    return *d < 0 ? *d : 0;
}
static int disc_read(disc *d, uint32_t lba, uint8_t *dst, uint32_t n)
{
    while (n) {                               /* a sector a call: the music's 2 KB refills keep up */
        int rc = om_sd_read_at(*d, lba * OM_SECTOR, dst, OM_SECTOR);
        if (rc) return rc;
        om_audio_tick();
        if (s_abort && s_abort()) return OM_POP_E_ABORT;   /* the console setup run: a key was pressed */
        lba++; dst += OM_SECTOR; n--;
    }
    return 0;
}
static void disc_close(disc *d) { if (*d >= 0) s_close(*d); *d = -1; }
#endif

static int load(uint8_t *scratch)
{
    disc d;
    uint32_t lba, size, n;
    int rc;
    if ((rc = disc_open(&d)) != 0) return OM_POP_E_NONE;
    if ((rc = disc_read(&d, OM_ISO_PVD_LBA, scratch, 1)) == 0 && (rc = om_iso_root(scratch, &lba, &size)) == 0) {
        if (size > DIR_MAX) size = DIR_MAX;
        n = (size + OM_SECTOR - 1u) / OM_SECTOR;
        if ((rc = disc_read(&d, lba, scratch, n)) == 0 &&
            (rc = om_iso_find(scratch, n * OM_SECTOR, OM_POP_ISO_NAME, &lba, &size)) == 0) {
            n = (size + OM_SECTOR - 1u) / OM_SECTOR;
            if (n * OM_SECTOR > OM_LW_LBG_MAX) rc = OM_E_RANGE;
            else rc = disc_read(&d, lba, (uint8_t *)OM_CT(OM_LW_LBG), n);   /* HWF-12: cache-through */
        }
    }
    disc_close(&d);
    slCashPurge();                            /* no stale line of the code just written (ST-238-R1) */
    if (rc == OM_ISO_E_NONE) return OM_POP_E_NONE;
    if (rc) return rc;
    return img_ok() ? 0 : OM_POP_E_BUILD;
}

int om_pop_open(const om_pop_req *r, uint8_t *scratch, char *why, uint32_t why_cap)
{
    int rc;
    if (why_cap) why[0] = 0;
    if (!img_ok() && (rc = load(scratch)) != 0) {
        if (why_cap > 1) {
            strncpy(why, rc == OM_POP_E_BUILD ? "POPUP.BIN is another build's" : "No POPUP.BIN on the menu's disc",
                    why_cap - 1);
            why[why_cap - 1] = 0;
        }
        return rc;
    }
    memset((void *)OM_CT(HDR->bss_start), 0, HDR->bss_end - HDR->bss_start);
    slCashPurge();
    rc = HDR->open(r, why, why_cap);
    if (why_cap) why[why_cap - 1] = 0;
    if (rc < 0) {
        HDR->close();
        return rc;
    }
    s_is_open = 1;
    return 0;
}

int om_pop_active(void) { return s_is_open; }

uint32_t om_pop_key(uint16_t press, uint16_t rep)
{
    uint32_t f;
    if (!s_is_open) return OM_POP_CLOSED;
    f = HDR->key(press, rep);
    if (f & OM_POP_CLOSED) {
        HDR->close();
        s_is_open = 0;
    }
    return f;
}

void om_pop_draw(uint16_t vis, uint32_t frame)
{
    if (s_is_open) HDR->draw(vis, frame);
}

void om_pop_abort(void)
{
    if (!s_is_open) return;
    HDR->close();
    s_is_open = 0;
}

int om_pop_setup(uint8_t *arena, uint32_t cap, uint32_t games, uint8_t loader, int (*abort)(void))
{
    om_pop_setup_req q;
    int rc;
    if (s_is_open) return OM_POP_E_BUILD;
    q.arena = arena; q.arena_cap = cap; q.games = games; q.loader = loader;
    s_abort = abort;
    rc = img_ok() ? 0 : load(arena);
    s_abort = 0;
    if (rc) return rc;
    memset((void *)OM_CT(HDR->bss_start), 0, HDR->bss_end - HDR->bss_start);
    slCashPurge();
    return HDR->setup(&q);
}
