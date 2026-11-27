/* menu/src/loadbg.h -- LOADBG.BIN (contract/SPEC.md §5.11): the loading screen's looping background.
 * BOOTANIM.BIN's layout and byte ops (menu/src/bootanim.c om_ba_ops, unchanged) with magic OMSATLBG and codec 3: index
 * entry 0 is the keyframe (frame 0 over zeros); entries 1..N are a seamless loop, entry f + 1 holding frame f decoded IN
 * PLACE over frame f - 1 (frame 0 over frame N - 1). The file is read into RAM (the menu borrows the card-scan work
 * area); one w x h buffer holds the frame on screen. Pure C, host-tested (menu/host-tests/test_loadbg.c). BSD-3-Clause. */
#ifndef OM_LOADBG_H
#define OM_LOADBG_H
#include <stdint.h>

#define OM_LBG_CODEC      3u
#define OM_LBG_MAX_W      320u
#define OM_LBG_MAX_H      224u
#define OM_LBG_MAX_ENTRY  65535u            /* compressed bytes per entry (the keyframe; tools/omsat/loadbg.py) */
#define OM_LBG_E_INFO     -86               /* info block or index out of the menu's limits */
#define OM_LBG_E_CRC      -87               /* a decoded frame does not match its index CRC-32 */
#define OM_LBG_E_DMA      -89               /* the menu: an SCU DMA of the background did not finish in time */

/* The file's bytes past its HWRAM part (SPEC §5.11; ledger D3), v = a byte's offset in that part, in three regions, in
 * this order:
 * - SR1: sound RAM 0x40000-0x77FFF, free while the menu runs (no clip, ring, slot or the parked
 *   DSP ring at 0x78000 reads it), so the per-game panel can fill it with the music playing;
 * - LW: 92,160 B of LWRAM, OM_LW_STAGE + 0x8800 .. + 0x1F000 (omlayout.h OM_LW_LBG), writable while the
 *   music plays; its entries are decoded in place by the CPU (SCU DMA cannot read Work RAM-L: SCU_Manual.txt Figure
 *   2.2); the stage's other tenants may overwrite it, so loading.c checksums it;
 * - SR2: the music region 0x10000-0x2FFFF, written only after om_audio_sndram_borrow (the music has faded). */
#define OM_LBG_SR1_OFF    0x40000u
#define OM_LBG_SR1_LEN    0x38000u
#define OM_LBG_LW_LEN     0x16800u
#define OM_LBG_SR2_OFF    0x10000u
#define OM_LBG_SR2_LEN    0x20000u
#define OM_LBG_LW_V0      OM_LBG_SR1_LEN                    /* v of LW's first byte */
#define OM_LBG_SR2_V0     (OM_LBG_SR1_LEN + OM_LBG_LW_LEN)  /* v of the music region's first byte */
static inline uint32_t om_lbg_sr_off(uint32_t v)            /* an SR1 or SR2 byte's sound-RAM offset (not LW's) */
{
    return v < OM_LBG_SR1_LEN ? OM_LBG_SR1_OFF + v : OM_LBG_SR2_OFF + (v - OM_LBG_SR2_V0);
}
static inline int om_lbg_sr_music(uint32_t v) { return v >= OM_LBG_SR2_V0; }   /* in the borrowed music region */
static inline int om_lbg_lw(uint32_t v) { return v >= OM_LBG_LW_V0 && v < OM_LBG_SR2_V0; }   /* in LWRAM */
/* the end (exclusive) of v's region: a read or a staging copy never crosses it */
static inline uint32_t om_lbg_region_end(uint32_t v)
{
    return v < OM_LBG_LW_V0 ? OM_LBG_LW_V0 : v < OM_LBG_SR2_V0 ? OM_LBG_SR2_V0 : 0xFFFFFFFFu;
}

typedef struct {
    const uint8_t *file, *index, *palette;
    uint32_t n;                             /* loop frames (the index holds n + 1 entries) */
    uint32_t w, h, scale, fps;              /* fps 60 or 30 (each frame shown for 1 or 2 V-blanks) */
} om_lbg;
/* The first `have` bytes of a file of file_size bytes are in memory (at least om_lbg_head_bytes): the §5.1 header, its
 * index CRC, the info block and every entry's range. The entries themselves may still be arriving (om_lbg_need). */
int om_lbg_open(om_lbg *g, const uint8_t *file, uint32_t have, uint32_t file_size);
uint32_t om_lbg_head_bytes(const uint8_t *file, uint32_t have);  /* bytes open needs: header + index (0 if have < 64) */
uint16_t om_lbg_color(const om_lbg *g, uint32_t i);             /* RGB555, bit 15 clear */

typedef struct {
    uint8_t *pix;                           /* w * h bytes: the frame on screen */
    uint32_t frame;                         /* the loop frame pix holds */
    const uint8_t *ops;                     /* the ops of the last step (for om_lbg_runs) */
    uint32_t ops_len;
} om_lbg_play;
/* Decodes the keyframe into pix (frame 0) and checks its CRC-32. */
int om_lbg_start(om_lbg_play *p, const om_lbg *g, uint8_t *pix);
/* File bytes that must have arrived before frame's entry can be applied (entry 0, the keyframe, comes first); host. */
uint32_t om_lbg_need(const om_lbg *g, uint32_t frame);
uint32_t om_lbg_key_need(const om_lbg *g);
/* Advances one frame in place (frame + 1, wrapping to 0). check != 0 also verifies the CRC-32. Host only (loadbg_host.c). */
int om_lbg_step(om_lbg_play *p, const om_lbg *g, int check);
uint32_t om_lbg_next(const om_lbg_play *p, const om_lbg *g);      /* the frame the next step decodes */
void om_lbg_entry_of(const om_lbg *g, uint32_t frame, uint32_t *off, uint32_t *len);   /* frame's entry in the file */

/* A step in pieces (Task 13c round 2: a full-res frame's decode can exceed what is left of a V-blank): begin, then
 * run up to max_ops ops a call (1 = more, 0 = done and p->frame advanced, < 0 = error), the same byte ops and checks as
 * om_ba_ops. check != 0 verifies the CRC-32 when done. */
typedef struct { const uint8_t *s, *end; uint32_t pos, next, crc; int check; } om_lbg_dec;
int om_lbg_dec_begin(om_lbg_dec *d, om_lbg_play *p, const om_lbg *g, const uint8_t *ops, uint32_t len, int check);
int om_lbg_dec_run(om_lbg_dec *d, om_lbg_play *p, const om_lbg *g, uint32_t max_ops);

/* Host tests and tools only (menu/src/loadbg_host.c, not in the Saturn builds): om_lbg_need above, and the pixels the
 * last step changed, as row runs: emit(ctx, row, col, n) with col + n <= w, in raster order. Walks the
 * op headers only (SKIP runs are what stayed), so an uploader writes nothing else. Returns the runs emitted. */
typedef void (*om_lbg_emit)(void *ctx, uint32_t row, uint32_t col, uint32_t n);
uint32_t om_lbg_runs(const om_lbg_play *p, const om_lbg *g, om_lbg_emit emit, void *ctx);
/* The same, resumable (an uploader with a time budget): next() gives the next changed run, 0 at the end. */
typedef struct { const uint8_t *s, *end; uint32_t row, col, left, rrow, rcol; } om_lbg_it;
void om_lbg_it_init(om_lbg_it *it, const om_lbg_play *p);
int om_lbg_it_next(om_lbg_it *it, const om_lbg *g, uint32_t *row, uint32_t *col, uint32_t *n);
#endif
